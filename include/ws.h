#pragma once
#include "server.h"

#define WS_OPCODE_TEXT   0x1
#define WS_OPCODE_BINARY 0x2
#define WS_OPCODE_CLOSE  0x8
#define WS_OPCODE_PING   0x9
#define WS_OPCODE_PONG   0xA

int  ws_handshake(client_t *c);
int  ws_read_frame(client_t *c, char *out, int *out_len, int *opcode_out);
int  ws_send_text(client_t *c, const char *msg, int len);
int  ws_send_binary(client_t *c, const unsigned char *data, int len);
void ws_close(client_t *c);
