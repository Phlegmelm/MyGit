#define _POSIX_C_SOURCE 200809L
#include "repo.h"
#include "fs_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

/* =========================================================================
 * Index helpers
 * ====================================================================== */

static int read_index_entries(IndexEntry entries[], size_t max)
{
    FILE *f = fopen(REPO_INDEX_FILE, "r");
    if (!f)
    {
        log_msg(LOG_DEBUG, "read_index_entries: no index file");
        return 0;
    }
    size_t count = 0;
    char line[MAX_PATH_LEN];
    while (count < max && fgets(line, sizeof(line), f))
    {
        trim_whitespace(line);
        if (!line[0] || (line[0] != 'A' && line[0] != 'D') || line[1] != ' ')
            continue;
        entries[count].action = line[0];
        strncpy(entries[count].path, line + 2, MAX_PATH_LEN - 1);
        entries[count].path[MAX_PATH_LEN - 1] = '\0';
        normalize_path(entries[count].path);
        count++;
    }
    fclose(f);
    log_msg(LOG_DEBUG, "read_index_entries: %zu entries", count);
    return (int)count;
}

static int write_index_entries(const IndexEntry entries[], size_t count)
{
    FILE *f = fopen(REPO_INDEX_FILE, "w");
    if (!f)
    {
        log_msg(LOG_ERROR, "write_index_entries: cannot write index: %s", strerror(errno));
        return 0;
    }
    for (size_t i = 0; i < count; ++i)
        fprintf(f, "%c %s\n", entries[i].action, entries[i].path);
    fclose(f);
    return 1;
}

static int find_index_entry(const IndexEntry entries[], size_t count, const char *path)
{
    for (size_t i = 0; i < count; ++i)
        if (!strcmp(entries[i].path, path))
            return (int)i;
    return -1;
}

/* =========================================================================
 * Ignore helpers  (.mygitignore with full glob support)
 * ====================================================================== */

int should_ignore_file(const char *path)
{
    if (!path || !path[0])
        return 0;
    /* Always ignore .mygit itself */
    if (strncmp(path, ".mygit", 6) == 0 &&
        (path[6] == '\0' || path[6] == '/' || path[6] == '\\'))
        return 1;

    FILE *f = fopen(REPO_IGNORE_FILE, "r");
    if (!f)
        return 0;

    int ignored = 0;
    char line[MAX_PATH_LEN];
    while (!ignored && fgets(line, sizeof(line), f))
    {
        trim_whitespace(line);
        if (!line[0] || line[0] == '#')
            continue;

        /* Directory pattern: "build/" matches "build/anything" */
        size_t ll = strlen(line);
        if (line[ll - 1] == '/')
        {
            if (strncmp(path, line, ll - 1) == 0 &&
                (path[ll - 1] == '/' || path[ll - 1] == '\0'))
            {
                ignored = 1;
                break;
            }
        }

        /* Glob match against full path */
        if (match_pattern(line, path))
        {
            ignored = 1;
            break;
        }

        /* Glob match against just the filename component */
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        if (match_pattern(line, base))
        {
            ignored = 1;
            break;
        }
    }
    fclose(f);
    if (ignored)
        log_msg(LOG_DEBUG, "should_ignore_file: '%s' ignored", path);
    return ignored;
}

/* =========================================================================
 * Ref / HEAD helpers
 * ====================================================================== */

static int get_ref_value(const char *ref_path, char *out, size_t size)
{
    if (!read_text_file(ref_path, out, size))
        return 0;
    trim_whitespace(out);
    return 1;
}

static int update_ref(const char *ref_path, const char *commit_id)
{
    return write_text_file(ref_path, commit_id);
}

static int set_head_ref(const char *branch_name)
{
    char ref_path[MAX_PATH_LEN];
    if (!path_join(REPO_REFS_HEADS_DIR, branch_name, ref_path, sizeof(ref_path)))
        return 0;
    char contents[MAX_PATH_LEN];
    snprintf(contents, sizeof(contents), "ref: %s", ref_path);
    return write_text_file(REPO_HEAD_FILE, contents);
}

static int get_current_branch_ref(char *ref_path, size_t size)
{
    char head[MAX_INPUT];
    if (!read_text_file(REPO_HEAD_FILE, head, sizeof(head)))
        return 0;
    trim_whitespace(head);
    const char prefix[] = "ref: ";
    if (strncmp(head, prefix, 5) != 0)
        return 0;
    strncpy(ref_path, head + 5, size - 1);
    ref_path[size - 1] = '\0';
    return 1;
}

int get_current_commit_id(char *commit_id, size_t size)
{
    char head[MAX_INPUT];
    if (!read_text_file(REPO_HEAD_FILE, head, sizeof(head)))
        return 0;
    trim_whitespace(head);
    if (strncmp(head, "ref: ", 5) == 0)
    {
        const char *ref = head + 5;
        if (!get_ref_value(ref, commit_id, size))
        {
            commit_id[0] = '\0';
            return 1;
        }
        return 1;
    }
    strncpy(commit_id, head, size - 1);
    commit_id[size - 1] = '\0';
    return commit_id[0] != '\0';
}

int update_current_branch_head(const char *commit_id)
{
    char ref[MAX_PATH_LEN];
    if (!get_current_branch_ref(ref, sizeof(ref)))
        return write_text_file(REPO_HEAD_FILE, commit_id);
    return update_ref(ref, commit_id);
}

int get_current_branch_name(char *branch_name, size_t size)
{
    char ref[MAX_PATH_LEN];
    if (!get_current_branch_ref(ref, sizeof(ref)))
    {
        branch_name[0] = '\0';
        return 0;
    }
    const char *pfx[] = {REPO_REFS_HEADS_DIR "/", "refs/heads/"};
    for (int i = 0; i < 2; i++)
    {
        size_t plen = strlen(pfx[i]);
        if (strncmp(ref, pfx[i], plen) == 0)
        {
            strncpy(branch_name, ref + plen, size - 1);
            branch_name[size - 1] = '\0';
            return 1;
        }
    }
    branch_name[0] = '\0';
    return 0;
}

int branch_exists(const char *branch_name)
{
    char ref[MAX_PATH_LEN];
    if (!path_join(REPO_REFS_HEADS_DIR, branch_name, ref, sizeof(ref)))
        return 0;
    return file_exists(ref);
}

/* =========================================================================
 * Repository initialisation
 * ====================================================================== */

int initialize_repository(void)
{
    if (directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Repository already exists in '%s'.", REPO_DIR);
        return 0;
    }

    const char *dirs[] = {
        REPO_DIR, REPO_OBJECTS_DIR, REPO_COMMITS_DIR,
        REPO_REFS_DIR, REPO_REFS_HEADS_DIR, REPO_STASH_DIR};
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i)
    {
        if (!create_directory(dirs[i]))
        {
            log_msg(LOG_ERROR, "Failed to create '%s': %s", dirs[i], strerror(errno));
            return 0;
        }
    }

    char default_branch_ref[MAX_PATH_LEN];
    path_join(REPO_REFS_HEADS_DIR, "main", default_branch_ref, sizeof(default_branch_ref));
    if (!write_text_file(default_branch_ref, "") ||
        !write_text_file(REPO_INDEX_FILE, "") ||
        !write_text_file(REPO_LOG_FILE, "") ||
        !write_text_file(REPO_DEBUG_LOG_FILE, "") ||
        !write_text_file(REPO_STASH_INDEX_FILE, ""))
    {
        log_msg(LOG_ERROR, "Failed to initialise metadata files.");
        return 0;
    }

    /* Default config */
    write_text_file(REPO_CONFIG_FILE, "author=Anonymous\n");

    if (!set_head_ref("main"))
    {
        log_msg(LOG_ERROR, "Failed to initialise HEAD.");
        return 0;
    }

    log_msg(LOG_INFO, "%sInitialised empty MyGit repository.%s", COL_GREEN, COL_RESET);
    log_msg(LOG_INFO, "  location : %s", REPO_DIR);
    log_msg(LOG_INFO, "  branch   : main");
    log_msg(LOG_INFO, "  tip      : 'mygit config author \"Your Name\"' to set your name");
    return 1;
}

/* =========================================================================
 * Config
 * ====================================================================== */

void config_set(const char *key, const char *value)
{
    if (!key || !value)
    {
        log_msg(LOG_ERROR, "Usage: config <key> <value>");
        return;
    }

    /* Read all lines except ones matching key */
    FILE *f = fopen(REPO_CONFIG_FILE, "r");
    char lines[64][MAX_INPUT];
    int n = 0;
    if (f)
    {
        char line[MAX_INPUT];
        while (n < 64 && fgets(line, sizeof(line), f))
        {
            trim_whitespace(line);
            if (!line[0])
                continue;
            char *eq = strchr(line, '=');
            if (!eq)
                continue;
            *eq = '\0';
            if (strcmp(line, key) != 0)
            {
                *eq = '=';
                strncpy(lines[n++], line, MAX_INPUT - 1);
            }
        }
        fclose(f);
    }
    /* Append new key=value */
    f = fopen(REPO_CONFIG_FILE, "w");
    if (!f)
    {
        log_msg(LOG_ERROR, "Cannot write config: %s", strerror(errno));
        return;
    }
    for (int i = 0; i < n; i++)
        fprintf(f, "%s\n", lines[i]);
    fprintf(f, "%s=%s\n", key, value);
    fclose(f);
    log_msg(LOG_INFO, "Config: %s = %s", key, value);
}

void config_get(const char *key)
{
    if (!key)
    {
        log_msg(LOG_ERROR, "Usage: config <key>");
        return;
    }
    FILE *f = fopen(REPO_CONFIG_FILE, "r");
    if (!f)
    {
        log_msg(LOG_WARN, "No config file found.");
        return;
    }
    char line[MAX_INPUT];
    while (fgets(line, sizeof(line), f))
    {
        trim_whitespace(line);
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        if (!strcmp(line, key))
        {
            printf("%s\n", eq + 1);
            fclose(f);
            return;
        }
    }
    fclose(f);
    log_msg(LOG_WARN, "Key '%s' not found in config.", key);
}

void config_list(void)
{
    FILE *f = fopen(REPO_CONFIG_FILE, "r");
    if (!f)
    {
        log_msg(LOG_WARN, "No config file.");
        return;
    }
    printf("%sConfig:%s\n", COL_BOLD, COL_RESET);
    char line[MAX_INPUT];
    while (fgets(line, sizeof(line), f))
    {
        trim_whitespace(line);
        if (!line[0])
            continue;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        printf("  %s%s%s = %s\n", COL_CYAN, line, COL_RESET, eq + 1);
    }
    fclose(f);
}

static void get_author(char *out, size_t size)
{
    FILE *f = fopen(REPO_CONFIG_FILE, "r");
    strncpy(out, "Anonymous", size - 1);
    out[size - 1] = '\0';
    if (!f)
        return;
    char line[MAX_INPUT];
    while (fgets(line, sizeof(line), f))
    {
        trim_whitespace(line);
        if (strncmp(line, "author=", 7) == 0)
        {
            strncpy(out, line + 7, size - 1);
            out[size - 1] = '\0';
            break;
        }
    }
    fclose(f);
}

/* =========================================================================
 * Staging area
 * ====================================================================== */

int stage_path(const char *path)
{
    if (!path || !path[0])
    {
        log_msg(LOG_ERROR, "Usage: add <file-path>");
        return 0;
    }
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return 0;
    }

    char norm[MAX_PATH_LEN];
    strncpy(norm, path, sizeof(norm) - 1);
    norm[sizeof(norm) - 1] = '\0';
    normalize_path(norm);
    trim_whitespace(norm);

    if (should_ignore_file(norm))
    {
        log_msg(LOG_WARN, "'%s' is ignored by .mygitignore.", norm);
        return 0;
    }
    if (!file_exists(norm))
    {
        log_msg(LOG_ERROR, "File '%s' does not exist.", norm);
        return 0;
    }

    IndexEntry *entries = malloc(sizeof(IndexEntry) * MAX_TREE_ENTRIES);
    if (!entries)
    {
        log_msg(LOG_ERROR, "Out of memory.");
        return 0;
    }

    int count = read_index_entries(entries, MAX_TREE_ENTRIES);
    if (count < 0)
        count = 0;
    int idx = find_index_entry(entries, count, norm);
    if (idx >= 0)
        entries[idx].action = 'A';
    else
    {
        if (count >= MAX_TREE_ENTRIES)
        {
            log_msg(LOG_ERROR, "Staging area full.");
            free(entries);
            return 0;
        }
        entries[count].action = 'A';
        strncpy(entries[count].path, norm, MAX_PATH_LEN - 1);
        entries[count].path[MAX_PATH_LEN - 1] = '\0';
        count++;
    }

    int ok = write_index_entries(entries, count);
    free(entries);
    if (ok)
        log_msg(LOG_INFO, "%sstaged%s  %s", COL_GREEN, COL_RESET, norm);
    return ok;
}

