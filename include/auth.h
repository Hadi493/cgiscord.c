#pragma once

/* simple token: base64(user_id:username:timestamp:hmac) */
int  auth_generate_token(int user_id, const char *username, char *out, int out_len);
int  auth_verify_token(const char *token, int *user_id_out, char *username_out, int username_len);
void auth_hash_password(const char *password, char *out, int out_len);
int  auth_check_password(const char *password, const char *hash);
