#include "vcs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int split_fields(char *line, char *fields[], size_t expected)
{
    size_t count = 0U;
    char *start = line;
    char *p;

    for (p = line; ; p++)
    {
        if (*p == '|' || *p == '\0')
        {
            if (count >= expected)
            {
                return 0;
            }
            fields[count++] = start;
            if (*p == '\0')
            {
                break;
            }
            *p = '\0';
            start = p + 1;
        }
    }

    return count == expected;
}

static int users_db_path(char path[VELOCE_PATH_LEN + 1])
{
    return path_join(path, VELOCE_PATH_LEN + 1U, storage_root(), VELOCE_USERS_DB);
}

static int parse_user_line(const char *line, UserRecord *user)
{
    char scratch[2048];
    char *fields[9];

    if (line == NULL || user == NULL)
    {
        return 0;
    }

    if (snprintf(scratch, sizeof(scratch), "%s", line) >= (int)sizeof(scratch))
    {
        return 0;
    }

    if (!split_fields(scratch, fields, 9U))
    {
        return 0;
    }

    (void)snprintf(user->uid, sizeof(user->uid), "%s", fields[0]);
    (void)snprintf(user->username, sizeof(user->username), "%s", fields[1]);
    (void)snprintf(user->name, sizeof(user->name), "%s", fields[2]);
    (void)snprintf(user->password_salt, sizeof(user->password_salt), "%s", fields[3]);
    (void)snprintf(user->password_hash, sizeof(user->password_hash), "%s", fields[4]);
    (void)snprintf(user->security_question, sizeof(user->security_question), "%s", fields[5]);
    (void)snprintf(user->answer_salt, sizeof(user->answer_salt), "%s", fields[6]);
    (void)snprintf(user->answer_hash, sizeof(user->answer_hash), "%s", fields[7]);
    (void)snprintf(user->created_at, sizeof(user->created_at), "%s", fields[8]);

    return 1;
}

static int write_user_line(FILE *fp, const UserRecord *user)
{
    if (fp == NULL || user == NULL)
    {
        return 0;
    }

    return fprintf(fp,
                   "%s|%s|%s|%s|%s|%s|%s|%s|%s\n",
                   user->uid,
                   user->username,
                   user->name,
                   user->password_salt,
                   user->password_hash,
                   user->security_question,
                   user->answer_salt,
                   user->answer_hash,
                   user->created_at) > 0;
}

static int is_valid_username(const char *username)
{
    size_t i;

    if (username == NULL)
    {
        return 0;
    }

    if (strlen(username) < 3U)
    {
        return 0;
    }

    for (i = 0U; username[i] != '\0'; i++)
    {
        if (!(isalnum((unsigned char)username[i]) || username[i] == '_' || username[i] == '-'))
        {
            return 0;
        }
    }

    return 1;
}

static int is_valid_password(const char *password)
{
    return password != NULL && strlen(password) >= 8U;
}

static int find_user_by_username(const char *username, UserRecord *result)
{
    FILE *fp;
    char path[VELOCE_PATH_LEN + 1];
    char line[2048];
    UserRecord current;

    if (users_db_path(path) != 0)
    {
        return 0;
    }

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        return 0;
    }

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (!parse_user_line(line, &current))
        {
            continue;
        }

        if (strcmp(current.username, username) == 0)
        {
            fclose(fp);
            if (result != NULL)
            {
                *result = current;
            }
            return 1;
        }
    }

    fclose(fp);
    return 0;
}

static int append_user(const UserRecord *user)
{
    FILE *fp;
    char path[VELOCE_PATH_LEN + 1];
    int ok;

    if (users_db_path(path) != 0)
    {
        return 0;
    }

    fp = fopen(path, "ab");
    if (fp == NULL)
    {
        return 0;
    }

    ok = write_user_line(fp, user);
    fclose(fp);
    return ok;
}