int stage_remove(const char *path)
{
    if (!path || !path[0])
    {
        log_msg(LOG_ERROR, "Usage: rm <file-path>");
        return 0;
    }
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return 0;
    }

    char norm[MAX_PATH_LEN];
    strncpy(norm, path, sizeof(norm) - 1);
    norm[sizeof(norm) - 1] = '\0';
    normalize_path(norm);
    trim_whitespace(norm);

    IndexEntry *entries = malloc(sizeof(IndexEntry) * MAX_TREE_ENTRIES);
    if (!entries)
    {
        log_msg(LOG_ERROR, "Out of memory.");
        return 0;
    }

    int count = read_index_entries(entries, MAX_TREE_ENTRIES);
    if (count < 0)
        count = 0;
    int idx = find_index_entry(entries, count, norm);
    if (idx >= 0)
    {
        if (entries[idx].action == 'D')
        {
            log_msg(LOG_WARN, "'%s' already staged for deletion.", norm);
            free(entries);
            return 1;
        }
        entries[idx].action = 'D';
    }
    else
    {
        if (count >= MAX_TREE_ENTRIES)
        {
            log_msg(LOG_ERROR, "Staging area full.");
            free(entries);
            return 0;
        }
        entries[count].action = 'D';
        strncpy(entries[count].path, norm, MAX_PATH_LEN - 1);
        entries[count].path[MAX_PATH_LEN - 1] = '\0';
        count++;
    }

    int ok = write_index_entries(entries, count);
    free(entries);
    if (ok)
        log_msg(LOG_INFO, "%sstaged delete%s  %s", COL_RED, COL_RESET, norm);
    return ok;
}

int clear_index(void)
{
    return write_text_file(REPO_INDEX_FILE, "");
}

/* =========================================================================
 * Commit metadata
 * ====================================================================== */

int read_commit_metadata(const char *commit_id, CommitMetadata *meta)
{
    if (!commit_id || !meta)
        return 0;
    char cf[MAX_PATH_LEN];
    if (!path_join(REPO_COMMITS_DIR, commit_id, cf, sizeof(cf)))
        return 0;
    strncat(cf, ".txt", sizeof(cf) - strlen(cf) - 1);

    FILE *f = fopen(cf, "r");
    if (!f)
    {
        log_msg(LOG_DEBUG, "read_commit_metadata: cannot open '%s'", cf);
        return 0;
    }

    memset(meta, 0, sizeof(*meta));
    char line[MAX_INPUT];
    while (fgets(line, sizeof(line), f))
    {
        trim_whitespace(line);
        if (strncmp(line, "commit:", 7) == 0)
            strncpy(meta->commit_id, line + 7, sizeof(meta->commit_id) - 1);
        else if (strncmp(line, "parent:", 7) == 0)
            strncpy(meta->parent_id, line + 7, sizeof(meta->parent_id) - 1);
        else if (strncmp(line, "parent2:", 8) == 0)
            strncpy(meta->parent2_id, line + 8, sizeof(meta->parent2_id) - 1);
        else if (strncmp(line, "branch:", 7) == 0)
            strncpy(meta->branch, line + 7, sizeof(meta->branch) - 1);
        else if (strncmp(line, "author:", 7) == 0)
            strncpy(meta->author, line + 7, sizeof(meta->author) - 1);
        else if (strncmp(line, "date:", 5) == 0)
            strncpy(meta->date, line + 5, sizeof(meta->date) - 1);
        else if (strncmp(line, "message:", 8) == 0)
            strncpy(meta->message, line + 8, sizeof(meta->message) - 1);
    }
    fclose(f);
    return meta->commit_id[0] != '\0';
}

int read_commit_tree(const char *commit_id, char paths[][MAX_PATH_LEN], size_t max)
{
    if (!commit_id)
        return 0;
    char cf[MAX_PATH_LEN];
    if (!path_join(REPO_COMMITS_DIR, commit_id, cf, sizeof(cf)))
        return 0;
    strncat(cf, ".txt", sizeof(cf) - strlen(cf) - 1);

    FILE *f = fopen(cf, "r");
    if (!f)
        return 0;
    int count = 0, in_tree = 0;
    char line[MAX_INPUT];
    while (fgets(line, sizeof(line), f))
    {
        trim_whitespace(line);
        if (!in_tree)
        {
            if (!strcmp(line, "tree:"))
                in_tree = 1;
            continue;
        }
        if (!line[0] || count >= (int)max)
            continue;
        strncpy(paths[count], line, MAX_PATH_LEN - 1);
        paths[count][MAX_PATH_LEN - 1] = '\0';
        count++;
    }
    fclose(f);
    return count;
}

/* =========================================================================
 * Snapshot helpers
 * ====================================================================== */

static int save_commit_metadata(const char *commit_id, const char *parent_id,
                                const char *parent2_id,
                                const char *branch, const char *author,
                                const char *message)
{
    char cf[MAX_PATH_LEN];
    if (!path_join(REPO_COMMITS_DIR, commit_id, cf, sizeof(cf)))
        return 0;
    strncat(cf, ".txt", sizeof(cf) - strlen(cf) - 1);

    char commit_dir[MAX_PATH_LEN];
    if (!path_join(REPO_OBJECTS_DIR, commit_id, commit_dir, sizeof(commit_dir)))
        return 0;

    char (*tree_paths)[MAX_PATH_LEN] = malloc(MAX_TREE_ENTRIES * MAX_PATH_LEN);
    if (!tree_paths)
        return 0;
    size_t tree_count = 0;
    collect_tree_paths(commit_dir, "", tree_paths, MAX_TREE_ENTRIES, &tree_count);

    FILE *f = fopen(cf, "w");
    if (!f)
    {
        free(tree_paths);
        return 0;
    }

    time_t now = time(NULL);
    struct tm tm_info;
#ifdef _WIN32
    localtime_s(&tm_info, &now);
#else
    localtime_r(&now, &tm_info);
#endif
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_info);

    fprintf(f, "commit:%s\n", commit_id);
    fprintf(f, "parent:%s\n", parent_id ? parent_id : "");
    fprintf(f, "parent2:%s\n", parent2_id ? parent2_id : "");
    fprintf(f, "branch:%s\n", branch ? branch : "");
    fprintf(f, "author:%s\n", author ? author : "Anonymous");
    fprintf(f, "date:%s\n", ts);
    fprintf(f, "message:%s\n", message ? message : "");
    fprintf(f, "tree:\n");
    for (size_t i = 0; i < tree_count; i++)
        fprintf(f, "%s\n", tree_paths[i]);
    fclose(f);
    free(tree_paths);
    return 1;
}

static int copy_parent_snapshot(const char *parent_id, const char *dst_dir)
{
    if (!parent_id || !parent_id[0])
        return create_directory(dst_dir);
    char src[MAX_PATH_LEN];
    if (!path_join(REPO_OBJECTS_DIR, parent_id, src, sizeof(src)))
        return 0;
    if (!directory_exists(src))
        return create_directory(dst_dir);
    return copy_directory_recursive(src, dst_dir);
}

static int create_commit_snapshot(const char *commit_id, const char *parent_id,
                                  const IndexEntry entries[], int count)
{
    char commit_dir[MAX_PATH_LEN];
    if (!path_join(REPO_OBJECTS_DIR, commit_id, commit_dir, sizeof(commit_dir)))
        return 0;
    if (!create_directory(commit_dir))
        return 0;
    if (!copy_parent_snapshot(parent_id, commit_dir))
        return 0;
    for (int i = 0; i < count; i++)
    {
        if (entries[i].action == 'A')
        {
            char dst[MAX_PATH_LEN];
            if (!path_join(commit_dir, entries[i].path, dst, sizeof(dst)))
                return 0;
            if (!copy_file(entries[i].path, dst))
            {
                log_msg(LOG_ERROR, "Failed to snapshot '%s'.", entries[i].path);
                return 0;
            }
        }
        else if (entries[i].action == 'D')
        {
            char target[MAX_PATH_LEN];
            if (!path_join(commit_dir, entries[i].path, target, sizeof(target)))
                return 0;
            remove_path_recursive(target);
        }
    }
    return 1;
}

int restore_snapshot_to_working_tree(const char *commit_id)
{
    char snap[MAX_PATH_LEN];
    path_join(REPO_OBJECTS_DIR, commit_id, snap, sizeof(snap));
    if (!directory_exists(snap))
    {
        log_msg(LOG_WARN, "No snapshot for commit '%s'.", commit_id);
        return 0;
    }
    return copy_directory_recursive(snap, ".");
}

/* =========================================================================
 * Commit command
 * ====================================================================== */

void commit_changes(const char *message)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return;
    }
    if (!message || !message[0])
    {
        log_msg(LOG_ERROR, "Commit message cannot be empty.");
        return;
    }

    IndexEntry *entries = malloc(sizeof(IndexEntry) * MAX_TREE_ENTRIES);
    if (!entries)
    {
        log_msg(LOG_ERROR, "Out of memory.");
        return;
    }

    int count = read_index_entries(entries, MAX_TREE_ENTRIES);
    if (count == 0)
    {
        log_msg(LOG_WARN, "Nothing staged. Use 'add' or 'rm' first.");
        free(entries);
        return;
    }

    char parent[MAX_COMMIT_ID] = "";
    get_current_commit_id(parent, sizeof(parent));

    time_t now = time(NULL);
    struct tm tm_info;
#ifdef _WIN32
    localtime_s(&tm_info, &now);
#else
    localtime_r(&now, &tm_info);
#endif
    char commit_id[MAX_COMMIT_ID];
    snprintf(commit_id, sizeof(commit_id), "%04d%02d%02d%02d%02d%02d-%04x",
             tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
             tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec,
             rand() & 0xffff);

    char branch[MAX_PATH_LEN] = "";
    get_current_branch_name(branch, sizeof(branch));
    char author[128] = "";
    get_author(author, sizeof(author));

    if (!create_commit_snapshot(commit_id, parent, entries, count))
    {
        log_msg(LOG_ERROR, "Failed to create snapshot.");
        free(entries);
        return;
    }

    /* Check for pending merge */
    char merge_parent[MAX_COMMIT_ID] = "";
    if (file_exists(REPO_MERGE_HEAD_FILE))
    {
        read_text_file(REPO_MERGE_HEAD_FILE, merge_parent, sizeof(merge_parent));
        trim_whitespace(merge_parent);
    }

    if (!save_commit_metadata(commit_id, parent,
                              merge_parent[0] ? merge_parent : NULL,
                              branch, author, message))
    {
        log_msg(LOG_ERROR, "Failed to save metadata.");
        free(entries);
        return;
    }

    if (!update_current_branch_head(commit_id))
    {
        log_msg(LOG_ERROR, "Failed to update branch ref.");
        free(entries);
        return;
    }

    /* Clean up merge state */
    if (merge_parent[0])
    {
        remove(REPO_MERGE_HEAD_FILE);
        remove(REPO_MERGE_MSG_FILE);
    }

    char log_entry[MAX_INPUT];
    snprintf(log_entry, sizeof(log_entry), "%s %s\n", commit_id, message);
    append_text_file(REPO_LOG_FILE, log_entry);
    clear_index();

    printf("%s[%s]%s %s%s%s — %s (%d file%s)\n",
           COL_YELLOW, commit_id, COL_RESET,
           COL_BOLD, branch, COL_RESET,
           message, count, count == 1 ? "" : "s");
    free(entries);
}

/* =========================================================================
 * Remove file
 * ====================================================================== */

