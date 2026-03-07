/*
 * voice.c — Voice channel management for cord
 *
 * Architecture: WebSocket Binary SFU (Selective Forwarding Unit)
 * ---------------------------------------------------------------
 * All audio flows through this server. Clients only need outbound
 * TCP to reach the server's public IP — works through all NATs
 * because every connection is client-initiated.
 *
 * No WebRTC. No DTLS. No ICE. No STUN/TURN.
 * Just WebSocket binary frames over your existing port 7000.
 *
 * VAD (Voice Activity Detection)
 * --------------------------------
 * VAD runs CLIENT-SIDE in the browser (RMS energy + hangover timer).
 * The client only sends binary frames when voice is detected.
 * The SERVER tracks per-client speaking state and:
 *   - Broadcasts voice_speaking=true when first frame arrives after silence
 *   - Broadcasts voice_speaking=false after VAD_HANGOVER_MS of silence
 *   - Relays audio frames to all other members in the same voice channel
 *
 * Binary frame layout:
 *   Client → Server:  [raw Int16 PCM, 16kHz mono, little-endian]
 *   Server → Client:  [4 bytes: sender user_id BE] [raw Int16 PCM]
 *
 * Using 4 bytes for sender_id avoids the ≤256 user limit.
 *
 * Internet / NAT traversal
 * -------------------------
 * Run cord on a VPS with a public IP. Open port 7000 in your firewall.
 * For TLS (wss://) wrap with nginx:
 *
 *   server {
 *       listen 443 ssl;
 *       ssl_certificate     /etc/letsencrypt/live/yourdomain/fullchain.pem;
 *       ssl_certificate_key /etc/letsencrypt/live/yourdomain/privkey.pem;
 *       location / {
 *           proxy_pass http://127.0.0.1:7000;
 *           proxy_http_version 1.1;
 *           proxy_set_header Upgrade $http_upgrade;
 *           proxy_set_header Connection "upgrade";
 *           proxy_read_timeout 86400;
 *       }
 *   }
 *
 * This gives you encrypted chat + voice over wss:// on port 443.
 */

#include "../include/server.h"
#include "../include/ws.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* forward declarations */
void voice_leave(server_t *s, client_t *c);
void voice_set_speaking(server_t *s, client_t *c, int speaking);