static int update_user_password(const char *uid, const char *new_salt, const char *new_hash)
{
    char path[VELOCE_PATH_LEN + 1];
    char tmp_path[VELOCE_PATH_LEN + 1];
    FILE *in;
    FILE *out;
    char line[2048];
    int changed = 0;

    if (users_db_path(path) != 0)
    {
        return 0;
    }

    if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path) >= (int)sizeof(tmp_path))
    {
        return 0;
    }

    in = fopen(path, "rb");
    out = fopen(tmp_path, "wb");
    if (in == NULL || out == NULL)
    {
        if (in != NULL)
        {
            fclose(in);
        }
        if (out != NULL)
        {
            fclose(out);
        }
        remove(tmp_path);
        return 0;
    }

    while (fgets(line, sizeof(line), in) != NULL)
    {
        UserRecord user;
        char raw[2048];

        (void)snprintf(raw, sizeof(raw), "%s", line);

        line[strcspn(line, "\r\n")] = '\0';
        if (!parse_user_line(line, &user))
        {
            if (fputs(raw, out) == EOF)
            {
                fclose(in);
                fclose(out);
                remove(tmp_path);
                return 0;
            }
            continue;
        }

        if (strcmp(user.uid, uid) == 0)
        {
            (void)snprintf(user.password_salt, sizeof(user.password_salt), "%s", new_salt);
            (void)snprintf(user.password_hash, sizeof(user.password_hash), "%s", new_hash);
            changed = 1;
        }

        if (!write_user_line(out, &user))
        {
            fclose(in);
            fclose(out);
            remove(tmp_path);
            return 0;
        }
    }

    fclose(in);
    fclose(out);

    if (!changed)
    {
        remove(tmp_path);
        return 0;
    }

    remove(path);
    if (rename(tmp_path, path) != 0)
    {
        remove(tmp_path);
        return 0;
    }

    return 1;
}

static void set_session_from_user(Session *session, const UserRecord *user)
{
    (void)snprintf(session->uid, sizeof(session->uid), "%s", user->uid);
    (void)snprintf(session->username, sizeof(session->username), "%s", user->username);
    (void)snprintf(session->name, sizeof(session->name), "%s", user->name);
}

static int reset_password_flow(void)
{
    char username[VELOCE_USERNAME_LEN + 1];
    char answer[VELOCE_ANSWER_LEN + 1];
    char answer_hash[VELOCE_HASH_HEX_LEN];
    char new_password[VELOCE_PASSWORD_LEN + 1];
    char confirm_password[VELOCE_PASSWORD_LEN + 1];
    char password_salt[VELOCE_ID_LEN];
    char password_hash[VELOCE_HASH_HEX_LEN];
    UserRecord user;

    app_clear_screen();
    (void)printf("Reset password\n\n");

    if (!read_line("Username: ", username, sizeof(username)))
    {
        return 0;
    }
    sanitize_field(username);

    if (!find_user_by_username(username, &user))
    {
        (void)printf("No account found for that username.\n");
        app_pause(NULL);
        return 0;
    }

    (void)printf("Security question: %s\n", user.security_question);
    if (!read_line("Answer: ", answer, sizeof(answer)))
    {
        return 0;
    }
    sanitize_field(answer);
    hash_secret(answer, user.answer_salt, answer_hash);

    if (strcmp(answer_hash, user.answer_hash) != 0)
    {
        (void)printf("Verification failed.\n");
        app_pause(NULL);
        return 0;
    }

    if (!read_password("New password (min 8 chars): ", new_password, sizeof(new_password)))
    {
        return 0;
    }
    if (!read_password("Confirm password: ", confirm_password, sizeof(confirm_password)))
    {
        return 0;
    }

    if (strcmp(new_password, confirm_password) != 0)
    {
        (void)printf("Passwords do not match.\n");
        app_pause(NULL);
        return 0;
    }

    if (!is_valid_password(new_password))
    {
        (void)printf("Password must be at least 8 characters.\n");
        app_pause(NULL);
        return 0;
    }

    generate_id(password_salt);
    hash_secret(new_password, password_salt, password_hash);

    if (!update_user_password(user.uid, password_salt, password_hash))
    {
        (void)printf("Failed to update password.\n");
        app_pause(NULL);
        return 0;
    }

    (void)printf("Password updated successfully.\n");
    app_pause(NULL);
    return 1;
}