void remove_file_command(const char *path)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return;
    }
    if (!path || !path[0])
    {
        log_msg(LOG_ERROR, "Usage: rm <file>");
        return;
    }

    char norm[MAX_PATH_LEN];
    strncpy(norm, path, sizeof(norm) - 1);
    norm[sizeof(norm) - 1] = '\0';
    normalize_path(norm);
    trim_whitespace(norm);

    if (file_exists(norm))
    {
        if (remove(norm) != 0)
        {
            log_msg(LOG_ERROR, "Cannot delete '%s': %s", norm, strerror(errno));
            return;
        }
        log_msg(LOG_INFO, "Deleted '%s' from working tree.", norm);
    }
    if (!stage_remove(norm))
        log_msg(LOG_ERROR, "Failed to stage deletion of '%s'.", norm);
}

/* =========================================================================
 * Status
 * ====================================================================== */

void show_status(void)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return;
    }

    char branch[MAX_PATH_LEN] = "";
    get_current_branch_name(branch, sizeof(branch));
    char commit_id[MAX_COMMIT_ID] = "";
    get_current_commit_id(commit_id, sizeof(commit_id));

    printf("%sOn branch %s%s%s%s\n",
           COL_BOLD, COL_GREEN, branch[0] ? branch : "(detached)", COL_RESET, COL_RESET);
    if (commit_id[0])
        printf("HEAD: %s%s%s\n", COL_YELLOW, commit_id, COL_RESET);
    else
        printf("%sNo commits yet.%s\n", COL_DIM, COL_RESET);

    /* Merge state */
    if (file_exists(REPO_MERGE_HEAD_FILE))
    {
        char mh[MAX_COMMIT_ID] = "";
        read_text_file(REPO_MERGE_HEAD_FILE, mh, sizeof(mh));
        printf("%sMerge in progress (MERGE_HEAD: %s). Commit to complete.%s\n",
               COL_MAGENTA, mh, COL_RESET);
    }

    IndexEntry *entries = malloc(sizeof(IndexEntry) * MAX_TREE_ENTRIES);
    if (!entries)
    {
        log_msg(LOG_ERROR, "Out of memory.");
        return;
    }
    int staged = read_index_entries(entries, MAX_TREE_ENTRIES);

    if (staged > 0)
    {
        printf("\n%sChanges staged for commit:%s\n", COL_BOLD, COL_RESET);
        for (int i = 0; i < staged; i++)
        {
            if (entries[i].action == 'A')
                printf("  %snew/modified:%s %s\n", COL_GREEN, COL_RESET, entries[i].path);
            else
                printf("  %sdeleted:     %s %s\n", COL_RED, COL_RESET, entries[i].path);
        }
    }
    else
        printf("\n%s(nothing staged)%s\n", COL_DIM, COL_RESET);

    if (commit_id[0])
    {
        char (*tracked)[MAX_PATH_LEN] = malloc(MAX_TREE_ENTRIES * MAX_PATH_LEN);
        if (!tracked)
        {
            free(entries);
            return;
        }
        int tc = read_commit_tree(commit_id, tracked, MAX_TREE_ENTRIES);
        int shown = 0;
        if (tc > 0)
        {
            for (int i = 0; i < tc; i++)
            {
                char hf[MAX_PATH_LEN], cd[MAX_PATH_LEN];
                path_join(REPO_OBJECTS_DIR, commit_id, cd, sizeof(cd));
                path_join(cd, tracked[i], hf, sizeof(hf));
                int staged_already = find_index_entry(entries, staged, tracked[i]) >= 0;
                if (staged_already)
                    continue;
                if (!file_exists(tracked[i]))
                {
                    if (!shown++)
                        printf("\n%sChanges not staged:%s\n", COL_BOLD, COL_RESET);
                    printf("  %sdeleted:  %s %s\n", COL_RED, COL_RESET, tracked[i]);
                }
                else if (compare_files(hf, tracked[i]))
                {
                    if (!shown++)
                        printf("\n%sChanges not staged:%s\n", COL_BOLD, COL_RESET);
                    printf("  %smodified: %s %s\n", COL_YELLOW, COL_RESET, tracked[i]);
                }
            }
        }
        if (!shown)
            printf("\n%s(working tree clean)%s\n", COL_DIM, COL_RESET);
        free(tracked);
    }
    free(entries);
}

/* =========================================================================
 * Log
 * ====================================================================== */

static int was_visited_fn(char visited[][MAX_COMMIT_ID], int vcount, const char *id);
static void show_log_all(int oneline);

void show_log(int oneline, int graph, int limit, int all)
{
    if (all)
    {
        show_log_all(oneline);
        return;
    }
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return;
    }
    char commit_id[MAX_COMMIT_ID] = "";
    if (!get_current_commit_id(commit_id, sizeof(commit_id)) || !commit_id[0])
    {
        printf("%sNo commits yet.%s\n", COL_DIM, COL_RESET);
        return;
    }

    int shown = 0;
    while (commit_id[0] && (limit <= 0 || shown < limit))
    {
        CommitMetadata m;
        if (!read_commit_metadata(commit_id, &m))
        {
            log_msg(LOG_WARN, "Cannot read commit '%s'.", commit_id);
            break;
        }

        if (oneline)
        {
            /* graph decoration */
            if (graph)
                printf("%s|%s ", COL_BLUE, COL_RESET);
            printf("%s%.20s%s %s\n", COL_YELLOW, m.commit_id, COL_RESET, m.message);
        }
        else
        {
            if (graph)
                printf("%s|%s\n", COL_BLUE, COL_RESET);
            printf("%scommit%s  %s%s%s\n", COL_BOLD, COL_RESET, COL_YELLOW, m.commit_id, COL_RESET);
            if (m.parent2_id[0])
                printf("merge   %s + %s\n", m.parent_id, m.parent2_id);
            printf("author  %s\n", m.author[0] ? m.author : "Anonymous");
            printf("branch  %s\n", m.branch[0] ? m.branch : "(unknown)");
            printf("date    %s\n", m.date);
            printf("\n        %s\n\n", m.message);
        }
        shown++;
        if (!m.parent_id[0])
            break;
        strncpy(commit_id, m.parent_id, sizeof(commit_id) - 1);
        commit_id[sizeof(commit_id) - 1] = '\0';
    }
    log_msg(LOG_DEBUG, "show_log: displayed %d commits", shown);
}

/* =========================================================================
 * Show commit
 * ====================================================================== */

void show_commit(const char *commit_id)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return;
    }
    if (!commit_id || !commit_id[0])
    {
        log_msg(LOG_ERROR, "Usage: show <commit-id>");
        return;
    }

    CommitMetadata m;
    if (!read_commit_metadata(commit_id, &m))
    {
        log_msg(LOG_ERROR, "Commit '%s' not found.", commit_id);
        return;
    }

    printf("%scommit%s  %s%s%s\n", COL_BOLD, COL_RESET, COL_YELLOW, m.commit_id, COL_RESET);
    printf("author  %s\n", m.author[0] ? m.author : "Anonymous");
    printf("date    %s\n", m.date);
    printf("branch  %s\n", m.branch);
    if (m.parent2_id[0])
        printf("parents %s\n        %s  (merge commit)\n", m.parent_id, m.parent2_id);
    else if (m.parent_id[0])
        printf("parent  %s\n", m.parent_id);
    printf("\n    %s\n\n", m.message);

    /* Show diff vs parent */
    if (m.parent_id[0])
    {
        printf("%s--- diff vs parent ---%s\n", COL_DIM, COL_RESET);
        show_diff(NULL, m.parent_id, m.commit_id);
    }
    else
    {
        /* First commit: list all files */
        char (*tree)[MAX_PATH_LEN] = malloc(MAX_TREE_ENTRIES * MAX_PATH_LEN);
        if (tree)
        {
            int tc = read_commit_tree(commit_id, tree, MAX_TREE_ENTRIES);
            for (int i = 0; i < tc; i++)
                printf("  %s+%s %s\n", COL_GREEN, COL_RESET, tree[i]);
            free(tree);
        }
    }
}

/* =========================================================================
 * Branches
 * ====================================================================== */

void show_branches(void)
{
    if (!directory_exists(REPO_REFS_HEADS_DIR))
    {
        log_msg(LOG_ERROR, "No branches found.");
        return;
    }
    char cur[MAX_PATH_LEN] = "";
    get_current_branch_name(cur, sizeof(cur));
    printf("%sBranches:%s\n", COL_BOLD, COL_RESET);

#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    char pat[MAX_PATH_LEN];
    snprintf(pat, sizeof(pat), "%s\\*", REPO_REFS_HEADS_DIR);
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do
    {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
            continue;
        int is_cur = cur[0] && !strcmp(cur, fd.cFileName);
        printf("  %s%s %s%s\n",
               is_cur ? COL_GREEN : "", is_cur ? "*" : " ",
               fd.cFileName, COL_RESET);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *dir = opendir(REPO_REFS_HEADS_DIR);
    if (!dir)
        return;
    struct dirent *e;
    while ((e = readdir(dir)) != NULL)
    {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        int is_cur = cur[0] && !strcmp(cur, e->d_name);
        printf("  %s%s %s%s\n",
               is_cur ? COL_GREEN : "", is_cur ? "*" : " ",
               e->d_name, COL_RESET);
    }
    closedir(dir);
#endif
}

void create_branch(const char *name)
{
    if (!name || !name[0])
    {
        log_msg(LOG_ERROR, "Usage: branch <name>");
        return;
    }
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return;
    }
    if (branch_exists(name))
    {
        log_msg(LOG_ERROR, "Branch '%s' already exists.", name);
        return;
    }

    /* Validate name: no spaces, slashes, dots at start */
    for (const char *c = name; *c; c++)
    {
        if (*c == ' ' || *c == '\\' || *c == '/')
        {
            log_msg(LOG_ERROR, "Invalid branch name '%s'. No spaces or slashes.", name);
            return;
        }
    }

    char commit_id[MAX_COMMIT_ID] = "";
    get_current_commit_id(commit_id, sizeof(commit_id));
    char ref[MAX_PATH_LEN];
    if (!path_join(REPO_REFS_HEADS_DIR, name, ref, sizeof(ref)))
    {
        log_msg(LOG_ERROR, "Branch name too long.");
        return;
    }
    if (!write_text_file(ref, commit_id))
    {
        log_msg(LOG_ERROR, "Failed to create branch '%s'.", name);
        return;
    }
    log_msg(LOG_INFO, "%sCreated branch '%s'%s at [%.8s]", COL_GREEN, name, COL_RESET, commit_id[0] ? commit_id : "root");
}

int checkout_branch(const char *name)
{
    if (!name || !name[0])
    {
        log_msg(LOG_ERROR, "Usage: checkout <branch>");
        return 0;
    }
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return 0;
    }

    /* Check for uncommitted changes */
    IndexEntry *entries = malloc(sizeof(IndexEntry) * MAX_TREE_ENTRIES);
    if (entries)
    {
        int staged = read_index_entries(entries, MAX_TREE_ENTRIES);
        free(entries);
        if (staged > 0)
        {
            log_msg(LOG_ERROR, "You have staged changes. Commit or stash them before checkout.");
            return 0;
        }
    }

    if (!branch_exists(name))
    {
        log_msg(LOG_ERROR, "Branch '%s' does not exist.", name);
        return 0;
    }

    char cur[MAX_PATH_LEN] = "";
    get_current_branch_name(cur, sizeof(cur));
    if (cur[0] && !strcmp(cur, name))
    {
        log_msg(LOG_INFO, "Already on branch '%s'.", name);
        return 1;
    }

    char ref[MAX_PATH_LEN];
    path_join(REPO_REFS_HEADS_DIR, name, ref, sizeof(ref));
    char target[MAX_COMMIT_ID] = "";
    get_ref_value(ref, target, sizeof(target));

    if (!set_head_ref(name))
    {
        log_msg(LOG_ERROR, "Failed to update HEAD.");
        return 0;
    }

    if (target[0])
    {
        log_msg(LOG_INFO, "Restoring snapshot [%.8s]...", target);
        if (!restore_snapshot_to_working_tree(target))
        {
            log_msg(LOG_ERROR, "Failed to restore snapshot.");
            return 0;
        }
    }

    log_msg(LOG_INFO, "%sSwitched to branch '%s'%s", COL_GREEN, name, COL_RESET);
    return 1;
}