/* ------------------------------------------------------------------ */
/* Internal: broadcast speaking state change to a voice channel        */
/* ------------------------------------------------------------------ */
void voice_set_speaking(server_t *s, client_t *c, int speaking) {
    if (c->speaking == speaking) return;
    c->speaking = speaking;

    char msg[256];
    snprintf(msg, sizeof(msg),
        "{\"type\":\"voice_speaking\","
        "\"channel_id\":%d,"
        "\"user_id\":%d,"
        "\"username\":\"%s\","
        "\"speaking\":%s}",
        c->voice_channel_id,
        c->user_id,
        c->username,
        speaking ? "true" : "false");

    /* broadcast to everyone in the voice channel including the speaker */
    for (int i = 0; i < s->client_count; i++) {
        client_t *other = s->clients[i];
        if (other->type == CONN_WEBSOCKET &&
            other->in_voice &&
            other->voice_channel_id == c->voice_channel_id) {
            ws_send_text(other, msg, strlen(msg));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Internal: build + send voice_state JSON to a single client          */
/* ------------------------------------------------------------------ */
static void send_voice_state(server_t *s, client_t *target, int ch_id) {
    char buf[2048];
    int pos = snprintf(buf, sizeof(buf),
        "{\"type\":\"voice_state\",\"channel_id\":%d,\"users\":[", ch_id);

    int first = 1;
    for (int i = 0; i < s->client_count; i++) {
        client_t *c = s->clients[i];
        if (!c->in_voice || c->voice_channel_id != ch_id) continue;
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "%s{\"id\":%d,\"name\":\"%s\",\"muted\":%s,\"speaking\":%s}",
            first ? "" : ",",
            c->user_id, c->username,
            c->muted    ? "true" : "false",
            c->speaking ? "true" : "false");
        first = 0;
    }
    snprintf(buf + pos, sizeof(buf) - pos, "]}");
    ws_send_text(target, buf, strlen(buf));
}

/* ------------------------------------------------------------------ */
/* voice_join                                                           */
/* ------------------------------------------------------------------ */
void voice_join(server_t *s, client_t *c, int voice_ch_id) {
    if (c->in_voice) {
        voice_leave(s, c);
    }

    c->in_voice         = 1;
    c->voice_channel_id = voice_ch_id;
    c->muted            = 0;
    c->speaking         = 0;
    c->last_audio_ms    = now_ms();

    printf("[voice] %s joined channel %d\n", c->username, voice_ch_id);

    /* notify all members (including new joiner) with full state */
    for (int i = 0; i < s->client_count; i++) {
        client_t *other = s->clients[i];
        if (other->type == CONN_WEBSOCKET &&
            other->in_voice &&
            other->voice_channel_id == voice_ch_id) {
            send_voice_state(s, other, voice_ch_id);
        }
    }
}

/* ------------------------------------------------------------------ */
/* voice_leave                                                          */
/* ------------------------------------------------------------------ */
void voice_leave(server_t *s, client_t *c) {
    if (!c->in_voice) return;

    int old_ch = c->voice_channel_id;

    /* clear state before notifying so we're not included in new state */
    c->in_voice         = 0;
    c->voice_channel_id = 0;
    c->speaking         = 0;

    printf("[voice] %s left channel %d\n", c->username, old_ch);

    /* tell the leaving client they're disconnected */
    ws_send_text(c,
        "{\"type\":\"voice_state\",\"channel_id\":0,\"users\":[]}",
        51);

    /* notify remaining members with updated state */
    for (int i = 0; i < s->client_count; i++) {
        client_t *other = s->clients[i];
        if (other->type == CONN_WEBSOCKET &&
            other->in_voice &&
            other->voice_channel_id == old_ch) {
            send_voice_state(s, other, old_ch);
        }
    }
}

/* ------------------------------------------------------------------ */
/* voice_relay: receive audio frame, update VAD state, forward         */
/* ------------------------------------------------------------------ */
void voice_relay(server_t *s, client_t *c,
                 const unsigned char *data, int len) {
    if (!c->in_voice || !c->authenticated || c->muted || len < 2) return;

    /* update VAD timestamp — server_tick() uses this to detect silence */
    c->last_audio_ms = now_ms();

    /* mark as speaking (broadcasts voice_speaking:true if state changed) */
    voice_set_speaking(s, c, 1);

    /*
     * Frame layout sent to receivers:
     *   [4 bytes big-endian: sender user_id][pcm payload]
     *
     * 4-byte sender ID so the JS side can handle any user_id value.
     */
    int frame_len = len + 4;
    unsigned char *frame = malloc(frame_len);
    if (!frame) return;

    uint32_t uid = (uint32_t)c->user_id;
    frame[0] = (uid >> 24) & 0xFF;
    frame[1] = (uid >> 16) & 0xFF;
    frame[2] = (uid >>  8) & 0xFF;
    frame[3] =  uid        & 0xFF;
    memcpy(frame + 4, data, len);

    /* relay to all other authenticated, non-muted members */
    for (int i = 0; i < s->client_count; i++) {
        client_t *other = s->clients[i];
        if (other->type      == CONN_WEBSOCKET &&
            other->in_voice  &&
            other->voice_channel_id == c->voice_channel_id &&
            other->fd        != c->fd &&
            other->authenticated) {
            ws_send_binary(other, frame, frame_len);
        }
    }

    free(frame);
}

/* ------------------------------------------------------------------ */
/* voice_mute_toggle                                                    */
/* ------------------------------------------------------------------ */
void voice_mute_toggle(server_t *s, client_t *c) {
    if (!c->in_voice) return;
    c->muted = !c->muted;

    /* if just muted, clear speaking state */
    if (c->muted) voice_set_speaking(s, c, 0);

    /* send updated full state to all members */
    for (int i = 0; i < s->client_count; i++) {
        client_t *other = s->clients[i];
        if (other->type == CONN_WEBSOCKET &&
            other->in_voice &&
            other->voice_channel_id == c->voice_channel_id) {
            send_voice_state(s, other, c->voice_channel_id);
        }
    }
}
