#include "../include/http.h"
#include "../include/server.h"
#include "../include/db.h"
#include "../include/auth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---------- parser ---------- */
int http_parse(const char *raw, int len, http_request_t *req) {
    memset(req, 0, sizeof(*req));

    /* method */
    const char *p = raw;
    const char *sp = strchr(p, ' ');
    if (!sp) return -1;
    int ml = (int)(sp - p);
    if (ml >= (int)sizeof(req->method)) return -1;
    strncpy(req->method, p, ml);

    /* path */
    p = sp + 1;
    sp = strchr(p, ' ');
    if (!sp) return -1;
    int pl = (int)(sp - p);
    if (pl >= (int)sizeof(req->path)) pl = sizeof(req->path) - 1;
    strncpy(req->path, p, pl);

    /* WebSocket upgrade — check case-insensitively, Railway proxy lowercases headers */
    int is_ws = 0;
    {
        /* make a lowercase copy of the headers only (up to \r\n\r\n) */
        char lower[1024] = {0};
        int copy_len = (int)sizeof(lower) - 1;
        const char *hdr_end = strstr(raw, "\r\n\r\n");
        if (hdr_end) {
            int hdr_len = (int)(hdr_end - raw);
            if (hdr_len < copy_len) copy_len = hdr_len;
        }
        for (int i = 0; i < copy_len; i++)
            lower[i] = (raw[i] >= 'A' && raw[i] <= 'Z') ? raw[i] + 32 : raw[i];
        if (strstr(lower, "upgrade: websocket")) is_ws = 1;
    }

    if (is_ws) {
        req->is_ws_upgrade = 1;
        /* find Sec-WebSocket-Key — try both capitalizations */
        const char *key = strstr(raw, "Sec-WebSocket-Key:");
        if (!key) key = strstr(raw, "sec-websocket-key:");
        if (key) {
            key += 18;
            while (*key == ' ') key++;
            int kl = 0;
            while (key[kl] && key[kl] != '\r' && key[kl] != '\n') kl++;
            if (kl < (int)sizeof(req->ws_key)) {
                strncpy(req->ws_key, key, kl);
                req->ws_key[kl] = '\0';
            }
        }
        return 0;
    }

    /* Authorization header */
    const char *auth = strstr(raw, "Authorization: Bearer ");
    if (auth) {
        auth += 22;
        int al = 0;
        while (auth[al] && auth[al] != '\r' && auth[al] != '\n') al++;
        if (al < (int)sizeof(req->token)) {
            strncpy(req->token, auth, al);
            req->token[al] = '\0';
        }
    }

    /* Content-Length — read exact body bytes */
    const char *cl_hdr = strstr(raw, "Content-Length:");
    int content_length = 0;
    if (cl_hdr) {
        cl_hdr += 15;
        while (*cl_hdr == ' ') cl_hdr++;
        content_length = atoi(cl_hdr);
    }

    /* locate header/body boundary */
    const char *body_start = strstr(raw, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        int available = len - (int)(body_start - raw);

        /* if Content-Length says there's more data than we have, signal incomplete */
        if (content_length > 0 && available < content_length)
            return 1; /* caller should buffer more */

        req->body_len = content_length > 0 ? content_length : available;
        if (req->body_len >= MAX_MSG_SIZE) req->body_len = MAX_MSG_SIZE - 1;
        if (req->body_len > 0) {
            memcpy(req->body, body_start, req->body_len);
            req->body[req->body_len] = '\0';
        }
    }

    return 0;
}

/* ---------- response helpers ---------- */
void http_send(client_t *c, int status, const char *content_type, const char *body, int body_len) {
    const char *status_text = "OK";
    if (status == 201) status_text = "Created";
    else if (status == 400) status_text = "Bad Request";
    else if (status == 401) status_text = "Unauthorized";
    else if (status == 404) status_text = "Not Found";
    else if (status == 409) status_text = "Conflict";

    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Headers: Authorization, Content-Type\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_text, content_type, body_len);

    write(c->fd, header, hlen);
    if (body_len > 0) write(c->fd, body, body_len);
}