/* =========================================================================
 * Diff (now supports commit-to-commit)
 * ====================================================================== */

static void diff_two_snapshots(const char *snap_a, const char *snap_b,
                               const char *label_a, const char *label_b)
{
    /* Collect all unique paths from both snapshots */
    char (*paths_a)[MAX_PATH_LEN] = malloc(MAX_TREE_ENTRIES * MAX_PATH_LEN);
    char (*paths_b)[MAX_PATH_LEN] = malloc(MAX_TREE_ENTRIES * MAX_PATH_LEN);
    if (!paths_a || !paths_b)
    {
        free(paths_a);
        free(paths_b);
        return;
    }

    size_t ca = 0, cb = 0;
    if (snap_a && directory_exists(snap_a))
        collect_tree_paths(snap_a, "", paths_a, MAX_TREE_ENTRIES, &ca);
    if (snap_b && directory_exists(snap_b))
        collect_tree_paths(snap_b, "", paths_b, MAX_TREE_ENTRIES, &cb);

    int shown = 0;
    /* Files in B */
    for (size_t i = 0; i < cb; i++)
    {
        char fa[MAX_PATH_LEN], fb[MAX_PATH_LEN];
        path_join(snap_a ? snap_a : "", paths_b[i], fa, sizeof(fa));
        path_join(snap_b, paths_b[i], fb, sizeof(fb));

        int in_a = snap_a && file_exists(fa);
        if (!in_a)
        {
            printf("  %s+ new file: %s%s\n", COL_GREEN, paths_b[i], COL_RESET);
            shown++;
        }
        else if (compare_files(fa, fb))
        {
            printf("%s--- %s/%s%s\n", COL_RED, label_a, paths_b[i], COL_RESET);
            printf("%s+++ %s/%s%s\n", COL_GREEN, label_b, paths_b[i], COL_RESET);
            /* Line diff */
            FILE *a = fopen(fa, "r"), *b2 = fopen(fb, "r");
            if (a && b2)
            {
                char la[MAX_INPUT], lb[MAX_INPUT];
                int ln = 1;
                while (1)
                {
                    int ga = fgets(la, sizeof(la), a) != NULL;
                    int gb = fgets(lb, sizeof(lb), b2) != NULL;
                    if (!ga && !gb)
                        break;
                    if (!ga)
                        la[0] = '\0';
                    if (!gb)
                        lb[0] = '\0';
                    if (strcmp(la, lb) != 0)
                    {
                        printf("%s@@ line %d @@%s\n", COL_DIM, ln, COL_RESET);
                        if (la[0])
                            printf("%s- %s%s", COL_RED, la, COL_RESET);
                        if (lb[0])
                            printf("%s+ %s%s", COL_GREEN, lb, COL_RESET);
                    }
                    ln++;
                }
                fclose(a);
                fclose(b2);
            }
            else
            {
                if (a)
                    fclose(a);
                if (b2)
                    fclose(b2);
            }
            shown++;
        }
    }
    /* Files deleted (in A but not B) */
    for (size_t i = 0; i < ca; i++)
    {
        char fb[MAX_PATH_LEN];
        path_join(snap_b ? snap_b : "", paths_a[i], fb, sizeof(fb));
        if (!snap_b || !file_exists(fb))
        {
            printf("  %s- deleted:   %s%s\n", COL_RED, paths_a[i], COL_RESET);
            shown++;
        }
    }
    if (!shown)
        printf("  %s(no differences)%s\n", COL_DIM, COL_RESET);
    free(paths_a);
    free(paths_b);
}

void show_diff(const char *path, const char *commit_a, const char *commit_b)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return;
    }

    /* Commit-to-commit diff */
    if (commit_a && commit_b)
    {
        char snap_a[MAX_PATH_LEN], snap_b[MAX_PATH_LEN];
        path_join(REPO_OBJECTS_DIR, commit_a, snap_a, sizeof(snap_a));
        path_join(REPO_OBJECTS_DIR, commit_b, snap_b, sizeof(snap_b));
        if (!directory_exists(snap_a))
        {
            log_msg(LOG_ERROR, "Commit '%s' not found.", commit_a);
            return;
        }
        if (!directory_exists(snap_b))
        {
            log_msg(LOG_ERROR, "Commit '%s' not found.", commit_b);
            return;
        }

        char la[32], lb[32];
        snprintf(la, sizeof(la), "%.12s", commit_a);
        snprintf(lb, sizeof(lb), "%.12s", commit_b);

        if (path && path[0])
        {
            /* Single file between commits */
            char fa[MAX_PATH_LEN], fb[MAX_PATH_LEN];
            path_join(snap_a, path, fa, sizeof(fa));
            path_join(snap_b, path, fb, sizeof(fb));
            diff_two_snapshots(snap_a, snap_b, la, lb);
        }
        else
            diff_two_snapshots(snap_a, snap_b, la, lb);
        return;
    }

    /* Default: working tree vs HEAD */
    char commit_id[MAX_COMMIT_ID] = "";
    if (!get_current_commit_id(commit_id, sizeof(commit_id)) || !commit_id[0])
    {
        log_msg(LOG_ERROR, "No HEAD commit to diff against.");
        return;
    }

    char snap[MAX_PATH_LEN];
    path_join(REPO_OBJECTS_DIR, commit_id, snap, sizeof(snap));

    if (path && path[0])
    {
        char snap_file[MAX_PATH_LEN];
        path_join(snap, path, snap_file, sizeof(snap_file));
        int se = file_exists(snap_file), we = file_exists(path);
        if (!se && !we)
        {
            printf("Path '%s' not found.\n", path);
            return;
        }
        if (!se)
        {
            printf("  %s+ new file: %s%s\n", COL_GREEN, path, COL_RESET);
            return;
        }
        if (!we)
        {
            printf("  %s- deleted:  %s%s\n", COL_RED, path, COL_RESET);
            return;
        }
        if (!compare_files(snap_file, path))
        {
            printf("  %s(unchanged) %s%s\n", COL_DIM, path, COL_RESET);
            return;
        }

        if (is_binary_file(path) || is_binary_file(snap_file))
        {
            printf("  %sBinary file changed: %s%s\n", COL_YELLOW, path, COL_RESET);
            return;
        }
        printf("%s--- a/%s%s\n+++ b/%s\n", COL_RED, path, COL_RESET, path);
        FILE *a = fopen(snap_file, "r"), *b = fopen(path, "r");
        if (a && b)
        {
            char la[MAX_INPUT], lb[MAX_INPUT];
            int ln = 1;
            while (1)
            {
                int ga = fgets(la, sizeof(la), a) != NULL, gb = fgets(lb, sizeof(lb), b) != NULL;
                if (!ga && !gb)
                    break;
                if (!ga)
                    la[0] = '\0';
                if (!gb)
                    lb[0] = '\0';
                if (strcmp(la, lb) != 0)
                {
                    printf("%s@@ line %d @@%s\n", COL_DIM, ln, COL_RESET);
                    if (la[0])
                        printf("%s- %s%s", COL_RED, la, COL_RESET);
                    if (lb[0])
                        printf("%s+ %s%s", COL_GREEN, lb, COL_RESET);
                }
                ln++;
            }
            fclose(a);
            fclose(b);
        }
        else
        {
            if (a)
                fclose(a);
            if (b)
                fclose(b);
        }
    }
    else
    {
        /* All tracked files */
        char (*tracked)[MAX_PATH_LEN] = malloc(MAX_TREE_ENTRIES * MAX_PATH_LEN);
        if (!tracked)
            return;
        int tc = read_commit_tree(commit_id, tracked, MAX_TREE_ENTRIES);
        if (!tc)
        {
            printf("No tracked files to diff.\n");
            free(tracked);
            return;
        }
        for (int i = 0; i < tc; i++)
            show_diff(tracked[i], NULL, NULL);
        free(tracked);
    }
}

/* =========================================================================
 * Reset / Revert
 * ====================================================================== */

int reset_file(const char *path)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return 0;
    }
    if (!path || !path[0])
    {
        log_msg(LOG_ERROR, "Usage: reset HEAD <file>");
        return 0;
    }

    char norm[MAX_PATH_LEN];
    strncpy(norm, path, sizeof(norm) - 1);
    norm[sizeof(norm) - 1] = '\0';
    normalize_path(norm);
    trim_whitespace(norm);

    IndexEntry *entries = malloc(sizeof(IndexEntry) * MAX_TREE_ENTRIES);
    if (!entries)
    {
        log_msg(LOG_ERROR, "Out of memory.");
        return 0;
    }
    int count = read_index_entries(entries, MAX_TREE_ENTRIES);

    IndexEntry *new_e = malloc(sizeof(IndexEntry) * MAX_TREE_ENTRIES);
    if (!new_e)
    {
        free(entries);
        return 0;
    }
    int new_count = 0, found = 0;
    for (int i = 0; i < count; i++)
    {
        if (!strcmp(entries[i].path, norm))
            found = 1;
        else
            memcpy(&new_e[new_count++], &entries[i], sizeof(IndexEntry));
    }
    if (!found)
    {
        log_msg(LOG_WARN, "'%s' is not staged.", norm);
        free(entries);
        free(new_e);
        return 0;
    }

    int ok = write_index_entries(new_e, new_count);
    free(entries);
    free(new_e);
    if (ok)
        log_msg(LOG_INFO, "Unstaged '%s'.", norm);
    return ok;
}

int reset_hard(const char *commit_id)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return 0;
    }
    if (!commit_id || !commit_id[0])
    {
        log_msg(LOG_ERROR, "Usage: reset --hard <commit-id>");
        return 0;
    }

    char cd[MAX_PATH_LEN];
    path_join(REPO_OBJECTS_DIR, commit_id, cd, sizeof(cd));
    if (!directory_exists(cd))
    {
        log_msg(LOG_ERROR, "Commit '%s' not found.", commit_id);
        return 0;
    }

    char (*files)[MAX_PATH_LEN] = malloc(MAX_TREE_ENTRIES * MAX_PATH_LEN);
    if (!files)
        return 0;
    int fc = read_commit_tree(commit_id, files, MAX_TREE_ENTRIES);
    for (int i = 0; i < fc; i++)
    {
        char src[MAX_PATH_LEN];
        path_join(cd, files[i], src, sizeof(src));
        if (!copy_file(src, files[i]))
            log_msg(LOG_WARN, "Failed to restore '%s'.", files[i]);
    }
    clear_index();
    if (!update_current_branch_head(commit_id))
        log_msg(LOG_WARN, "Could not update HEAD (non-fatal).");
    log_msg(LOG_INFO, "%sReset to commit [%s]%s", COL_YELLOW, commit_id, COL_RESET);
    free(files);
    return 1;
}

void revert_commit(const char *commit_id)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return;
    }
    if (!commit_id || !commit_id[0])
    {
        log_msg(LOG_ERROR, "Usage: revert <commit-id>");
        return;
    }

    CommitMetadata meta;
    if (!read_commit_metadata(commit_id, &meta))
    {
        log_msg(LOG_ERROR, "Commit '%s' not found.", commit_id);
        return;
    }

    char (*files)[MAX_PATH_LEN] = malloc(MAX_TREE_ENTRIES * MAX_PATH_LEN);
    if (!files)
    {
        log_msg(LOG_ERROR, "Out of memory.");
        return;
    }
    int fc = read_commit_tree(commit_id, files, MAX_TREE_ENTRIES);
    if (!fc)
    {
        log_msg(LOG_ERROR, "No files in commit '%s'.", commit_id);
        free(files);
        return;
    }

    IndexEntry *entries = malloc(sizeof(IndexEntry) * MAX_TREE_ENTRIES);
    if (!entries)
    {
        free(files);
        return;
    }
    for (int i = 0; i < fc; i++)
    {
        entries[i].action = 'D';
        strncpy(entries[i].path, files[i], MAX_PATH_LEN - 1);
        entries[i].path[MAX_PATH_LEN - 1] = '\0';
    }
    write_index_entries(entries, fc);
    free(entries);
    free(files);
    log_msg(LOG_INFO, "Staged revert of [%s]. Run 'commit -m <message>' to complete.", commit_id);
}

/* =========================================================================
 * Merge  (fast-forward + file-level auto-merge)
 * ====================================================================== */

