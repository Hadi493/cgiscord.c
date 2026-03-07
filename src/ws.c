#include "../include/ws.h"
#include "../include/server.h"
#include "../include/db.h"
#include "../include/auth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

#define WS_MAGIC "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* ---------- base64 ---------- */
static int base64_encode(const unsigned char *in, int in_len, char *out, int out_size) {
    BIO *bmem = BIO_new(BIO_s_mem());
    BIO *b64  = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_push(b64, bmem);
    BIO_write(b64, in, in_len);
    BIO_flush(b64);
    BUF_MEM *bptr;
    BIO_get_mem_ptr(b64, &bptr);
    int len = (int)bptr->length;
    if (len >= out_size) len = out_size - 1;
    memcpy(out, bptr->data, len);
    out[len] = '\0';
    BIO_free_all(b64);
    return len;
}

/* ---------- handshake ---------- */
int ws_handshake(client_t *c) {
    /* compute accept key: SHA1(key + magic) -> base64 */
    char combined[256];
    snprintf(combined, sizeof(combined), "%s%s", c->ws_key_buf, WS_MAGIC);

    unsigned char sha1[20];
    SHA1((unsigned char*)combined, strlen(combined), sha1);

    char accept[64];
    base64_encode(sha1, 20, accept, sizeof(accept));

    char response[512];
    int len = snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        accept);

    write(c->fd, response, len);
    return 0;
}

/* ---------- frame reader ---------- */
int ws_read_frame(client_t *c, char *out, int *out_len, int *opcode_out) {
    unsigned char buf[BUFFER_SIZE];
    int n = read(c->fd, buf, sizeof(buf));
    if (n <= 0) return -1;
    if (n < 2) return 0;

    int fin    = (buf[0] >> 7) & 1;
    int opcode = buf[0] & 0x0F;
    int masked = (buf[1] >> 7) & 1;
    uint64_t payload_len = buf[1] & 0x7F;

    (void)fin;
    if (opcode_out) *opcode_out = opcode;

    int offset = 2;
    if (payload_len == 126) {
        if (n < 4) return 0;
        payload_len = ((uint64_t)buf[2] << 8) | buf[3];
        offset = 4;
    } else if (payload_len == 127) {
        if (n < 10) return 0;
        payload_len = 0;
        for (int i = 0; i < 8; i++)
            payload_len = (payload_len << 8) | buf[2+i];
        offset = 10;
    }

    if (opcode == WS_OPCODE_CLOSE) return -1;
    if (opcode == WS_OPCODE_PING) {
        unsigned char pong[2] = {0x8A, 0x00};
        write(c->fd, pong, 2);
        return 0;
    }
    /* accept text and binary */
    if (opcode != WS_OPCODE_TEXT && opcode != WS_OPCODE_BINARY) return 0;

    unsigned char mask[4] = {0};
    if (masked) {
        if (n < offset + 4) return 0;
        memcpy(mask, buf + offset, 4);
        offset += 4;
    }

    if (payload_len > (uint64_t)(n - offset)) payload_len = n - offset;
    /* for binary (audio) allow larger frames */
    int max_len = (opcode == WS_OPCODE_BINARY) ? BUFFER_SIZE - 1 : MAX_MSG_SIZE - 1;
    if (payload_len >= (uint64_t)max_len) payload_len = (uint64_t)max_len;

    for (uint64_t i = 0; i < payload_len; i++) {
        out[i] = masked ? (buf[offset + i] ^ mask[i % 4]) : buf[offset + i];
    }
    out[payload_len] = '\0';
    *out_len = (int)payload_len;
    return 1;
}

/* ---------- frame sender ---------- */
int ws_send_text(client_t *c, const char *msg, int len) {
    unsigned char frame[MAX_MSG_SIZE + 10];
    int offset = 0;

    frame[offset++] = 0x81; /* FIN + text opcode */

    if (len < 126) {
        frame[offset++] = (unsigned char)len;
    } else if (len < 65536) {
        frame[offset++] = 126;
        frame[offset++] = (len >> 8) & 0xFF;
        frame[offset++] = len & 0xFF;
    } else {
        frame[offset++] = 127;
        for (int i = 7; i >= 0; i--)
            frame[offset++] = (len >> (i*8)) & 0xFF;
    }

    memcpy(frame + offset, msg, len);
    offset += len;
    write(c->fd, frame, offset);
    return 0;
}

void ws_close(client_t *c) {
    unsigned char frame[2] = {0x88, 0x00};
    write(c->fd, frame, 2);
}

/* ---------- binary frame sender (for audio) ---------- */
int ws_send_binary(client_t *c, const unsigned char *data, int len) {
    /* header: up to 10 bytes + payload */
    unsigned char header[10];
    int hlen = 0;
    header[hlen++] = 0x82; /* FIN + binary opcode */

    if (len < 126) {
        header[hlen++] = (unsigned char)len;
    } else if (len < 65536) {
        header[hlen++] = 126;
        header[hlen++] = (len >> 8) & 0xFF;
        header[hlen++] = len & 0xFF;
    } else {
        header[hlen++] = 127;
        for (int i = 7; i >= 0; i--)
            header[hlen++] = (len >> (i*8)) & 0xFF;
    }

    /* two writes — header then payload, avoid large copy */
    write(c->fd, header, hlen);
    write(c->fd, data, len);
    return 0;
}

/* ---------- JSON helpers ---------- */
static void json_escape(const char *in, char *out, int out_size) {
    int j = 0;
    for (int i = 0; in[i] && j < out_size - 2; i++) {
        if (in[i] == '"')       { out[j++] = '\\'; out[j++] = '"'; }
        else if (in[i] == '\\') { out[j++] = '\\'; out[j++] = '\\'; }
        else if (in[i] == '\n') { out[j++] = '\\'; out[j++] = 'n'; }
        else if (in[i] == '\r') { out[j++] = '\\'; out[j++] = 'r'; }
        else out[j++] = in[i];
    }
    out[j] = '\0';
}

