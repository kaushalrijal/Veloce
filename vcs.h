#ifndef VCS_H
#define VCS_H

#include <stddef.h>

#define VELOCE_ID_LEN 17
#define VELOCE_USERNAME_LEN 31
#define VELOCE_NAME_LEN 63
#define VELOCE_PASSWORD_LEN 127
#define VELOCE_QUESTION_LEN 127
#define VELOCE_ANSWER_LEN 127
#define VELOCE_PATH_LEN 511
#define VELOCE_MSG_LEN 159
#define VELOCE_TIMESTAMP_LEN 20
#define VELOCE_HASH_HEX_LEN 65

#define VELOCE_USERS_DB "users.db"
#define VELOCE_REPOS_DB "repos.db"
#define VELOCE_COMMITS_DB "commits.db"
#define VELOCE_SNAPSHOTS_DIR "snapshots"
#define VELOCE_WORKSPACE_DIR "workspace"

typedef struct
{
    char uid[VELOCE_ID_LEN];
    char username[VELOCE_USERNAME_LEN + 1];
    char name[VELOCE_NAME_LEN + 1];
    char password_salt[VELOCE_ID_LEN];
    char password_hash[VELOCE_HASH_HEX_LEN];
    char security_question[VELOCE_QUESTION_LEN + 1];
    char answer_salt[VELOCE_ID_LEN];
    char answer_hash[VELOCE_HASH_HEX_LEN];
    char created_at[VELOCE_TIMESTAMP_LEN];
} UserRecord;

typedef struct
{
    char id[VELOCE_ID_LEN];
    char owner_uid[VELOCE_ID_LEN];
    int rid;
    char name[VELOCE_NAME_LEN + 1];
    int initialized;
    char tracked_file[VELOCE_PATH_LEN + 1];
    char created_at[VELOCE_TIMESTAMP_LEN];
} RepoRecord;

typedef struct
{
    char id[VELOCE_ID_LEN];
    char repo_id[VELOCE_ID_LEN];
    char timestamp[VELOCE_TIMESTAMP_LEN];
    char message[VELOCE_MSG_LEN + 1];
    char snapshot_path[VELOCE_PATH_LEN + 1];
} CommitRecord;

typedef struct
{
    char uid[VELOCE_ID_LEN];
    char username[VELOCE_USERNAME_LEN + 1];
    char name[VELOCE_NAME_LEN + 1];
} Session;

void load(void);

int verify_auth(Session *session);
int repo(const Session *session, RepoRecord *opened_repo);
void comm(RepoRecord *repo);

int ensure_storage_ready(void);
const char *storage_root(void);

void app_clear_screen(void);
void app_pause(const char *prompt);
void app_sleep_ms(unsigned int ms);
int app_getch(void);

int read_line(const char *prompt, char *buffer, size_t size);
int read_password(const char *prompt, char *buffer, size_t size);
int read_int(const char *prompt, int *value);
void sanitize_field(char *value);
void trim_whitespace(char *value);

void generate_id(char out[VELOCE_ID_LEN]);
void now_timestamp(char out[VELOCE_TIMESTAMP_LEN]);
void hash_secret(const char *secret, const char *salt, char out[VELOCE_HASH_HEX_LEN]);

int path_join(char *out, size_t out_size, const char *left, const char *right);
int ensure_dir(const char *path);
int file_exists(const char *path);
int read_text_file(const char *path, char **content, size_t *len);
int write_text_file(const char *path, const char *content, size_t len);
int copy_text_file(const char *src, const char *dst);

#endif