int merge_branch(const char *branch_name)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return 0;
    }
    if (!branch_name || !branch_name[0])
    {
        log_msg(LOG_ERROR, "Usage: merge <branch>");
        return 0;
    }
    if (!branch_exists(branch_name))
    {
        log_msg(LOG_ERROR, "Branch '%s' does not exist.", branch_name);
        return 0;
    }

    char cur[MAX_PATH_LEN] = "";
    get_current_branch_name(cur, sizeof(cur));
    if (!strcmp(cur, branch_name))
    {
        log_msg(LOG_ERROR, "Cannot merge a branch into itself.");
        return 0;
    }

    /* Check clean working tree */
    IndexEntry *idx = malloc(sizeof(IndexEntry) * MAX_TREE_ENTRIES);
    if (idx)
    {
        int s = read_index_entries(idx, MAX_TREE_ENTRIES);
        free(idx);
        if (s > 0)
        {
            log_msg(LOG_ERROR, "Staged changes present. Commit or stash first.");
            return 0;
        }
    }

    char our_commit[MAX_COMMIT_ID] = "";
    get_current_commit_id(our_commit, sizeof(our_commit));

    char branch_ref[MAX_PATH_LEN];
    path_join(REPO_REFS_HEADS_DIR, branch_name, branch_ref, sizeof(branch_ref));
    char their_commit[MAX_COMMIT_ID] = "";
    get_ref_value(branch_ref, their_commit, sizeof(their_commit));

    if (!their_commit[0])
    {
        log_msg(LOG_WARN, "Branch '%s' has no commits.", branch_name);
        return 0;
    }
    if (!strcmp(our_commit, their_commit))
    {
        log_msg(LOG_INFO, "Already up to date.");
        return 1;
    }

    /* Fast-forward check: is our_commit an ancestor of their_commit? */
    int is_ff = 0;
    if (!our_commit[0])
        is_ff = 1;
    else
    {
        /* Walk their history looking for our commit */
        char walk[MAX_COMMIT_ID];
        strncpy(walk, their_commit, sizeof(walk) - 1);
        for (int depth = 0; depth < 256 && walk[0]; depth++)
        {
            if (!strcmp(walk, our_commit))
            {
                is_ff = 1;
                break;
            }
            CommitMetadata wm;
            if (!read_commit_metadata(walk, &wm) || !wm.parent_id[0])
                break;
            strncpy(walk, wm.parent_id, sizeof(walk) - 1);
        }
    }

    if (is_ff)
    {
        /* Fast-forward: just move our branch pointer and restore snapshot */
        if (!update_current_branch_head(their_commit))
        {
            log_msg(LOG_ERROR, "Failed to update branch pointer.");
            return 0;
        }
        if (their_commit[0] && !restore_snapshot_to_working_tree(their_commit))
        {
            log_msg(LOG_ERROR, "Failed to restore snapshot.");
            return 0;
        }
        log_msg(LOG_INFO, "%sFast-forward merge.%s Branch '%s' is now at [%.8s].",
                COL_GREEN, COL_RESET, cur, their_commit);
        return 1;
    }

    /* Three-way merge: collect files from both snapshots */
    char our_snap[MAX_PATH_LEN], their_snap[MAX_PATH_LEN];
    path_join(REPO_OBJECTS_DIR, our_commit, our_snap, sizeof(our_snap));
    path_join(REPO_OBJECTS_DIR, their_commit, their_snap, sizeof(their_snap));

    char (*their_files)[MAX_PATH_LEN] = malloc(MAX_TREE_ENTRIES * MAX_PATH_LEN);
    char (*our_files)[MAX_PATH_LEN] = malloc(MAX_TREE_ENTRIES * MAX_PATH_LEN);
    if (!their_files || !our_files)
    {
        free(their_files);
        free(our_files);
        return 0;
    }

    size_t tc = 0, oc = 0;
    collect_tree_paths(their_snap, "", their_files, MAX_TREE_ENTRIES, &tc);
    collect_tree_paths(our_snap, "", our_files, MAX_TREE_ENTRIES, &oc);

    IndexEntry *staged = malloc(sizeof(IndexEntry) * MAX_TREE_ENTRIES);
    if (!staged)
    {
        free(their_files);
        free(our_files);
        return 0;
    }
    int staged_count = 0;
    int conflicts = 0;

    /* Bring in files from their branch */
    for (size_t i = 0; i < tc; i++)
    {
        char their_f[MAX_PATH_LEN], our_f[MAX_PATH_LEN];
        path_join(their_snap, their_files[i], their_f, sizeof(their_f));
        path_join(our_snap, their_files[i], our_f, sizeof(our_f));

        int exists_ours = file_exists(our_f);
        int wt_exists = file_exists(their_files[i]);

        if (!exists_ours)
        {
            /* New file in their branch: bring it in */
            copy_file(their_f, their_files[i]);
            staged[staged_count].action = 'A';
            strncpy(staged[staged_count].path, their_files[i], MAX_PATH_LEN - 1);
            staged_count++;
        }
        else if (compare_files(our_f, their_f))
        {
            /* Both modified — check if we also changed it */
            int we_changed = wt_exists && compare_files(our_f, their_files[i]);
            if (!we_changed)
            {
                /* We didn't touch it: take theirs */
                copy_file(their_f, their_files[i]);
                staged[staged_count].action = 'A';
                strncpy(staged[staged_count].path, their_files[i], MAX_PATH_LEN - 1);
                staged_count++;
            }
            else
            {
                /* Conflict! Write conflict markers */
                FILE *cf = fopen(their_files[i], "a");
                if (cf)
                {
                    fprintf(cf, "\n<<<<<<< HEAD (%s)\n", cur);
                    fprintf(cf, "======= (%s)\n", branch_name);
                    FILE *tf = fopen(their_f, "r");
                    if (tf)
                    {
                        char line[MAX_INPUT];
                        while (fgets(line, sizeof(line), tf))
                            fputs(line, cf);
                        fclose(tf);
                    }
                    fprintf(cf, ">>>>>>> %s\n", branch_name);
                    fclose(cf);
                }
                log_msg(LOG_WARN, "%sCONFLICT%s in '%s' — resolve manually then commit.",
                        COL_RED, COL_RESET, their_files[i]);
                staged[staged_count].action = 'A';
                strncpy(staged[staged_count].path, their_files[i], MAX_PATH_LEN - 1);
                staged_count++;
                conflicts++;
            }
        }
    }

    write_index_entries(staged, staged_count);
    free(staged);
    free(their_files);
    free(our_files);

    /* Write MERGE_HEAD so next commit records both parents */
    write_text_file(REPO_MERGE_HEAD_FILE, their_commit);
    char merge_msg[MAX_MESSAGE_LEN];
    snprintf(merge_msg, sizeof(merge_msg), "Merge branch '%s' into '%s'", branch_name, cur);
    write_text_file(REPO_MERGE_MSG_FILE, merge_msg);

    if (conflicts)
    {
        log_msg(LOG_WARN, "%s%d conflict(s) found.%s Fix them then 'commit -m \"%s\"'.",
                COL_RED, conflicts, COL_RESET, merge_msg);
        return 0;
    }

    log_msg(LOG_INFO, "%sMerge successful.%s Staged %d file(s).", COL_GREEN, COL_RESET, staged_count);
    log_msg(LOG_INFO, "Run: commit -m \"%s\"", merge_msg);
    return 1;
}

/* =========================================================================
 * Stash
 * ====================================================================== */

static int count_stash_entries(void)
{
    FILE *f = fopen(REPO_STASH_INDEX_FILE, "r");
    if (!f)
        return 0;
    int n = 0;
    char line[MAX_INPUT];
    while (fgets(line, sizeof(line), f))
    {
        trim_whitespace(line);
        if (line[0])
            n++;
    }
    fclose(f);
    return n;
}

int stash_save(const char *message)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return 0;
    }

    IndexEntry *entries = malloc(sizeof(IndexEntry) * MAX_TREE_ENTRIES);
    if (!entries)
        return 0;
    int staged = read_index_entries(entries, MAX_TREE_ENTRIES);
    free(entries);

    /* Also check for unstaged changes */
    char commit_id[MAX_COMMIT_ID] = "";
    get_current_commit_id(commit_id, sizeof(commit_id));
    int has_unstaged = 0;
    if (commit_id[0])
    {
        char (*tracked)[MAX_PATH_LEN] = malloc(MAX_TREE_ENTRIES * MAX_PATH_LEN);
        if (tracked)
        {
            int tc = read_commit_tree(commit_id, tracked, MAX_TREE_ENTRIES);
            for (int i = 0; i < tc; i++)
            {
                char hf[MAX_PATH_LEN], cd[MAX_PATH_LEN];
                path_join(REPO_OBJECTS_DIR, commit_id, cd, sizeof(cd));
                path_join(cd, tracked[i], hf, sizeof(hf));
                if (!file_exists(tracked[i]) || compare_files(hf, tracked[i]))
                {
                    has_unstaged = 1;
                    break;
                }
            }
            free(tracked);
        }
    }

    if (staged == 0 && !has_unstaged)
    {
        log_msg(LOG_WARN, "Nothing to stash. Working tree is clean.");
        return 0;
    }

    int idx = count_stash_entries();
    if (idx >= MAX_STASH_ENTRIES)
    {
        log_msg(LOG_ERROR, "Stash is full (%d entries).", MAX_STASH_ENTRIES);
        return 0;
    }

    /* Save stash snapshot like a commit */
    char stash_id[MAX_COMMIT_ID];
    snprintf(stash_id, sizeof(stash_id), "stash-%04d", idx);

    char stash_snap[MAX_PATH_LEN];
    path_join(REPO_STASH_DIR, stash_id, stash_snap, sizeof(stash_snap));

    /* Copy entire working tree tracked files */
    if (!directory_exists(stash_snap))
        create_directory(stash_snap);
    if (commit_id[0])
    {
        char src[MAX_PATH_LEN];
        path_join(REPO_OBJECTS_DIR, commit_id, src, sizeof(src));
        copy_directory_recursive(src, stash_snap);
    }
    /* Overlay with current working tree changes */
    {
        char (*all)[MAX_PATH_LEN] = malloc(MAX_TREE_ENTRIES * MAX_PATH_LEN);
        if (all)
        {
            size_t ac = 0;
            collect_tree_paths(".", "", all, MAX_TREE_ENTRIES, &ac);
            for (size_t i = 0; i < ac; i++)
            {
                if (should_ignore_file(all[i]))
                    continue;
                char dst[MAX_PATH_LEN];
                path_join(stash_snap, all[i], dst, sizeof(dst));
                copy_file(all[i], dst);
            }
            free(all);
        }
    }

    /* Record in stash index */
    char branch[MAX_PATH_LEN] = "";
    get_current_branch_name(branch, sizeof(branch));
    time_t now = time(NULL);
    struct tm tm_info;
#ifdef _WIN32
    localtime_s(&tm_info, &now);
#else
    localtime_r(&now, &tm_info);
#endif
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_info);

    char msg[MAX_MESSAGE_LEN];
    if (message && message[0])
        strncpy(msg, message, sizeof(msg) - 1);
    else
        snprintf(msg, sizeof(msg), "WIP on %s: %.8s", branch, commit_id[0] ? commit_id : "(root)");
    msg[sizeof(msg) - 1] = '\0';

    char entry[MAX_INPUT];
    snprintf(entry, sizeof(entry), "%d|%s|%s|%s|%s\n", idx, stash_id, msg, ts, branch);
    append_text_file(REPO_STASH_INDEX_FILE, entry);

    /* Restore working tree to HEAD */
    if (commit_id[0])
        restore_snapshot_to_working_tree(commit_id);
    clear_index();

    log_msg(LOG_INFO, "%sSaved stash@{%d}:%s %s", COL_GREEN, idx, COL_RESET, msg);
    return 1;
}

