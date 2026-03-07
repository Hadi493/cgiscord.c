#pragma once
#include "server.h"

typedef struct {
    char method[16];
    char path[256];
    char body[MAX_MSG_SIZE];
    int  body_len;
    char token[512];
    int  is_ws_upgrade;
    char ws_key[128];
} http_request_t;

int  http_parse(const char *raw, int len, http_request_t *req);
void http_handle(server_t *s, client_t *c, http_request_t *req);
void http_send(client_t *c, int status, const char *content_type, const char *body, int body_len);
void http_send_json(client_t *c, int status, const char *json);
