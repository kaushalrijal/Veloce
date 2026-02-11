#include "vcs.h"

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

static int repos_db_path(char path[VELOCE_PATH_LEN + 1])
{
    return path_join(path, VELOCE_PATH_LEN + 1U, storage_root(), VELOCE_REPOS_DB);
}

static int parse_repo_line(const char *line, RepoRecord *repo)
{
    char scratch[2048];
    char *fields[7];

    if (line == NULL || repo == NULL)
    {
        return 0;
    }

    if (snprintf(scratch, sizeof(scratch), "%s", line) >= (int)sizeof(scratch))
    {
        return 0;
    }

    if (!split_fields(scratch, fields, 7U))
    {
        return 0;
    }

    (void)snprintf(repo->id, sizeof(repo->id), "%s", fields[0]);
    (void)snprintf(repo->owner_uid, sizeof(repo->owner_uid), "%s", fields[1]);
    repo->rid = atoi(fields[2]);
    (void)snprintf(repo->name, sizeof(repo->name), "%s", fields[3]);
    repo->initialized = atoi(fields[4]);
    (void)snprintf(repo->tracked_file, sizeof(repo->tracked_file), "%s", fields[5]);
    (void)snprintf(repo->created_at, sizeof(repo->created_at), "%s", fields[6]);

    return 1;
}

static int write_repo_line(FILE *fp, const RepoRecord *repo)
{
    if (fp == NULL || repo == NULL)
    {
        return 0;
    }

    return fprintf(fp,
                   "%s|%s|%d|%s|%d|%s|%s\n",
                   repo->id,
                   repo->owner_uid,
                   repo->rid,
                   repo->name,
                   repo->initialized,
                   repo->tracked_file,
                   repo->created_at) > 0;
}

static int append_repo(const RepoRecord *repo)
{
    char path[VELOCE_PATH_LEN + 1];
    FILE *fp;
    int ok;

    if (repos_db_path(path) != 0)
    {
        return 0;
    }

    fp = fopen(path, "ab");
    if (fp == NULL)
    {
        return 0;
    }

    ok = write_repo_line(fp, repo);
    fclose(fp);
    return ok;
}

static int next_repo_id_for_owner(const char *owner_uid)
{
    char path[VELOCE_PATH_LEN + 1];
    FILE *fp;
    char line[2048];
    int max_rid = 0;

    if (repos_db_path(path) != 0)
    {
        return 1;
    }

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        return 1;
    }

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        RepoRecord repo;

        line[strcspn(line, "\r\n")] = '\0';
        if (!parse_repo_line(line, &repo))
        {
            continue;
        }

        if (strcmp(repo.owner_uid, owner_uid) == 0 && repo.rid > max_rid)
        {
            max_rid = repo.rid;
        }
    }

    fclose(fp);
    return max_rid + 1;
}

static void create_repo(const Session *session)
{
    RepoRecord repo;

    app_clear_screen();
    (void)printf("Create repository\n\n");

    if (!read_line("Repository name: ", repo.name, sizeof(repo.name)))
    {
        return;
    }
    sanitize_field(repo.name);

    if (repo.name[0] == '\0')
    {
        (void)printf("Repository name cannot be empty.\n");
        app_pause(NULL);
        return;
    }

    generate_id(repo.id);
    (void)snprintf(repo.owner_uid, sizeof(repo.owner_uid), "%s", session->uid);
    repo.rid = next_repo_id_for_owner(session->uid);
    repo.initialized = 0;
    repo.tracked_file[0] = '\0';
    now_timestamp(repo.created_at);

    if (!append_repo(&repo))
    {
        (void)printf("Failed to create repository.\n");
        app_pause(NULL);
        return;
    }

    (void)printf("Repository created as #%d (%s).\n", repo.rid, repo.name);
    app_pause(NULL);
}

static int load_repo_for_owner(const char *owner_uid, int rid, RepoRecord *result)
{
    char path[VELOCE_PATH_LEN + 1];
    FILE *fp;
    char line[2048];

    if (repos_db_path(path) != 0)
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
        RepoRecord repo;

        line[strcspn(line, "\r\n")] = '\0';
        if (!parse_repo_line(line, &repo))
        {
            continue;
        }

        if (strcmp(repo.owner_uid, owner_uid) == 0 && repo.rid == rid)
        {
            *result = repo;
            fclose(fp);
            return 1;
        }
    }

    fclose(fp);
    return 0;
}

static void view_repos(const Session *session)
{
    char path[VELOCE_PATH_LEN + 1];
    FILE *fp;
    char line[2048];
    int count = 0;

    app_clear_screen();
    (void)printf("Your repositories\n\n");

    if (repos_db_path(path) != 0)
    {
        (void)printf("Failed to access repository database.\n");
        app_pause(NULL);
        return;
    }

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        (void)printf("Failed to access repository database.\n");
        app_pause(NULL);
        return;
    }

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        RepoRecord repo;

        line[strcspn(line, "\r\n")] = '\0';
        if (!parse_repo_line(line, &repo))
        {
            continue;
        }

        if (strcmp(repo.owner_uid, session->uid) == 0)
        {
            count++;
            (void)printf("%d) #%d  %s", count, repo.rid, repo.name);
            if (repo.initialized)
            {
                (void)printf("  [initialized]");
            }
            (void)printf("\n");
        }
    }

    fclose(fp);

    if (count == 0)
    {
        (void)printf("No repositories yet.\n");
    }

    app_pause(NULL);
}

static int open_repo(const Session *session, RepoRecord *opened)
{
    int rid;

    app_clear_screen();
    (void)printf("Open repository\n\n");

    if (!read_int("Repository id (#): ", &rid))
    {
        (void)printf("Please provide a valid repository id.\n");
        app_pause(NULL);
        return 0;
    }

    if (!load_repo_for_owner(session->uid, rid, opened))
    {
        (void)printf("Repository #%d was not found.\n", rid);
        app_pause(NULL);
        return 0;
    }

    (void)printf("Opening repository #%d (%s).\n", opened->rid, opened->name);
    app_pause(NULL);
    return 1;
}

int repo(const Session *session, RepoRecord *opened_repo)
{
    int choice;

    while (1)
    {
        app_clear_screen();
        (void)printf("Welcome %s (%s)\n\n", session->name, session->username);
        (void)printf("1) Create repository\n");
        (void)printf("2) View repositories\n");
        (void)printf("3) Open repository\n");
        (void)printf("4) Logout\n");
        (void)printf("5) Exit\n");

        if (!read_int("Choice: ", &choice))
        {
            (void)printf("Please enter a valid number.\n");
            app_pause(NULL);
            continue;
        }

        if (choice == 1)
        {
            create_repo(session);
        }
        else if (choice == 2)
        {
            view_repos(session);
        }
        else if (choice == 3)
        {
            if (open_repo(session, opened_repo))
            {
                comm(opened_repo);
            }
        }
        else if (choice == 4)
        {
            return 1;
        }
        else if (choice == 5)
        {
            return 0;
        }
        else
        {
            (void)printf("Please choose 1 to 5.\n");
            app_pause(NULL);
        }
    }
}