int stash_pop(void)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return 0;
    }

    FILE *f = fopen(REPO_STASH_INDEX_FILE, "r");
    if (!f)
    {
        log_msg(LOG_WARN, "No stash entries.");
        return 0;
    }

    /* Read all lines, pop last */
    char lines[MAX_STASH_ENTRIES][MAX_INPUT];
    int n = 0;
    while (n < MAX_STASH_ENTRIES && fgets(lines[n], sizeof(lines[n]), f))
    {
        trim_whitespace(lines[n]);
        if (lines[n][0])
            n++;
    }
    fclose(f);
    if (!n)
    {
        log_msg(LOG_WARN, "No stash entries.");
        return 0;
    }

    /* Parse last entry */
    char last[MAX_INPUT];
    strncpy(last, lines[n - 1], sizeof(last) - 1);
    char *p = last, *tok;
    tok = strtok(p, "|");    /* index */
    tok = strtok(NULL, "|"); /* stash_id */
    char stash_id[64] = "";
    if (tok)
        strncpy(stash_id, tok, sizeof(stash_id) - 1);
    tok = strtok(NULL, "|"); /* message */
    char msg[MAX_MESSAGE_LEN] = "";
    if (tok)
        strncpy(msg, tok, sizeof(msg) - 1);

    char stash_snap[MAX_PATH_LEN];
    path_join(REPO_STASH_DIR, stash_id, stash_snap, sizeof(stash_snap));
    if (!directory_exists(stash_snap))
    {
        log_msg(LOG_ERROR, "Stash snapshot '%s' missing.", stash_id);
        return 0;
    }

    /* Restore stash snapshot to working tree */
    if (!copy_directory_recursive(stash_snap, "."))
    {
        log_msg(LOG_ERROR, "Failed to restore stash.");
        return 0;
    }

    /* Remove from stash index */
    f = fopen(REPO_STASH_INDEX_FILE, "w");
    if (f)
    {
        for (int i = 0; i < n - 1; i++)
            fprintf(f, "%s\n", lines[i]);
        fclose(f);
    }

    /* Remove stash snapshot dir */
    remove_path_recursive(stash_snap);

    log_msg(LOG_INFO, "%sRestored stash:%s %s", COL_GREEN, COL_RESET, msg);
    log_msg(LOG_INFO, "Stash entry dropped.");
    return 1;
}

int stash_list(void)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return 0;
    }
    FILE *f = fopen(REPO_STASH_INDEX_FILE, "r");
    if (!f)
    {
        printf("No stash entries.\n");
        return 1;
    }
    char line[MAX_INPUT];
    int n = 0;
    printf("%sStash:%s\n", COL_BOLD, COL_RESET);
    while (fgets(line, sizeof(line), f))
    {
        trim_whitespace(line);
        if (!line[0])
            continue;
        /* Parse: idx|stash_id|message|date|branch */
        char tmp[MAX_INPUT];
        strncpy(tmp, line, sizeof(tmp) - 1);
        char *idx_s = strtok(tmp, "|");
        strtok(NULL, "|"); /* stash_id */
        char *msg_s = strtok(NULL, "|");
        char *date_s = strtok(NULL, "|");
        char *branch_s = strtok(NULL, "|");
        printf("  %sstash@{%s}%s  %s  %s[%s]%s\n",
               COL_YELLOW, idx_s ? idx_s : "?", COL_RESET,
               msg_s ? msg_s : "(no message)",
               COL_DIM, branch_s ? branch_s : "?", COL_RESET);
        if (date_s)
            printf("           %s%s%s\n", COL_DIM, date_s, COL_RESET);
        n++;
    }
    fclose(f);
    if (!n)
        printf("  (empty)\n");
    return 1;
}

int stash_drop(int index)
{
    (void)index; /* For now, drop always removes the top */
    FILE *f = fopen(REPO_STASH_INDEX_FILE, "r");
    if (!f)
    {
        log_msg(LOG_WARN, "No stash entries.");
        return 0;
    }
    char lines[MAX_STASH_ENTRIES][MAX_INPUT];
    int n = 0;
    while (n < MAX_STASH_ENTRIES && fgets(lines[n], sizeof(lines[n]), f))
    {
        trim_whitespace(lines[n]);
        if (lines[n][0])
            n++;
    }
    fclose(f);
    if (!n)
    {
        log_msg(LOG_WARN, "No stash entries.");
        return 0;
    }

    /* Parse stash_id to remove snapshot dir */
    char tmp[MAX_INPUT];
    strncpy(tmp, lines[n - 1], sizeof(tmp) - 1);
    strtok(tmp, "|");
    char *sid = strtok(NULL, "|");
    if (sid)
    {
        char sp[MAX_PATH_LEN];
        path_join(REPO_STASH_DIR, sid, sp, sizeof(sp));
        remove_path_recursive(sp);
    }
    f = fopen(REPO_STASH_INDEX_FILE, "w");
    if (f)
    {
        for (int i = 0; i < n - 1; i++)
            fprintf(f, "%s\n", lines[i]);
        fclose(f);
    }
    log_msg(LOG_INFO, "Dropped stash@{%d}.", n - 1);
    return 1;
}

/* =========================================================================
 * Tags
 * ====================================================================== */

static const char *REPO_TAGS_DIR = ".mygit/refs/tags";

void create_tag(const char *tag_name, const char *commit_id)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return;
    }
    if (!tag_name || !tag_name[0])
    {
        log_msg(LOG_ERROR, "Usage: tag <name> [commit-id]");
        return;
    }
    create_directory(REPO_TAGS_DIR);

    char ref[MAX_PATH_LEN];
    path_join(REPO_TAGS_DIR, tag_name, ref, sizeof(ref));
    if (file_exists(ref))
    {
        log_msg(LOG_ERROR, "Tag '%s' already exists.", tag_name);
        return;
    }

    char cid[MAX_COMMIT_ID] = "";
    if (commit_id && commit_id[0])
        strncpy(cid, commit_id, sizeof(cid) - 1);
    else
        get_current_commit_id(cid, sizeof(cid));

    if (!cid[0])
    {
        log_msg(LOG_ERROR, "No commit to tag.");
        return;
    }
    if (!write_text_file(ref, cid))
    {
        log_msg(LOG_ERROR, "Failed to create tag '%s'.", tag_name);
        return;
    }
    log_msg(LOG_INFO, "%sTagged%s [%.8s] as '%s'", COL_YELLOW, COL_RESET, cid, tag_name);
}

void show_tags(void)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return;
    }
    create_directory(REPO_TAGS_DIR);
    printf("%sTags:%s\n", COL_BOLD, COL_RESET);
    int found = 0;

#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    char pat[MAX_PATH_LEN];
    snprintf(pat, sizeof(pat), "%s\\*", REPO_TAGS_DIR);
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        printf("  (none)\n");
        return;
    }
    do
    {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
            continue;
        char ref[MAX_PATH_LEN], cid[MAX_COMMIT_ID] = "";
        path_join(REPO_TAGS_DIR, fd.cFileName, ref, sizeof(ref));
        read_text_file(ref, cid, sizeof(cid));
        printf("  %s%s%s -> %.8s\n", COL_YELLOW, fd.cFileName, COL_RESET, cid);
        found++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *dir = opendir(REPO_TAGS_DIR);
    if (!dir)
    {
        printf("  (none)\n");
        return;
    }
    struct dirent *e;
    while ((e = readdir(dir)) != NULL)
    {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char ref[MAX_PATH_LEN], cid[MAX_COMMIT_ID] = "";
        path_join(REPO_TAGS_DIR, e->d_name, ref, sizeof(ref));
        read_text_file(ref, cid, sizeof(cid));
        printf("  %s%s%s -> %.8s\n", COL_YELLOW, e->d_name, COL_RESET, cid);
        found++;
    }
    closedir(dir);
#endif
    if (!found)
        printf("  (none)\n");
}

/* =========================================================================
 * Index locking  (prevents concurrent writes corrupting the index)
 * ====================================================================== */

int acquire_index_lock(void)
{
    /* Try to create the lock file exclusively */
    FILE *f = fopen(REPO_INDEX_LOCK_FILE, "wx"); /* 'x' = fail if exists (C11) */
    if (!f)
    {
        /* Fallback for compilers without C11 'x' mode */
        if (file_exists(REPO_INDEX_LOCK_FILE))
        {
            log_msg(LOG_ERROR, "Index is locked by another process. If this is an error, delete '%s'.",
                    REPO_INDEX_LOCK_FILE);
            return 0;
        }
        f = fopen(REPO_INDEX_LOCK_FILE, "w");
        if (!f)
            return 0;
    }
    fclose(f);
    return 1;
}

void release_index_lock(void)
{
    remove(REPO_INDEX_LOCK_FILE);
}

/* =========================================================================
 * Binary file detection
 * ====================================================================== */

int is_binary_file(const char *path)
{
    if (!path)
        return 0;
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    unsigned char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    /* Heuristic: if >5% of bytes are null or non-printable control chars, treat as binary */
    size_t suspicious = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (buf[i] == 0 || (buf[i] < 8 && buf[i] != '\t' && buf[i] != '\n' && buf[i] != '\r'))
            suspicious++;
    }
    return n > 0 && (suspicious * 100 / n) > 5;
}

/* =========================================================================
 * stage_all  (add .)
 * ====================================================================== */

int stage_all(void)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return 0;
    }

    char (*paths)[MAX_PATH_LEN] = malloc(MAX_TREE_ENTRIES * MAX_PATH_LEN);
    if (!paths)
    {
        log_msg(LOG_ERROR, "Out of memory.");
        return 0;
    }

    size_t count = 0;
    collect_tree_paths(".", "", paths, MAX_TREE_ENTRIES, &count);

    int staged = 0;
    for (size_t i = 0; i < count; i++)
    {
        if (should_ignore_file(paths[i]))
            continue;
        if (stage_path(paths[i]))
            staged++;
    }

    /* Also stage deletions: files tracked in HEAD but gone from working tree */
    char commit_id[MAX_COMMIT_ID] = "";
    get_current_commit_id(commit_id, sizeof(commit_id));
    if (commit_id[0])
    {
        char (*tracked)[MAX_PATH_LEN] = malloc(MAX_TREE_ENTRIES * MAX_PATH_LEN);
        if (tracked)
        {
            int tc = read_commit_tree(commit_id, tracked, MAX_TREE_ENTRIES);
            for (int i = 0; i < tc; i++)
            {
                if (!file_exists(tracked[i]))
                {
                    stage_remove(tracked[i]);
                    staged++;
                }
            }
            free(tracked);
        }
    }

    free(paths);
    log_msg(LOG_INFO, "Staged %d file%s.", staged, staged == 1 ? "" : "s");
    return staged > 0;
}

/* =========================================================================
 * branch -d  (delete branch)
 * ====================================================================== */

int delete_branch(const char *name)
{
    if (!name || !name[0])
    {
        log_msg(LOG_ERROR, "Usage: branch -d <name>");
        return 0;
    }
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return 0;
    }

    char cur[MAX_PATH_LEN] = "";
    get_current_branch_name(cur, sizeof(cur));
    if (!strcmp(cur, name))
    {
        log_msg(LOG_ERROR, "Cannot delete the currently checked-out branch '%s'.", name);
        return 0;
    }

    if (!branch_exists(name))
    {
        log_msg(LOG_ERROR, "Branch '%s' does not exist.", name);
        return 0;
    }

    char ref[MAX_PATH_LEN];
    path_join(REPO_REFS_HEADS_DIR, name, ref, sizeof(ref));
    if (remove(ref) != 0)
    {
        log_msg(LOG_ERROR, "Failed to delete branch '%s': %s", name, strerror(errno));
        return 0;
    }

    log_msg(LOG_INFO, "%sDeleted branch '%s'.%s", COL_RED, name, COL_RESET);
    return 1;
}

/* =========================================================================
 * merge --abort
 * ====================================================================== */

int merge_abort(void)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return 0;
    }
    if (!file_exists(REPO_MERGE_HEAD_FILE))
    {
        log_msg(LOG_ERROR, "No merge in progress.");
        return 0;
    }

    /* Restore working tree to pre-merge HEAD */
    char commit_id[MAX_COMMIT_ID] = "";
    get_current_commit_id(commit_id, sizeof(commit_id));
    if (commit_id[0])
    {
        if (!restore_snapshot_to_working_tree(commit_id))
        {
            log_msg(LOG_ERROR, "Failed to restore pre-merge state.");
            return 0;
        }
    }

    remove(REPO_MERGE_HEAD_FILE);
    remove(REPO_MERGE_MSG_FILE);
    clear_index();

    log_msg(LOG_INFO, "%sMerge aborted.%s Working tree restored to HEAD.", COL_YELLOW, COL_RESET);
    return 1;
}

