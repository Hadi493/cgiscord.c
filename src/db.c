#include "../include/db.h"
#include "../include/auth.h"
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static sqlite3 *g_db = NULL;

int db_init(const char *path) {
    if (sqlite3_open(path, &g_db) != SQLITE_OK) {
        fprintf(stderr, "db: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }

    const char *schema =
        "PRAGMA journal_mode=WAL;"
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  username TEXT UNIQUE NOT NULL,"
        "  password_hash TEXT NOT NULL,"
        "  created_at INTEGER DEFAULT (strftime('%s','now'))"
        ");"
        "CREATE TABLE IF NOT EXISTS channels ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT UNIQUE NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  channel_id INTEGER NOT NULL,"
        "  user_id INTEGER NOT NULL,"
        "  content TEXT NOT NULL,"
        "  created_at INTEGER DEFAULT (strftime('%s','now')),"
        "  FOREIGN KEY(channel_id) REFERENCES channels(id),"
        "  FOREIGN KEY(user_id) REFERENCES users(id)"
        ");";

    char *err = NULL;
    if (sqlite3_exec(g_db, schema, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "db schema: %s\n", err);
        sqlite3_free(err);
        return -1;
    }

    /* create default channels */
    sqlite3_exec(g_db,
        "INSERT OR IGNORE INTO channels(name) VALUES('general');"
        "INSERT OR IGNORE INTO channels(name) VALUES('random');"
        "INSERT OR IGNORE INTO channels(name) VALUES('dev');",
        NULL, NULL, NULL);

    return 0;
}

void db_close(void) {
    if (g_db) sqlite3_close(g_db);
    g_db = NULL;
}

int db_create_user(const char *username, const char *password) {
    char hash[128];
    auth_hash_password(password, hash, sizeof(hash));

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(g_db,
        "INSERT INTO users(username, password_hash) VALUES(?,?)", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hash, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_auth_user(const char *username, const char *password, int *user_id_out) {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(g_db,
        "SELECT id, password_hash FROM users WHERE username=?", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }

    int uid = sqlite3_column_int(stmt, 0);
    const char *stored_hash = (const char*)sqlite3_column_text(stmt, 1);

    if (auth_check_password(password, stored_hash) != 0) {
        sqlite3_finalize(stmt);
        return -1;
    }

    *user_id_out = uid;
    sqlite3_finalize(stmt);
    return 0;
}

int db_get_username(int user_id, char *out, int out_len) {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(g_db, "SELECT username FROM users WHERE id=?", -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, user_id);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        strncpy(out, (const char*)sqlite3_column_text(stmt, 0), out_len-1);
        out[out_len-1] = '\0';
    }
    sqlite3_finalize(stmt);
    return (rc == SQLITE_ROW) ? 0 : -1;
}

int db_get_channels(db_channel_t *out, int max, int *count_out) {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(g_db, "SELECT id, name FROM channels", -1, &stmt, NULL);
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max) {
        out[count].id = sqlite3_column_int(stmt, 0);
        strncpy(out[count].name, (const char*)sqlite3_column_text(stmt, 1),
                MAX_CHANNEL-1);
        count++;
    }
    *count_out = count;
    sqlite3_finalize(stmt);
    return 0;
}

int db_create_channel(const char *name, int *id_out) {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(g_db, "INSERT INTO channels(name) VALUES(?)", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) {
        *id_out = (int)sqlite3_last_insert_rowid(g_db);
        return 0;
    }
    return -1;
}

int db_add_message(int channel_id, int user_id, const char *content) {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(g_db,
        "INSERT INTO messages(channel_id, user_id, content) VALUES(?,?,?)",
        -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, channel_id);
    sqlite3_bind_int(stmt, 2, user_id);
    sqlite3_bind_text(stmt, 3, content, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_get_messages(int channel_id, db_message_t *out, int max, int *count_out) {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(g_db,
        "SELECT m.id, m.channel_id, m.user_id, u.username, m.content, m.created_at "
        "FROM messages m JOIN users u ON m.user_id=u.id "
        "WHERE m.channel_id=? ORDER BY m.id DESC LIMIT ?",
        -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, channel_id);
    sqlite3_bind_int(stmt, 2, max);

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max) {
        out[count].id         = sqlite3_column_int(stmt, 0);
        out[count].channel_id = sqlite3_column_int(stmt, 1);
        out[count].user_id    = sqlite3_column_int(stmt, 2);
        strncpy(out[count].username, (const char*)sqlite3_column_text(stmt, 3), MAX_USERNAME-1);
        strncpy(out[count].content,  (const char*)sqlite3_column_text(stmt, 4), MAX_MESSAGE-1);
        out[count].created_at = (time_t)sqlite3_column_int64(stmt, 5);
        count++;
    }

    /* reverse so oldest first */
    for (int i = 0, j = count-1; i < j; i++, j--) {
        db_message_t tmp = out[i]; out[i] = out[j]; out[j] = tmp;
    }

    *count_out = count;
    sqlite3_finalize(stmt);
    return 0;
}