void http_send_json(client_t *c, int status, const char *json) {
    http_send(c, status, "application/json", json, strlen(json));
}

/* ---------- JSON string extractor ---------- */
/*
 * Finds "key":"value" in a JSON string and copies value into out.
 * Correctly handles whitespace around the colon.
 * Returns 1 on success, 0 if key not found or value malformed.
 */
static int json_str(const char *json, const char *key, char *out, int out_len) {
    /* search for  "key"  */
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);

    /* skip whitespace then colon */
    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return 0;
    p++;

    /* skip whitespace then opening quote */
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return 0;
    p++; /* now pointing at first char of value */

    /* copy until closing quote, handling backslash escapes */
    int i = 0;
    while (*p && *p != '"' && i < out_len - 1) {
        if (*p == '\\') {
            p++;
            if (!*p) break;
            switch (*p) {
                case '"':  out[i++] = '"';  break;
                case '\\': out[i++] = '\\'; break;
                case 'n':  out[i++] = '\n'; break;
                case 'r':  out[i++] = '\r'; break;
                case 't':  out[i++] = '\t'; break;
                default:   out[i++] = *p;   break;
            }
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    return (i > 0 || *p == '"') ? 1 : 0;
}

/* ---------- route handlers ---------- */
static void route_register(server_t *s, client_t *c, http_request_t *req) {
    (void)s;
    char username[64] = {0}, password[128] = {0};

    if (!json_str(req->body, "username", username, sizeof(username)) ||
        !json_str(req->body, "password", password, sizeof(password))) {
        http_send_json(c, 400, "{\"error\":\"missing fields\"}");
        return;
    }

    if (strlen(username) < 2) {
        http_send_json(c, 400, "{\"error\":\"username too short (min 2)\"}");
        return;
    }
    if (strlen(password) < 4) {
        http_send_json(c, 400, "{\"error\":\"password too short (min 4)\"}");
        return;
    }

    if (db_create_user(username, password) != 0) {
        http_send_json(c, 409, "{\"error\":\"username taken\"}");
        return;
    }

    int uid = 0;
    db_auth_user(username, password, &uid);
    char token[512];
    auth_generate_token(uid, username, token, sizeof(token));

    char resp[600];
    snprintf(resp, sizeof(resp),
        "{\"token\":\"%s\",\"username\":\"%s\",\"id\":%d}",
        token, username, uid);
    http_send_json(c, 201, resp);
}

static void route_login(server_t *s, client_t *c, http_request_t *req) {
    (void)s;
    char username[64] = {0}, password[128] = {0};

    if (!json_str(req->body, "username", username, sizeof(username)) ||
        !json_str(req->body, "password", password, sizeof(password))) {
        http_send_json(c, 400, "{\"error\":\"missing fields\"}");
        return;
    }

    int uid = 0;
    if (db_auth_user(username, password, &uid) != 0) {
        http_send_json(c, 401, "{\"error\":\"invalid credentials\"}");
        return;
    }

    char token[512];
    auth_generate_token(uid, username, token, sizeof(token));

    char resp[600];
    snprintf(resp, sizeof(resp),
        "{\"token\":\"%s\",\"username\":\"%s\",\"id\":%d}",
        token, username, uid);
    http_send_json(c, 200, resp);
}

void http_handle(server_t *s, client_t *c, http_request_t *req) {
    extern const char FRONTEND_HTML[];
    /* OPTIONS preflight */
    if (strcmp(req->method, "OPTIONS") == 0) {
        http_send_json(c, 200, "{}");
        return;
    }

    /* serve frontend */
    if (strcmp(req->path, "/") == 0 || strcmp(req->path, "/index.html") == 0) {
        http_send(c, 200, "text/html", FRONTEND_HTML, strlen(FRONTEND_HTML));
        return;
    }

    /* API routes */
    if (strcmp(req->path, "/api/register") == 0 && strcmp(req->method, "POST") == 0) {
        route_register(s, c, req);
        return;
    }
    if (strcmp(req->path, "/api/login") == 0 && strcmp(req->method, "POST") == 0) {
        route_login(s, c, req);
        return;
    }

    http_send_json(c, 404, "{\"error\":\"not found\"}");
}
