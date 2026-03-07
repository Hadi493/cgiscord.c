#pragma once
#include <sys/epoll.h>
#include <stdint.h>
#include <time.h>

#define SERVER_PORT     7000
#define MAX_CLIENTS     64
#define MAX_EVENTS      64
#define BUFFER_SIZE     65536
#define MAX_MSG_SIZE    8192

/* VAD: silence hangover before marking user as stopped speaking (ms) */
#define VAD_HANGOVER_MS  400

typedef enum {
    CONN_HTTP,
    CONN_WEBSOCKET
} conn_type_t;

typedef struct client {
    int fd;
    conn_type_t type;
    char read_buf[BUFFER_SIZE];
    int  read_len;
    char write_buf[BUFFER_SIZE];
    int  write_len;

    /* ws upgrade */
    char ws_key_buf[128];

    /* ws auth */
    int  authenticated;
    int  user_id;
    char username[64];
    int  channel_id;

    /* voice */
    int  in_voice;
    int  voice_channel_id;
    int  muted;
    int  speaking;          /* 1 = VAD currently active on this client */
    long last_audio_ms;     /* timestamp of last received audio frame  */
} client_t;

typedef struct {
    int        epfd;
    int        listen_fd;
    client_t  *clients[MAX_CLIENTS];
    int        client_count;
} server_t;

int  server_init(server_t *s);
void server_poll(server_t *s);
void server_tick(server_t *s);   /* call periodically to expire VAD speaking state */
void server_destroy(server_t *s);
void server_broadcast(server_t *s, int channel_id, const char *msg, int sender_fd);
void server_broadcast_binary(server_t *s, int voice_channel_id,
                             const unsigned char *data, int len, int sender_fd);
client_t *server_get_client(server_t *s, int fd);
long now_ms(void);
