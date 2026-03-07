#pragma once
#include <time.h>

#define MAX_USERNAME  64
#define MAX_PASSWORD  128
#define MAX_CHANNEL   64
#define MAX_MESSAGE   2048

typedef struct {
    int  id;
    char username[MAX_USERNAME];
    char password_hash[MAX_PASSWORD];
} db_user_t;

typedef struct {
    int  id;
    char name[MAX_CHANNEL];
} db_channel_t;

typedef struct {
    int    id;
    int    channel_id;
    int    user_id;
    char   username[MAX_USERNAME];
    char   content[MAX_MESSAGE];
    time_t created_at;
} db_message_t;

int db_init(const char *path);
void db_close(void);

/* users */
int db_create_user(const char *username, const char *password);
int db_auth_user(const char *username, const char *password, int *user_id_out);
int db_get_username(int user_id, char *out, int out_len);

/* channels */
int db_get_channels(db_channel_t *out, int max, int *count_out);
int db_create_channel(const char *name, int *id_out);

/* messages */
int db_add_message(int channel_id, int user_id, const char *content);
int db_get_messages(int channel_id, db_message_t *out, int max, int *count_out);
