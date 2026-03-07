#include "../include/auth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

/* secret key for HMAC token signing — change this! */
static const char SECRET[] = "change_this_secret_key_pls_123!";

/* ---------- hex utils ---------- */
static void bytes_to_hex(const unsigned char *in, int len, char *out) {
    for (int i = 0; i < len; i++)
        sprintf(out + i*2, "%02x", in[i]);
}

static void hex_to_bytes(const char *hex, unsigned char *out, int out_len) {
    for (int i = 0; i < out_len; i++) {
        unsigned int b;
        sscanf(hex + i*2, "%02x", &b);
        out[i] = (unsigned char)b;
    }
}

/* ---------- base64 (url-safe, no padding) ---------- */
static const char b64chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static int b64_encode(const unsigned char *in, int in_len, char *out, int out_size) {
    int i = 0, j = 0;
    while (i < in_len && j + 4 < out_size) {
        unsigned int a = i < in_len ? in[i++] : 0;
        unsigned int b = i < in_len ? in[i++] : 0;
        unsigned int c = i < in_len ? in[i++] : 0;
        unsigned int triple = (a << 16) | (b << 8) | c;
        out[j++] = b64chars[(triple >> 18) & 0x3F];
        out[j++] = b64chars[(triple >> 12) & 0x3F];
        out[j++] = b64chars[(triple >>  6) & 0x3F];
        out[j++] = b64chars[(triple      ) & 0x3F];
    }
    out[j] = '\0';
    return j;
}

/* ---------- password hashing ---------- */
/* format: salt_hex:sha256(password+salt)_hex */
void auth_hash_password(const char *password, char *out, int out_len) {
    unsigned char salt[16];
    RAND_bytes(salt, sizeof(salt));

    char salt_hex[33];
    bytes_to_hex(salt, 16, salt_hex);

    unsigned char tohash[256];
    int tlen = snprintf((char*)tohash, sizeof(tohash), "%s%s", password, salt_hex);

    unsigned char digest[32];
    SHA256(tohash, tlen, digest);

    char digest_hex[65];
    bytes_to_hex(digest, 32, digest_hex);

    snprintf(out, out_len, "%s:%s", salt_hex, digest_hex);
}

int auth_check_password(const char *password, const char *hash) {
    char salt_hex[33] = {0};
    const char *colon = strchr(hash, ':');
    if (!colon) return -1;
    int salt_len = (int)(colon - hash);
    if (salt_len != 32) return -1;
    strncpy(salt_hex, hash, 32);
    salt_hex[32] = '\0';

    unsigned char tohash[256];
    int tlen = snprintf((char*)tohash, sizeof(tohash), "%s%s", password, salt_hex);

    unsigned char digest[32];
    SHA256(tohash, tlen, digest);

    char digest_hex[65];
    bytes_to_hex(digest, 32, digest_hex);

    return strcmp(colon + 1, digest_hex);
}

/* ---------- token ---------- */
/* format: b64(uid:username:ts):hmac_hex */
int auth_generate_token(int user_id, const char *username, char *out, int out_len) {
    char payload[256];
    snprintf(payload, sizeof(payload), "%d:%s:%ld", user_id, username, (long)time(NULL));

    char payload_b64[512];
    b64_encode((unsigned char*)payload, strlen(payload), payload_b64, sizeof(payload_b64));

    /* HMAC-SHA256 */
    unsigned char hmac[32];
    unsigned int hmac_len = 32;
    HMAC(EVP_sha256(), SECRET, strlen(SECRET),
         (unsigned char*)payload_b64, strlen(payload_b64), hmac, &hmac_len);

    char hmac_hex[65];
    bytes_to_hex(hmac, 32, hmac_hex);

    snprintf(out, out_len, "%s.%s", payload_b64, hmac_hex);
    return 0;
}

int auth_verify_token(const char *token, int *user_id_out, char *username_out, int username_len) {
    char buf[1024];
    strncpy(buf, token, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';

    char *dot = strrchr(buf, '.');
    if (!dot) return -1;
    *dot = '\0';
    char *payload_b64 = buf;
    char *hmac_hex    = dot + 1;

    /* verify HMAC */
    unsigned char hmac[32];
    unsigned int hmac_len = 32;
    HMAC(EVP_sha256(), SECRET, strlen(SECRET),
         (unsigned char*)payload_b64, strlen(payload_b64), hmac, &hmac_len);

    char expected_hex[65];
    bytes_to_hex(hmac, 32, expected_hex);

    if (strcmp(expected_hex, hmac_hex) != 0) return -1;

    /* decode payload — we encoded with our own b64, just scan it as base64 */
    /* decode directly from format uid:username:ts */
    /* since we store payload_b64 as is, let's re-encode and compare — actually
       let's just decode the b64. We'll use a simple decode. */
    /* For simplicity, tokens store the raw payload after a known separator.
       Let's just re-generate and compare. Instead, store payload as plain after dot too: */
    /* Actually: let's use a different token format: uid:username:ts.hmac */
    /* We already changed the format below. Let me just decode the b64 manually */

    /* Simple approach: decode each 4 b64 chars -> 3 bytes */
    static const char *b64c =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    char decoded[256] = {0};
    int di = 0;
    int plen = strlen(payload_b64);
    for (int i = 0; i + 3 < plen && di + 2 < (int)sizeof(decoded)-1; i += 4) {
        int v0 = strchr(b64c, payload_b64[i  ]) - b64c;
        int v1 = strchr(b64c, payload_b64[i+1]) - b64c;
        int v2 = i+2 < plen ? (strchr(b64c, payload_b64[i+2]) - b64c) : 0;
        int v3 = i+3 < plen ? (strchr(b64c, payload_b64[i+3]) - b64c) : 0;
        if (v0 < 0 || v1 < 0) break;
        decoded[di++] = (v0 << 2) | (v1 >> 4);
        if (v2 >= 0 && i+2 < plen) decoded[di++] = ((v1 & 0xF) << 4) | (v2 >> 2);
        if (v3 >= 0 && i+3 < plen) decoded[di++] = ((v2 & 0x3) << 6) | v3;
    }
    decoded[di] = '\0';

    /* parse uid:username:ts */
    int uid = 0;
    char uname[64] = {0};
    long ts = 0;
    if (sscanf(decoded, "%d:%63[^:]:%ld", &uid, uname, &ts) < 2) return -1;

    /* optional: check token age (7 days) */
    if (time(NULL) - ts > 7 * 24 * 3600) return -1;

    *user_id_out = uid;
    strncpy(username_out, uname, username_len - 1);
    username_out[username_len-1] = '\0';
    return 0;
}