/* =========================================================================
 * cherry-pick
 * ====================================================================== */

int cherry_pick(const char *commit_id)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return 0;
    }
    if (!commit_id || !commit_id[0])
    {
        log_msg(LOG_ERROR, "Usage: cherry-pick <commit-id>");
        return 0;
    }

    CommitMetadata meta;
    if (!read_commit_metadata(commit_id, &meta))
    {
        log_msg(LOG_ERROR, "Commit '%s' not found.", commit_id);
        return 0;
    }

    /* Check clean index */
    IndexEntry *idx = malloc(sizeof(IndexEntry) * MAX_TREE_ENTRIES);
    if (idx)
    {
        int s = read_index_entries(idx, MAX_TREE_ENTRIES);
        free(idx);
        if (s > 0)
        {
            log_msg(LOG_ERROR, "Staged changes present. Commit or stash first.");
            return 0;
        }
    }

    char snap[MAX_PATH_LEN];
    path_join(REPO_OBJECTS_DIR, commit_id, snap, sizeof(snap));
    if (!directory_exists(snap))
    {
        log_msg(LOG_ERROR, "No snapshot for commit '%s'.", commit_id);
        return 0;
    }

    /* Get parent snapshot to diff against */
    char parent_snap[MAX_PATH_LEN] = "";
    if (meta.parent_id[0])
        path_join(REPO_OBJECTS_DIR, meta.parent_id, parent_snap, sizeof(parent_snap));

    /* Collect files in picked commit */
    char (*files)[MAX_PATH_LEN] = malloc(MAX_TREE_ENTRIES * MAX_PATH_LEN);
    if (!files)
    {
        log_msg(LOG_ERROR, "Out of memory.");
        return 0;
    }
    int fc = read_commit_tree(commit_id, files, MAX_TREE_ENTRIES);

    IndexEntry *new_entries = malloc(sizeof(IndexEntry) * MAX_TREE_ENTRIES);
    if (!new_entries)
    {
        free(files);
        return 0;
    }
    int ne = 0, conflicts = 0;

    for (int i = 0; i < fc; i++)
    {
        char src[MAX_PATH_LEN], psrc[MAX_PATH_LEN];
        path_join(snap, files[i], src, sizeof(src));

        int was_in_parent = 0;
        if (parent_snap[0])
        {
            path_join(parent_snap, files[i], psrc, sizeof(psrc));
            was_in_parent = file_exists(psrc);
        }

        /* If file changed between parent and this commit, apply it */
        if (!was_in_parent || compare_files(psrc, src))
        {
            int wt_exists = file_exists(files[i]);
            int wt_differs = wt_exists && was_in_parent && compare_files(psrc, files[i]);

            if (wt_differs)
            {
                /* Conflict: working tree already diverged */
                FILE *cf = fopen(files[i], "a");
                if (cf)
                {
                    fprintf(cf, "\n<<<<<<< HEAD\n");
                    fprintf(cf, "======= (cherry-pick %.8s)\n", commit_id);
                    FILE *sf = fopen(src, "r");
                    if (sf)
                    {
                        char line[MAX_INPUT];
                        while (fgets(line, sizeof(line), sf))
                            fputs(line, cf);
                        fclose(sf);
                    }
                    fprintf(cf, ">>>>>>> %.8s\n", commit_id);
                    fclose(cf);
                }
                log_msg(LOG_WARN, "%sCONFLICT%s in '%s'", COL_RED, COL_RESET, files[i]);
                conflicts++;
            }
            else
                copy_file(src, files[i]);

            new_entries[ne].action = 'A';
            strncpy(new_entries[ne].path, files[i], MAX_PATH_LEN - 1);
            ne++;
        }
    }

    /* Check for deletions (files in parent but not in picked commit) */
    if (parent_snap[0])
    {
        char (*pfiles)[MAX_PATH_LEN] = malloc(MAX_TREE_ENTRIES * MAX_PATH_LEN);
        if (pfiles)
        {
            size_t pc = 0;
            collect_tree_paths(parent_snap, "", pfiles, MAX_TREE_ENTRIES, &pc);
            for (size_t i = 0; i < pc; i++)
            {
                char in_pick[MAX_PATH_LEN];
                path_join(snap, pfiles[i], in_pick, sizeof(in_pick));
                if (!file_exists(in_pick) && file_exists(pfiles[i]))
                {
                    remove(pfiles[i]);
                    new_entries[ne].action = 'D';
                    strncpy(new_entries[ne].path, pfiles[i], MAX_PATH_LEN - 1);
                    ne++;
                }
            }
            free(pfiles);
        }
    }

    write_index_entries(new_entries, ne);
    free(new_entries);
    free(files);

    if (conflicts)
    {
        log_msg(LOG_WARN, "%d conflict(s). Resolve then commit.", conflicts);
        return 0;
    }

    /* Auto-commit with original message */
    char pick_msg[MAX_MESSAGE_LEN];
    snprintf(pick_msg, sizeof(pick_msg), "%s", meta.message);
    commit_changes(pick_msg);
    log_msg(LOG_INFO, "%sCherry-picked [%.8s]%s: %s", COL_GREEN, commit_id, COL_RESET, meta.message);
    return 1;
}

/* =========================================================================
 * blame
 * ====================================================================== */

void blame_file(const char *path)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return;
    }
    if (!path || !path[0])
    {
        log_msg(LOG_ERROR, "Usage: blame <file>");
        return;
    }
    if (!file_exists(path))
    {
        log_msg(LOG_ERROR, "File '%s' does not exist.", path);
        return;
    }

    /* Walk history oldest->newest building line ownership map */
    /* First collect full commit chain newest->oldest */
    char chain[MAX_BISECT_COMMITS][MAX_COMMIT_ID];
    int chain_len = 0;
    char cur[MAX_COMMIT_ID] = "";
    get_current_commit_id(cur, sizeof(cur));
    while (cur[0] && chain_len < MAX_BISECT_COMMITS)
    {
        strncpy(chain[chain_len++], cur, MAX_COMMIT_ID - 1);
        CommitMetadata m;
        if (!read_commit_metadata(cur, &m) || !m.parent_id[0])
            break;
        strncpy(cur, m.parent_id, sizeof(cur) - 1);
    }

    /* Read current file lines */
    FILE *wf = fopen(path, "r");
    if (!wf)
    {
        log_msg(LOG_ERROR, "Cannot open '%s'.", path);
        return;
    }
    char lines[4096][MAX_INPUT];
    char blame_commit[4096][MAX_COMMIT_ID];
    char blame_author[4096][64];
    char blame_date[4096][32];
    int line_count = 0;
    while (line_count < 4096 && fgets(lines[line_count], MAX_INPUT, wf))
    {
        strncpy(blame_commit[line_count], "(uncommitted)", MAX_COMMIT_ID - 1);
        strncpy(blame_author[line_count], "?", 63);
        strncpy(blame_date[line_count], "?", 31);
        line_count++;
    }
    fclose(wf);

    /* Walk from oldest commit forward, updating blame for changed lines */
    for (int ci = chain_len - 1; ci >= 0; ci--)
    {
        CommitMetadata m;
        if (!read_commit_metadata(chain[ci], &m))
            continue;

        char snap_file[MAX_PATH_LEN], snap[MAX_PATH_LEN];
        path_join(REPO_OBJECTS_DIR, chain[ci], snap, sizeof(snap));
        path_join(snap, path, snap_file, sizeof(snap_file));
        if (!file_exists(snap_file))
            continue;

        /* Get parent version */
        char psnap_file[MAX_PATH_LEN] = "";
        if (m.parent_id[0])
        {
            char psnap[MAX_PATH_LEN];
            path_join(REPO_OBJECTS_DIR, m.parent_id, psnap, sizeof(psnap));
            path_join(psnap, path, psnap_file, sizeof(psnap_file));
        }

        FILE *sf = fopen(snap_file, "r");
        if (!sf)
            continue;
        FILE *pf = psnap_file[0] ? fopen(psnap_file, "r") : NULL;

        char sline[MAX_INPUT], pline[MAX_INPUT];
        int ln = 0;
        while (ln < line_count && fgets(sline, sizeof(sline), sf))
        {
            int pgot = pf ? (fgets(pline, sizeof(pline), pf) != NULL) : 0;
            /* If line differs from parent (or no parent), this commit introduced it */
            if (!pgot || strcmp(sline, pline) != 0)
            {
                strncpy(blame_commit[ln], chain[ci], MAX_COMMIT_ID - 1);
                strncpy(blame_author[ln], m.author[0] ? m.author : "?", 63);
                /* date: first 10 chars of m.date */
                strncpy(blame_date[ln], m.date, 10);
                blame_date[ln][10] = '\0';
            }
            ln++;
        }
        fclose(sf);
        if (pf)
            fclose(pf);
    }

    /* Print blame output */
    printf("%s%-20s %-16s %-10s  %s%s\n",
           COL_BOLD, "commit", "author", "date", "content", COL_RESET);
    printf("%s%s%s\n", COL_DIM,
           "--------------------+----------------+----------+------------------",
           COL_RESET);
    for (int i = 0; i < line_count; i++)
    {
        char short_id[10] = "";
        strncpy(short_id, blame_commit[i], 8);
        /* Strip trailing newline from line */
        char display[MAX_INPUT];
        strncpy(display, lines[i], sizeof(display) - 1);
        display[strcspn(display, "\r\n")] = '\0';
        printf("%s%-8s%s  %s%-14.14s%s  %s%-10s%s  %s\n",
               COL_YELLOW, short_id, COL_RESET,
               COL_CYAN, blame_author[i], COL_RESET,
               COL_DIM, blame_date[i], COL_RESET,
               display);
    }
}

/* =========================================================================
 * bisect
 * ====================================================================== */

/* Find midpoint commit between two commits in history */
static int find_bisect_mid(const char *bad, const char *good,
                           char *mid_out, size_t mid_size)
{
    /* Walk from bad commit, collect all ancestors */
    char chain[MAX_BISECT_COMMITS][MAX_COMMIT_ID];
    int chain_len = 0;
    char cur[MAX_COMMIT_ID];
    strncpy(cur, bad, sizeof(cur) - 1);
    while (cur[0] && chain_len < MAX_BISECT_COMMITS)
    {
        strncpy(chain[chain_len++], cur, MAX_COMMIT_ID - 1);
        if (!strcmp(cur, good))
            break;
        CommitMetadata m;
        if (!read_commit_metadata(cur, &m) || !m.parent_id[0])
            break;
        strncpy(cur, m.parent_id, sizeof(cur) - 1);
    }
    if (chain_len < 2)
    {
        strncpy(mid_out, bad, mid_size - 1);
        return chain_len;
    }
    int mid = chain_len / 2;
    strncpy(mid_out, chain[mid], mid_size - 1);
    mid_out[mid_size - 1] = '\0';
    return chain_len;
}

void bisect_start(void)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return;
    }
    create_directory(REPO_BISECT_DIR);
    write_text_file(REPO_BISECT_LOG_FILE, "");
    write_text_file(REPO_BISECT_GOOD_FILE, "");
    write_text_file(REPO_BISECT_BAD_FILE, "");

    /* Save current HEAD so we can restore on reset */
    char cur[MAX_COMMIT_ID] = "";
    get_current_commit_id(cur, sizeof(cur));
    write_text_file(REPO_BISECT_HEAD_FILE, cur);

    log_msg(LOG_INFO, "%sBisect started.%s Mark commits with 'bisect good <id>' and 'bisect bad <id>'.",
            COL_CYAN, COL_RESET);
}