static char *json_get_string(const char *json, const char *key, char *out, int out_len) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return NULL;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return NULL;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < out_len - 1) {
        if (*p == '\\') p++;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return out;
}

/* ---------- message handler ---------- */
void ws_handle_message(server_t *s, client_t *c, const char *msg, int len) {
    (void)len;

    char type[32] = {0};
    json_get_string(msg, "type", type, sizeof(type));

    /* AUTH */
    if (strcmp(type, "auth") == 0) {
        char token[512] = {0};
        json_get_string(msg, "token", token, sizeof(token));

        int uid = 0;
        char uname[64] = {0};
        if (auth_verify_token(token, &uid, uname, sizeof(uname)) == 0) {
            c->authenticated = 1;
            c->user_id = uid;
            strncpy(c->username, uname, sizeof(c->username)-1);

            /* send channels list */
            db_channel_t channels[32];
            int count = 0;
            db_get_channels(channels, 32, &count);

            char resp[4096];
            int pos = snprintf(resp, sizeof(resp), "{\"type\":\"channels\",\"channels\":[");
            for (int i = 0; i < count; i++) {
                pos += snprintf(resp+pos, sizeof(resp)-pos,
                    "%s{\"id\":%d,\"name\":\"%s\"}",
                    i ? "," : "", channels[i].id, channels[i].name);
            }
            snprintf(resp+pos, sizeof(resp)-pos, "]}");
            ws_send_text(c, resp, strlen(resp));

            /* announce join */
            char announce[256];
            snprintf(announce, sizeof(announce),
                "{\"type\":\"system\",\"text\":\"%s joined the server\"}", uname);
            server_broadcast(s, c->channel_id, announce, c->fd);
            ws_send_text(c, announce, strlen(announce));

        } else {
            ws_send_text(c, "{\"type\":\"error\",\"text\":\"invalid token\"}", 38);
        }
        return;
    }

    if (!c->authenticated) {
        ws_send_text(c, "{\"type\":\"error\",\"text\":\"not authenticated\"}", 41);
        return;
    }

    /* JOIN CHANNEL */
    if (strcmp(type, "join") == 0) {
        char ch_id_str[16] = {0};
        json_get_string(msg, "channel_id", ch_id_str, sizeof(ch_id_str));
        int ch_id = atoi(ch_id_str);
        if (ch_id > 0) {
            c->channel_id = ch_id;

            /* send last 50 messages */
            db_message_t msgs[50];
            int count = 0;
            db_get_messages(ch_id, msgs, 50, &count);

            char resp[MAX_MSG_SIZE * 4];
            int pos = snprintf(resp, sizeof(resp), "{\"type\":\"history\",\"messages\":[");
            for (int i = 0; i < count; i++) {
                char econtent[MAX_MESSAGE*2];
                char euser[MAX_USERNAME*2];
                json_escape(msgs[i].content, econtent, sizeof(econtent));
                json_escape(msgs[i].username, euser, sizeof(euser));
                pos += snprintf(resp+pos, sizeof(resp)-pos,
                    "%s{\"id\":%d,\"user\":\"%s\",\"text\":\"%s\",\"ts\":%ld}",
                    i ? "," : "", msgs[i].id, euser, econtent, (long)msgs[i].created_at);
            }
            snprintf(resp+pos, sizeof(resp)-pos, "]}");
            ws_send_text(c, resp, strlen(resp));
        }
        return;
    }

    /* SEND MESSAGE */
    if (strcmp(type, "message") == 0) {
        char text[MAX_MESSAGE] = {0};
        json_get_string(msg, "text", text, sizeof(text));
        if (strlen(text) == 0) return;

        db_add_message(c->channel_id, c->user_id, text);

        char etext[MAX_MESSAGE*2], euser[MAX_USERNAME*2];
        json_escape(text, etext, sizeof(etext));
        json_escape(c->username, euser, sizeof(euser));

        char broadcast[MAX_MSG_SIZE];
        snprintf(broadcast, sizeof(broadcast),
            "{\"type\":\"message\",\"user\":\"%s\",\"text\":\"%s\",\"ts\":%ld}",
            euser, etext, (long)time(NULL));

        /* send to everyone in channel incl sender */
        ws_send_text(c, broadcast, strlen(broadcast));
        server_broadcast(s, c->channel_id, broadcast, c->fd);
        return;
    }

    /* TYPING indicator */
    if (strcmp(type, "typing") == 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"type\":\"typing\",\"user\":\"%s\"}", c->username);
        server_broadcast(s, c->channel_id, buf, c->fd);
        return;
    }

    /* VOICE JOIN */
    if (strcmp(type, "voice_join") == 0) {
        char ch_id_str[16] = {0};
        json_get_string(msg, "channel_id", ch_id_str, sizeof(ch_id_str));
        int ch_id = atoi(ch_id_str);
        if (ch_id > 0) {
            extern void voice_join(server_t *s, client_t *c, int voice_ch_id);
            voice_join(s, c, ch_id);
        }
        return;
    }

    /* VOICE LEAVE */
    if (strcmp(type, "voice_leave") == 0) {
        extern void voice_leave(server_t *s, client_t *c);
        voice_leave(s, c);
        return;
    }

    /* VOICE MUTE TOGGLE */
    if (strcmp(type, "voice_mute") == 0) {
        extern void voice_mute_toggle(server_t *s, client_t *c);
        voice_mute_toggle(s, c);
        return;
    }
}
