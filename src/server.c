#include "../include/server.h"
#include "../include/ws.h"
#include "../include/http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

/* ------------------------------------------------------------------ */
long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void add_to_epoll(int epfd, int fd, uint32_t events) {
    struct epoll_event ev = {0};
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

static void mod_epoll(int epfd, int fd, uint32_t events) {
    struct epoll_event ev = {0};
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}

int server_init(server_t *s) {
    memset(s, 0, sizeof(*s));

    /* Railway (and most PaaS) inject PORT env var — use it if set */
    int port = SERVER_PORT;
    const char *port_env = getenv("PORT");
    if (port_env && atoi(port_env) > 0) port = atoi(port_env);

    s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->listen_fd < 0) return -1;

    int opt = 1;
    setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_nonblocking(s->listen_fd);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(s->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return -1;
    }
    if (listen(s->listen_fd, 32) < 0) return -1;

    s->epfd = epoll_create1(0);
    if (s->epfd < 0) return -1;

    add_to_epoll(s->epfd, s->listen_fd, EPOLLIN);
    printf("[server] listening on port %d\n", port);
    return 0;
}

static client_t *client_new(int fd) {
    client_t *c = calloc(1, sizeof(client_t));
    if (!c) return NULL;
    c->fd = fd;
    c->type = CONN_HTTP;
    c->channel_id = 1; /* default channel */
    return c;
}

static void client_free(server_t *s, client_t *c) {
    epoll_ctl(s->epfd, EPOLL_CTL_DEL, c->fd, NULL);
    close(c->fd);
    for (int i = 0; i < s->client_count; i++) {
        if (s->clients[i] == c) {
            s->clients[i] = s->clients[--s->client_count];
            break;
        }
    }
    free(c);
}

client_t *server_get_client(server_t *s, int fd) {
    for (int i = 0; i < s->client_count; i++) {
        if (s->clients[i]->fd == fd) return s->clients[i];
    }
    return NULL;
}

static void handle_accept(server_t *s) {
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int fd = accept(s->listen_fd, (struct sockaddr*)&addr, &addrlen);
    if (fd < 0) return;

    if (s->client_count >= MAX_CLIENTS) {
        close(fd);
        return;
    }

    set_nonblocking(fd);
    client_t *c = client_new(fd);
    if (!c) { close(fd); return; }

    s->clients[s->client_count++] = c;
    add_to_epoll(s->epfd, fd, EPOLLIN | EPOLLET);
}

static void handle_read(server_t *s, client_t *c) {
    if (c->type == CONN_WEBSOCKET) {
        char msg[BUFFER_SIZE];
        int msg_len = 0;
        int opcode  = 0;
        int ret = ws_read_frame(c, msg, &msg_len, &opcode);
        if (ret < 0) {
            /* notify voice room on disconnect */
            if (c->in_voice) {
                extern void voice_leave(server_t *s, client_t *c);
                voice_leave(s, c);
            }
            client_free(s, c);
            return;
        }
        if (ret == 0 || msg_len == 0) return;

        if (opcode == WS_OPCODE_BINARY) {
            /* audio frame — relay to voice room */
            extern void voice_relay(server_t *s, client_t *c,
                                    const unsigned char *data, int len);
            voice_relay(s, c, (const unsigned char *)msg, msg_len);
        } else {
            extern void ws_handle_message(server_t *s, client_t *c,
                                          const char *msg, int len);
            ws_handle_message(s, c, msg, msg_len);
        }
        return;
    }

    /* HTTP read */
    int n = read(c->fd, c->read_buf + c->read_len, BUFFER_SIZE - c->read_len - 1);
    if (n <= 0) {
        client_free(s, c);
        return;
    }
    c->read_len += n;
    c->read_buf[c->read_len] = '\0';

    /* check if full HTTP request received */
    if (!strstr(c->read_buf, "\r\n\r\n")) return;

    http_request_t req;
    memset(&req, 0, sizeof(req));
    int parse_ret = http_parse(c->read_buf, c->read_len, &req);
    if (parse_ret < 0) {
        client_free(s, c);
        return;
    }
    if (parse_ret == 1) return; /* body not fully received yet, keep buffering */
    c->read_len = 0;

    if (req.is_ws_upgrade) {
        /* do websocket handshake, upgrade conn */
        memcpy(c->ws_key_buf, req.ws_key, sizeof(req.ws_key));
        if (ws_handshake(c) == 0) {
            c->type = CONN_WEBSOCKET;
        } else {
            client_free(s, c);
        }
        return;
    }

    http_handle(s, c, &req);
}

static void handle_write(server_t *s, client_t *c) {
    if (c->write_len <= 0) return;
    int n = write(c->fd, c->write_buf, c->write_len);
    if (n < 0) {
        client_free(s, c);
        return;
    }
    if (n < c->write_len) {
        memmove(c->write_buf, c->write_buf + n, c->write_len - n);
    }
    c->write_len -= n;
    if (c->write_len == 0) {
        mod_epoll(s->epfd, c->fd, EPOLLIN | EPOLLET);
    }
}

void server_poll(server_t *s) {
    struct epoll_event events[MAX_EVENTS];
    int n = epoll_wait(s->epfd, events, MAX_EVENTS, 50); /* 50ms tick */
    for (int i = 0; i < n; i++) {
        int fd = events[i].data.fd;
        if (fd == s->listen_fd) {
            handle_accept(s);
        } else {
            client_t *c = server_get_client(s, fd);
            if (!c) continue;
            if (events[i].events & EPOLLIN)  handle_read(s, c);
            if (events[i].events & EPOLLOUT) handle_write(s, c);
            if (events[i].events & (EPOLLHUP | EPOLLERR)) client_free(s, c);
        }
    }
}

/*
 * server_tick: expire speaking state for clients that have gone silent.
 * Called from main loop every epoll cycle. Sends a voice_speaking=false
 * notification to the room when a client stops transmitting audio.
 */
void server_tick(server_t *s) {
    extern void voice_set_speaking(server_t *s, client_t *c, int speaking);
    long now = now_ms();
    for (int i = 0; i < s->client_count; i++) {
        client_t *c = s->clients[i];
        if (c->in_voice && c->speaking) {
            if (now - c->last_audio_ms > VAD_HANGOVER_MS) {
                voice_set_speaking(s, c, 0);
            }
        }
    }
}

void server_broadcast(server_t *s, int channel_id, const char *msg, int sender_fd) {
    for (int i = 0; i < s->client_count; i++) {
        client_t *c = s->clients[i];
        if (c->type == CONN_WEBSOCKET && c->channel_id == channel_id && c->fd != sender_fd) {
            ws_send_text(c, msg, strlen(msg));
        }
    }
}

void server_broadcast_binary(server_t *s, int voice_channel_id,
                             const unsigned char *data, int len, int sender_fd) {
    for (int i = 0; i < s->client_count; i++) {
        client_t *c = s->clients[i];
        if (c->type == CONN_WEBSOCKET &&
            c->in_voice &&
            c->voice_channel_id == voice_channel_id &&
            c->fd != sender_fd &&
            !c->muted) {
            ws_send_binary(c, data, len);
        }
    }
}

void server_destroy(server_t *s) {
    for (int i = 0; i < s->client_count; i++) {
        close(s->clients[i]->fd);
        free(s->clients[i]);
    }
    close(s->listen_fd);
    close(s->epfd);
}
