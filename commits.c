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

static int commits_db_path(char path[VELOCE_PATH_LEN + 1])
{
    return path_join(path, VELOCE_PATH_LEN + 1U, storage_root(), VELOCE_COMMITS_DB);
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

static int update_repo_record(const RepoRecord *updated)
{
    char path[VELOCE_PATH_LEN + 1];
    char tmp_path[VELOCE_PATH_LEN + 1];
    FILE *in;
    FILE *out;
    char line[2048];
    int changed = 0;

    if (repos_db_path(path) != 0)
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
        RepoRecord repo;

        line[strcspn(line, "\r\n")] = '\0';
        if (!parse_repo_line(line, &repo))
        {
            continue;
        }

        if (strcmp(repo.id, updated->id) == 0)
        {
            repo = *updated;
            changed = 1;
        }

        if (!write_repo_line(out, &repo))
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

static int parse_commit_line(const char *line, CommitRecord *commit)
{
    char scratch[2048];
    char *fields[5];

    if (line == NULL || commit == NULL)
    {
        return 0;
    }

    if (snprintf(scratch, sizeof(scratch), "%s", line) >= (int)sizeof(scratch))
    {
        return 0;
    }

    if (!split_fields(scratch, fields, 5U))
    {
        return 0;
    }

    (void)snprintf(commit->id, sizeof(commit->id), "%s", fields[0]);
    (void)snprintf(commit->repo_id, sizeof(commit->repo_id), "%s", fields[1]);
    (void)snprintf(commit->timestamp, sizeof(commit->timestamp), "%s", fields[2]);
    (void)snprintf(commit->message, sizeof(commit->message), "%s", fields[3]);
    (void)snprintf(commit->snapshot_path, sizeof(commit->snapshot_path), "%s", fields[4]);

    return 1;
}

static int write_commit_line(FILE *fp, const CommitRecord *commit)
{
    if (fp == NULL || commit == NULL)
    {
        return 0;
    }

    return fprintf(fp,
                   "%s|%s|%s|%s|%s\n",
                   commit->id,
                   commit->repo_id,
                   commit->timestamp,
                   commit->message,
                   commit->snapshot_path) > 0;
}

static int append_commit(const CommitRecord *commit)
{
    char path[VELOCE_PATH_LEN + 1];
    FILE *fp;
    int ok;

    if (commits_db_path(path) != 0)
    {
        return 0;
    }

    fp = fopen(path, "ab");
    if (fp == NULL)
    {
        return 0;
    }

    ok = write_commit_line(fp, commit);
    fclose(fp);
    return ok;
}

static int build_snapshot_path(const char *commit_id, char out[VELOCE_PATH_LEN + 1])
{
    char snapshots_dir[VELOCE_PATH_LEN + 1];
    char file_name[VELOCE_ID_LEN + 5];

    if (path_join(snapshots_dir, sizeof(snapshots_dir), storage_root(), VELOCE_SNAPSHOTS_DIR) != 0)
    {
        return -1;
    }

    if (snprintf(file_name, sizeof(file_name), "%s.txt", commit_id) >= (int)sizeof(file_name))
    {
        return -1;
    }

    if (path_join(out, VELOCE_PATH_LEN + 1U, snapshots_dir, file_name) != 0)
    {
        return -1;
    }

    return 0;
}

static int create_commit_with_message(RepoRecord *repo, const char *message)
{
    char *content;
    size_t len;
    CommitRecord commit;

    if (read_text_file(repo->tracked_file, &content, &len) != 0)
    {
        (void)printf("Failed to read tracked file: %s\n", repo->tracked_file);
        return 0;
    }

    generate_id(commit.id);
    (void)snprintf(commit.repo_id, sizeof(commit.repo_id), "%s", repo->id);
    now_timestamp(commit.timestamp);
    (void)snprintf(commit.message, sizeof(commit.message), "%s", message);
    sanitize_field(commit.message);

    if (build_snapshot_path(commit.id, commit.snapshot_path) != 0)
    {
        free(content);
        return 0;
    }

    if (write_text_file(commit.snapshot_path, content, len) != 0)
    {
        free(content);
        return 0;
    }

    free(content);

    if (!append_commit(&commit))
    {
        return 0;
    }

    (void)printf("Commit created: %s\n", commit.id);
    return 1;
}

static int init_repo(RepoRecord *repo)
{
    int choice;
    char path[VELOCE_PATH_LEN + 1];

    while (1)
    {
        app_clear_screen();
        (void)printf("Repository: %s\n", repo->name);
        (void)printf("This repository is not initialized yet.\n\n");
        (void)printf("1) Track an existing file\n");
        (void)printf("2) Create a new tracked file\n");
        (void)printf("3) Back\n");

        if (!read_int("Choice: ", &choice))
        {
            (void)printf("Please enter a valid number.\n");
            app_pause(NULL);
            continue;
        }

        if (choice == 1)
        {
            if (!read_line("Path to file: ", path, sizeof(path)))
            {
                return 0;
            }
            sanitize_field(path);
            if (!file_exists(path))
            {
                (void)printf("File not found.\n");
                app_pause(NULL);
                continue;
            }

            (void)snprintf(repo->tracked_file, sizeof(repo->tracked_file), "%s", path);
            break;
        }

        if (choice == 2)
        {
            char workspace_root[VELOCE_PATH_LEN + 1];
            char repo_workspace[VELOCE_PATH_LEN + 1];

            if (path_join(workspace_root, sizeof(workspace_root), storage_root(), VELOCE_WORKSPACE_DIR) != 0 ||
                path_join(repo_workspace, sizeof(repo_workspace), workspace_root, repo->id) != 0)
            {
                (void)printf("Failed to build workspace path.\n");
                app_pause(NULL);
                return 0;
            }

            if (ensure_dir(repo_workspace) != 0)
            {
                (void)printf("Failed to create repository workspace.\n");
                app_pause(NULL);
                return 0;
            }

            if (path_join(path, sizeof(path), repo_workspace, "tracked.txt") != 0)
            {
                (void)printf("Failed to build tracked file path.\n");
                app_pause(NULL);
                return 0;
            }

            if (write_text_file(path, "", 0U) != 0)
            {
                (void)printf("Failed to create tracked file.\n");
                app_pause(NULL);
                return 0;
            }

            (void)snprintf(repo->tracked_file, sizeof(repo->tracked_file), "%s", path);
            break;
        }

        if (choice == 3)
        {
            return 0;
        }

        (void)printf("Please choose 1, 2, or 3.\n");
        app_pause(NULL);
    }

    repo->initialized = 1;

    if (!update_repo_record(repo))
    {
        (void)printf("Failed to save repository state.\n");
        app_pause(NULL);
        return 0;
    }

    if (!create_commit_with_message(repo, "Initial commit"))
    {
        (void)printf("Repository initialized, but initial commit failed.\n");
        app_pause(NULL);
        return 0;
    }

    (void)printf("Repository initialized successfully.\n");
    app_pause(NULL);
    return 1;
}

static int create_commit(RepoRecord *repo)
{
    char message[VELOCE_MSG_LEN + 1];

    app_clear_screen();
    (void)printf("Create commit\n\n");

    if (!read_line("Commit message: ", message, sizeof(message)))
    {
        return 0;
    }
    sanitize_field(message);

    if (message[0] == '\0')
    {
        (void)printf("Commit message cannot be empty.\n");
        app_pause(NULL);
        return 0;
    }

    if (!create_commit_with_message(repo, message))
    {
        (void)printf("Commit failed.\n");
        app_pause(NULL);
        return 0;
    }

    app_pause(NULL);
    return 1;
}

static int load_commits_for_repo(const RepoRecord *repo, CommitRecord **items, size_t *count)
{
    char path[VELOCE_PATH_LEN + 1];
    FILE *fp;
    char line[2048];
    CommitRecord *list = NULL;
    size_t len = 0U;
    size_t cap = 0U;

    if (items == NULL || count == NULL)
    {
        return 0;
    }

    *items = NULL;
    *count = 0U;

    if (commits_db_path(path) != 0)
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
        CommitRecord commit;
        CommitRecord *next;

        line[strcspn(line, "\r\n")] = '\0';
        if (!parse_commit_line(line, &commit))
        {
            continue;
        }

        if (strcmp(commit.repo_id, repo->id) != 0)
        {
            continue;
        }

        if (len == cap)
        {
            cap = (cap == 0U) ? 8U : cap * 2U;
            next = (CommitRecord *)realloc(list, cap * sizeof(CommitRecord));
            if (next == NULL)
            {
                free(list);
                fclose(fp);
                return 0;
            }
            list = next;
        }

        list[len++] = commit;
    }

    fclose(fp);

    *items = list;
    *count = len;
    return 1;
}

static void view_commits(const RepoRecord *repo)
{
    CommitRecord *commits;
    size_t count;
    size_t i;

    app_clear_screen();
    (void)printf("Commits for %s\n\n", repo->name);

    if (!load_commits_for_repo(repo, &commits, &count))
    {
        (void)printf("Failed to load commits.\n");
        app_pause(NULL);
        return;
    }

    if (count == 0U)
    {
        (void)printf("No commits yet.\n");
        free(commits);
        app_pause(NULL);
        return;
    }

    for (i = 0U; i < count; i++)
    {
        (void)printf("%zu) %s  %s\n", i + 1U, commits[i].id, commits[i].timestamp);
        (void)printf("    %s\n", commits[i].message);
    }

    free(commits);
    app_pause(NULL);
}

static int revert_commit(RepoRecord *repo)
{
    CommitRecord *commits;
    size_t count;
    size_t i;
    int choice;
    char revert_msg[VELOCE_MSG_LEN + 1];

    app_clear_screen();
    (void)printf("Revert commit\n\n");

    if (!load_commits_for_repo(repo, &commits, &count))
    {
        (void)printf("Failed to load commits.\n");
        app_pause(NULL);
        return 0;
    }

    if (count == 0U)
    {
        (void)printf("No commits available.\n");
        free(commits);
        app_pause(NULL);
        return 0;
    }

    for (i = 0U; i < count; i++)
    {
        (void)printf("%zu) %s  %s\n", i + 1U, commits[i].id, commits[i].message);
    }

    if (!read_int("Select commit number: ", &choice))
    {
        free(commits);
        (void)printf("Invalid selection.\n");
        app_pause(NULL);
        return 0;
    }

    if (choice < 1 || (size_t)choice > count)
    {
        free(commits);
        (void)printf("Invalid selection.\n");
        app_pause(NULL);
        return 0;
    }

    if (copy_text_file(commits[(size_t)choice - 1U].snapshot_path, repo->tracked_file) != 0)
    {
        free(commits);
        (void)printf("Failed to restore file from snapshot.\n");
        app_pause(NULL);
        return 0;
    }

    (void)snprintf(revert_msg,
                   sizeof(revert_msg),
                   "Revert to %s",
                   commits[(size_t)choice - 1U].id);

    if (!create_commit_with_message(repo, revert_msg))
    {
        free(commits);
        (void)printf("File reverted, but failed to record revert commit.\n");
        app_pause(NULL);
        return 0;
    }

    free(commits);
    (void)printf("Repository reverted successfully.\n");
    app_pause(NULL);
    return 1;
}

void comm(RepoRecord *repo)
{
    int choice;

    if (!repo->initialized)
    {
        if (!init_repo(repo))
        {
            return;
        }
    }

    while (1)
    {
        app_clear_screen();
        (void)printf("Repository #%d: %s\n", repo->rid, repo->name);
        (void)printf("Tracked file: %s\n\n", repo->tracked_file);
        (void)printf("1) Create commit\n");
        (void)printf("2) View commits\n");
        (void)printf("3) Revert to commit\n");
        (void)printf("4) Back\n");

        if (!read_int("Choice: ", &choice))
        {
            (void)printf("Please enter a valid number.\n");
            app_pause(NULL);
            continue;
        }

        if (choice == 1)
        {
            (void)create_commit(repo);
        }
        else if (choice == 2)
        {
            view_commits(repo);
        }
        else if (choice == 3)
        {
            (void)revert_commit(repo);
        }
        else if (choice == 4)
        {
            return;
        }
        else
        {
            (void)printf("Please choose 1 to 4.\n");
            app_pause(NULL);
        }
    }
}