static int login_flow(Session *session)
{
    char username[VELOCE_USERNAME_LEN + 1];
    char password[VELOCE_PASSWORD_LEN + 1];
    char password_hash[VELOCE_HASH_HEX_LEN];
    UserRecord user;
    int choice;

    while (1)
    {
        app_clear_screen();
        (void)printf("Login\n\n");

        if (!read_line("Username: ", username, sizeof(username)))
        {
            return 0;
        }
        sanitize_field(username);

        if (!read_password("Password: ", password, sizeof(password)))
        {
            return 0;
        }

        if (find_user_by_username(username, &user))
        {
            hash_secret(password, user.password_salt, password_hash);
            if (strcmp(password_hash, user.password_hash) == 0)
            {
                set_session_from_user(session, &user);
                (void)printf("Login successful.\n");
                app_pause(NULL);
                return 1;
            }
        }

        (void)printf("\nCredentials did not match.\n");
        (void)printf("1) Try again\n");
        (void)printf("2) Reset password\n");
        (void)printf("3) Back\n");

        if (!read_int("Choice: ", &choice))
        {
            choice = 1;
        }

        if (choice == 2)
        {
            (void)reset_password_flow();
        }
        else if (choice == 3)
        {
            return 0;
        }
    }
}

static int signup_flow(Session *session)
{
    UserRecord user;
    char password[VELOCE_PASSWORD_LEN + 1];
    char confirm_password[VELOCE_PASSWORD_LEN + 1];

    app_clear_screen();
    (void)printf("Create account\n\n");

    while (1)
    {
        if (!read_line("Username (letters/numbers/_/-): ", user.username, sizeof(user.username)))
        {
            return 0;
        }
        sanitize_field(user.username);

        if (!is_valid_username(user.username))
        {
            (void)printf("Username must be at least 3 characters and only use letters, numbers, '_' or '-'.\n");
            continue;
        }

        if (find_user_by_username(user.username, NULL))
        {
            (void)printf("That username is already in use.\n");
            continue;
        }

        break;
    }

    while (1)
    {
        if (!read_password("Password (min 8 chars): ", password, sizeof(password)))
        {
            return 0;
        }
        if (!read_password("Confirm password: ", confirm_password, sizeof(confirm_password)))
        {
            return 0;
        }

        if (strcmp(password, confirm_password) != 0)
        {
            (void)printf("Passwords do not match. Try again.\n");
            continue;
        }

        if (!is_valid_password(password))
        {
            (void)printf("Password must be at least 8 characters.\n");
            continue;
        }

        break;
    }

    if (!read_line("Full name: ", user.name, sizeof(user.name)))
    {
        return 0;
    }
    sanitize_field(user.name);

    if (!read_line("Security question: ", user.security_question, sizeof(user.security_question)))
    {
        return 0;
    }
    sanitize_field(user.security_question);

    {
        char answer[VELOCE_ANSWER_LEN + 1];

        if (!read_line("Security answer: ", answer, sizeof(answer)))
        {
            return 0;
        }
        sanitize_field(answer);

        generate_id(user.answer_salt);
        hash_secret(answer, user.answer_salt, user.answer_hash);
    }

    generate_id(user.uid);
    generate_id(user.password_salt);
    hash_secret(password, user.password_salt, user.password_hash);
    now_timestamp(user.created_at);

    if (!append_user(&user))
    {
        (void)printf("Failed to create account.\n");
        app_pause(NULL);
        return 0;
    }

    set_session_from_user(session, &user);
    (void)printf("Account created successfully.\n");
    app_pause(NULL);
    return 1;
}

int verify_auth(Session *session)
{
    int choice;

    while (1)
    {
        app_clear_screen();
        (void)printf("Veloce\n");
        (void)printf("1) Login\n");
        (void)printf("2) Signup\n");
        (void)printf("3) Exit\n");

        if (!read_int("Choice: ", &choice))
        {
            (void)printf("Please enter a valid number.\n");
            app_pause(NULL);
            continue;
        }

        if (choice == 1)
        {
            if (login_flow(session))
            {
                return 1;
            }
        }
        else if (choice == 2)
        {
            if (signup_flow(session))
            {
                return 1;
            }
        }
        else if (choice == 3)
        {
            return 0;
        }
        else
        {
            (void)printf("Please choose 1, 2, or 3.\n");
            app_pause(NULL);
        }
    }
}