void bisect_good(const char *commit_id)
{
    if (!directory_exists(REPO_BISECT_DIR))
    {
        log_msg(LOG_ERROR, "Run 'bisect start' first.");
        return;
    }

    char cid[MAX_COMMIT_ID] = "";
    if (commit_id && commit_id[0])
        strncpy(cid, commit_id, sizeof(cid) - 1);
    else
        get_current_commit_id(cid, sizeof(cid));

    if (!cid[0])
    {
        log_msg(LOG_ERROR, "No commit to mark as good.");
        return;
    }
    write_text_file(REPO_BISECT_GOOD_FILE, cid);
    append_text_file(REPO_BISECT_LOG_FILE, "good ");
    append_text_file(REPO_BISECT_LOG_FILE, cid);
    append_text_file(REPO_BISECT_LOG_FILE, "\n");
    log_msg(LOG_INFO, "%sMarked [%.8s] as good.%s", COL_GREEN, cid, COL_RESET);

    /* Try to step */
    char bad[MAX_COMMIT_ID] = "";
    read_text_file(REPO_BISECT_BAD_FILE, bad, sizeof(bad));
    if (bad[0])
    {
        char mid[MAX_COMMIT_ID] = "";
        int steps = find_bisect_mid(bad, cid, mid, sizeof(mid));
        if (steps <= 2)
        {
            log_msg(LOG_INFO, "%sBisect complete.%s First bad commit is [%.8s].", COL_RED, COL_RESET, bad);
            return;
        }
        log_msg(LOG_INFO, "Checking [%.8s]... (~%d steps remaining)", mid, steps / 2);
        restore_snapshot_to_working_tree(mid);
        write_text_file(REPO_BISECT_HEAD_FILE, mid);
    }
}

void bisect_bad(const char *commit_id)
{
    if (!directory_exists(REPO_BISECT_DIR))
    {
        log_msg(LOG_ERROR, "Run 'bisect start' first.");
        return;
    }

    char cid[MAX_COMMIT_ID] = "";
    if (commit_id && commit_id[0])
        strncpy(cid, commit_id, sizeof(cid) - 1);
    else
        get_current_commit_id(cid, sizeof(cid));

    if (!cid[0])
    {
        log_msg(LOG_ERROR, "No commit to mark as bad.");
        return;
    }
    write_text_file(REPO_BISECT_BAD_FILE, cid);
    append_text_file(REPO_BISECT_LOG_FILE, "bad ");
    append_text_file(REPO_BISECT_LOG_FILE, cid);
    append_text_file(REPO_BISECT_LOG_FILE, "\n");
    log_msg(LOG_INFO, "%sMarked [%.8s] as bad.%s", COL_RED, cid, COL_RESET);

    char good[MAX_COMMIT_ID] = "";
    read_text_file(REPO_BISECT_GOOD_FILE, good, sizeof(good));
    if (good[0])
    {
        char mid[MAX_COMMIT_ID] = "";
        int steps = find_bisect_mid(cid, good, mid, sizeof(mid));
        if (steps <= 2)
        {
            log_msg(LOG_INFO, "%sBisect complete.%s First bad commit is [%.8s].", COL_RED, COL_RESET, cid);
            return;
        }
        log_msg(LOG_INFO, "Checking [%.8s]... (~%d steps remaining)", mid, steps / 2);
        restore_snapshot_to_working_tree(mid);
        write_text_file(REPO_BISECT_HEAD_FILE, mid);
    }
}

void bisect_reset(void)
{
    if (!directory_exists(REPO_BISECT_DIR))
    {
        log_msg(LOG_WARN, "No bisect in progress.");
        return;
    }

    char original[MAX_COMMIT_ID] = "";
    read_text_file(REPO_BISECT_HEAD_FILE, original, sizeof(original));
    if (original[0])
    {
        restore_snapshot_to_working_tree(original);
        log_msg(LOG_INFO, "Restored working tree to [%.8s].", original);
    }

    remove_path_recursive(REPO_BISECT_DIR);
    log_msg(LOG_INFO, "%sBisect reset.%s", COL_GREEN, COL_RESET);
}

/* =========================================================================
 * grep
 * ====================================================================== */

static int grep_file(const char *filepath, const char *pattern, int ignore_case)
{
    FILE *f = fopen(filepath, "r");
    if (!f)
        return 0;
    char line[MAX_INPUT];
    int ln = 0, found = 0;
    while (fgets(line, sizeof(line), f))
    {
        ln++;
        char haystack[MAX_INPUT], needle[MAX_INPUT];
        strncpy(haystack, line, sizeof(haystack) - 1);
        strncpy(needle, pattern, sizeof(needle) - 1);
        if (ignore_case)
        {
            for (char *p = haystack; *p; p++)
                *p = (char)tolower((unsigned char)*p);
            for (char *p = needle; *p; p++)
                *p = (char)tolower((unsigned char)*p);
        }
        if (strstr(haystack, needle))
        {
            line[strcspn(line, "\r\n")] = '\0';
            printf("%s%s%s:%s%d%s: %s\n",
                   COL_MAGENTA, filepath, COL_RESET,
                   COL_YELLOW, ln, COL_RESET,
                   line);
            found++;
        }
    }
    fclose(f);
    return found;
}

void grep_repo(const char *pattern, int ignore_case)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return;
    }
    if (!pattern || !pattern[0])
    {
        log_msg(LOG_ERROR, "Usage: grep <pattern> [-i]");
        return;
    }

    char (*paths)[MAX_PATH_LEN] = malloc(MAX_TREE_ENTRIES * MAX_PATH_LEN);
    if (!paths)
        return;
    size_t count = 0;
    collect_tree_paths(".", "", paths, MAX_TREE_ENTRIES, &count);

    int total = 0;
    for (size_t i = 0; i < count; i++)
    {
        if (should_ignore_file(paths[i]))
            continue;
        if (is_binary_file(paths[i]))
            continue;
        total += grep_file(paths[i], pattern, ignore_case);
    }

    if (!total)
        printf("%s(no matches for '%s')%s\n", COL_DIM, pattern, COL_RESET);
    else
        printf("\n%s%d match%s%s\n", COL_BOLD, total, total == 1 ? "" : "es", COL_RESET);
    free(paths);
}

/* =========================================================================
 * stash apply  (restore without dropping)
 * ====================================================================== */

int stash_apply(void)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return 0;
    }

    FILE *f = fopen(REPO_STASH_INDEX_FILE, "r");
    if (!f)
    {
        log_msg(LOG_WARN, "No stash entries.");
        return 0;
    }
    char lines[MAX_STASH_ENTRIES][MAX_INPUT];
    int n = 0;
    while (n < MAX_STASH_ENTRIES && fgets(lines[n], sizeof(lines[n]), f))
    {
        trim_whitespace(lines[n]);
        if (lines[n][0])
            n++;
    }
    fclose(f);
    if (!n)
    {
        log_msg(LOG_WARN, "No stash entries.");
        return 0;
    }

    char last[MAX_INPUT];
    strncpy(last, lines[n - 1], sizeof(last) - 1);
    strtok(last, "|");
    char *sid = strtok(NULL, "|");
    char stash_id[64] = "";
    if (sid)
        strncpy(stash_id, sid, sizeof(stash_id) - 1);
    char *msg_tok = strtok(NULL, "|");
    char msg[MAX_MESSAGE_LEN] = "";
    if (msg_tok)
        strncpy(msg, msg_tok, sizeof(msg) - 1);

    char stash_snap[MAX_PATH_LEN];
    path_join(REPO_STASH_DIR, stash_id, stash_snap, sizeof(stash_snap));
    if (!directory_exists(stash_snap))
    {
        log_msg(LOG_ERROR, "Stash snapshot missing.");
        return 0;
    }

    if (!copy_directory_recursive(stash_snap, "."))
    {
        log_msg(LOG_ERROR, "Failed to apply stash.");
        return 0;
    }

    log_msg(LOG_INFO, "%sApplied stash%s (not dropped): %s", COL_GREEN, COL_RESET, msg);
    log_msg(LOG_INFO, "Run 'stash drop' to remove it.");
    return 1;
}

/* =========================================================================
 * tag -d  (delete tag)
 * ====================================================================== */

void delete_tag(const char *tag_name)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return;
    }
    if (!tag_name || !tag_name[0])
    {
        log_msg(LOG_ERROR, "Usage: tag -d <name>");
        return;
    }

    char ref[MAX_PATH_LEN];
    path_join(REPO_REFS_TAGS_DIR, tag_name, ref, sizeof(ref));
    if (!file_exists(ref))
    {
        log_msg(LOG_ERROR, "Tag '%s' does not exist.", tag_name);
        return;
    }
    if (remove(ref) != 0)
    {
        log_msg(LOG_ERROR, "Failed to delete tag '%s': %s", tag_name, strerror(errno));
        return;
    }
    log_msg(LOG_INFO, "%sDeleted tag '%s'.%s", COL_RED, tag_name, COL_RESET);
}

/* =========================================================================
 * log --all  (show commits across all branches)
 * ====================================================================== */

/* Extend show_log to accept --all: walk every branch head */
static int was_visited_fn(char visited[][MAX_COMMIT_ID], int vcount, const char *id)
{
    for (int i = 0; i < vcount; i++)
        if (!strcmp(visited[i], id))
            return 1;
    return 0;
}

static void show_log_all(int oneline)
{
    if (!directory_exists(REPO_DIR))
    {
        log_msg(LOG_ERROR, "Not a MyGit repository.");
        return;
    }

    /* Collect all branch commit IDs */
    char visited[MAX_BISECT_COMMITS][MAX_COMMIT_ID];
    int vcount = 0;

/* Helper: print chain from a commit, skipping already-visited */
#define MARK_VISITED(id)                                         \
    do                                                           \
    {                                                            \
        if (vcount < MAX_BISECT_COMMITS)                         \
            strncpy(visited[vcount++], (id), MAX_COMMIT_ID - 1); \
    } while (0)

/* Standard C: inline helper via temp variable */
#define WAS_VISITED(id) was_visited_fn(visited, vcount, (id))

    /* Iterate all branch refs */
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    char pat[MAX_PATH_LEN];
    snprintf(pat, sizeof(pat), "%s\\*", REPO_REFS_HEADS_DIR);
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        printf("No branches.\n");
        return;
    }
    do
    {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
            continue;
        char ref[MAX_PATH_LEN], branch_head[MAX_COMMIT_ID] = "";
        path_join(REPO_REFS_HEADS_DIR, fd.cFileName, ref, sizeof(ref));
        read_text_file(ref, branch_head, sizeof(branch_head));
        trim_whitespace(branch_head);
        if (!branch_head[0])
            continue;
        printf("%s--- branch: %s ---%s\n", COL_BLUE, fd.cFileName, COL_RESET);
        char walk[MAX_COMMIT_ID];
        strncpy(walk, branch_head, sizeof(walk) - 1);
        while (walk[0] && !WAS_VISITED(walk))
        {
            MARK_VISITED(walk);
            CommitMetadata m;
            if (!read_commit_metadata(walk, &m))
                break;
            if (oneline)
                printf("  %s%.8s%s %s\n", COL_YELLOW, m.commit_id, COL_RESET, m.message);
            else
                printf("  %s%.8s%s [%s] %s — %s\n", COL_YELLOW, m.commit_id, COL_RESET,
                       m.date, m.author[0] ? m.author : "?", m.message);
            if (!m.parent_id[0])
                break;
            strncpy(walk, m.parent_id, sizeof(walk) - 1);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *dir = opendir(REPO_REFS_HEADS_DIR);
    if (!dir)
    {
        printf("No branches.\n");
        return;
    }
    struct dirent *e;
    while ((e = readdir(dir)) != NULL)
    {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char ref[MAX_PATH_LEN], branch_head[MAX_COMMIT_ID] = "";
        path_join(REPO_REFS_HEADS_DIR, e->d_name, ref, sizeof(ref));
        read_text_file(ref, branch_head, sizeof(branch_head));
        trim_whitespace(branch_head);
        if (!branch_head[0])
            continue;
        printf("%s--- branch: %s ---%s\n", COL_BLUE, e->d_name, COL_RESET);
        char walk[MAX_COMMIT_ID];
        strncpy(walk, branch_head, sizeof(walk) - 1);
        while (walk[0] && !WAS_VISITED(walk))
        {
            MARK_VISITED(walk);
            CommitMetadata m;
            if (!read_commit_metadata(walk, &m))
                break;
            if (oneline)
                printf("  %s%.8s%s %s\n", COL_YELLOW, m.commit_id, COL_RESET, m.message);
            else
                printf("  %s%.8s%s [%s] %s — %s\n", COL_YELLOW, m.commit_id, COL_RESET,
                       m.date, m.author[0] ? m.author : "?", m.message);
            if (!m.parent_id[0])
                break;
            strncpy(walk, m.parent_id, sizeof(walk) - 1);
        }
    }
    closedir(dir);
#endif
#undef MARK_VISITED
#undef WAS_VISITED
}