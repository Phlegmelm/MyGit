#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "fs_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h> /* _isatty, _fileno */
#else
#include <unistd.h> /* isatty, fileno */
#endif

int g_debug_enabled = 0;
int g_color_enabled = 0;

/* -------------------------------------------------------------------------
 * Colour init
 * ---------------------------------------------------------------------- */

void init_color_support(void)
{
#ifdef _WIN32
    /* Enable VT100 on Windows 10+ */
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != INVALID_HANDLE_VALUE)
    {
        DWORD mode = 0;
        if (GetConsoleMode(h, &mode))
        {
            SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
            g_color_enabled = 1;
        }
    }
#else
    if (isatty(fileno(stdout)))
        g_color_enabled = 1;
#endif
    /* MYGIT_COLOR=0 disables, MYGIT_COLOR=1 forces on */
    const char *env = getenv("MYGIT_COLOR");
    if (env)
        g_color_enabled = (strcmp(env, "0") != 0);
}

/* -------------------------------------------------------------------------
 * Logging
 * ---------------------------------------------------------------------- */

static const char *level_tag(LogLevel level)
{
    switch (level)
    {
    case LOG_DEBUG:
        return "DEBUG";
    case LOG_INFO:
        return "INFO ";
    case LOG_WARN:
        return "WARN ";
    case LOG_ERROR:
        return "ERROR";
    default:
        return "?????";
    }
}

static const char *level_color(LogLevel level)
{
    switch (level)
    {
    case LOG_DEBUG:
        return COL_DIM;
    case LOG_INFO:
        return COL_CYAN;
    case LOG_WARN:
        return COL_YELLOW;
    case LOG_ERROR:
        return COL_RED;
    default:
        return "";
    }
}

void log_msg(LogLevel level, const char *fmt, ...)
{
    if (!fmt)
        return;

    time_t now = time(NULL);
    struct tm tm_info;
#ifdef _WIN32
    localtime_s(&tm_info, &now);
#else
    localtime_r(&now, &tm_info);
#endif
    char ts[24];
    strftime(ts, sizeof(ts), "%H:%M:%S", &tm_info);

    /* Always write everything to debug.log */
    FILE *dbg = fopen(REPO_DEBUG_LOG_FILE, "a");
    if (dbg)
    {
        fprintf(dbg, "[%s][%s] ", ts, level_tag(level));
        va_list ap;
        va_start(ap, fmt);
        vfprintf(dbg, fmt, ap);
        va_end(ap);
        fprintf(dbg, "\n");
        fclose(dbg);
    }

    if (level == LOG_DEBUG && !g_debug_enabled)
        return;

    FILE *dest = (level == LOG_ERROR || level == LOG_WARN) ? stderr : stdout;
    fprintf(dest, "%s[%s][%s]%s ",
            level_color(level), ts, level_tag(level), COL_RESET);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(dest, fmt, ap);
    va_end(ap);
    fprintf(dest, "\n");
}

void print_error(const char *message)
{
    if (message)
        log_msg(LOG_ERROR, "%s", message);
}

/* -------------------------------------------------------------------------
 * String helpers
 * ---------------------------------------------------------------------- */

void trim_whitespace(char *text)
{
    if (!text)
        return;
    char *start = text;
    while (*start && isspace((unsigned char)*start))
        start++;
    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1)))
        end--;
    *end = '\0';
    if (start != text)
        memmove(text, start, (size_t)(end - start + 1));
}

void normalize_path(char *path)
{
    if (!path)
        return;
    for (char *p = path; *p; ++p)
        if (*p == '\\')
            *p = '/';
}

/*
 * Recursive glob matching.
 * Supports: * (any sequence), ? (any single char), prefix*, *suffix, dir/
 */
int match_pattern(const char *pattern, const char *str)
{
    if (!pattern || !str)
        return 0;

    if (*pattern == '*')
    {
        if (pattern[1] == '\0')
            return 1;
        while (*str)
        {
            if (match_pattern(pattern + 1, str))
                return 1;
            str++;
        }
        return match_pattern(pattern + 1, str);
    }

    if (*pattern == '?' && *str)
        return match_pattern(pattern + 1, str + 1);

    if (*pattern == '\0' && *str == '\0')
        return 1;
    if (*pattern == '\0' || *str == '\0')
        return 0;
    if (*pattern == *str)
        return match_pattern(pattern + 1, str + 1);

    return 0;
}

/* -------------------------------------------------------------------------
 * File I/O
 * ---------------------------------------------------------------------- */

int read_text_file(const char *path, char *output, size_t size)
{
    if (!path || !output || size == 0)
        return 0;
    FILE *file = fopen(path, "r");
    if (!file)
    {
        log_msg(LOG_DEBUG, "read_text_file: cannot open '%s': %s", path, strerror(errno));
        return 0;
    }
    if (fgets(output, (int)size, file) == NULL)
        output[0] = '\0';
    output[strcspn(output, "\r\n")] = '\0';
    fclose(file);
    return 1;
}

int write_text_file(const char *path, const char *contents)
{
    if (!path)
        return 0;
    if (!create_parent_directories(path))
        return 0;
    FILE *file = fopen(path, "w");
    if (!file)
    {
        log_msg(LOG_ERROR, "write_text_file: cannot open '%s' for writing: %s",
                path, strerror(errno));
        return 0;
    }
    if (contents)
        fputs(contents, file);
    fclose(file);
    return 1;
}

int append_text_file(const char *path, const char *contents)
{
    if (!path)
        return 0;
    FILE *file = fopen(path, "a");
    if (!file)
    {
        log_msg(LOG_DEBUG, "append_text_file: cannot open '%s': %s", path, strerror(errno));
        return 0;
    }
    if (contents)
        fputs(contents, file);
    fclose(file);
    return 1;
}

/* -------------------------------------------------------------------------
 * Path helpers
 * ---------------------------------------------------------------------- */

int path_join(const char *base, const char *name, char *out, size_t size)
{
    if (!base || !name || !out || size == 0)
        return 0;
    size_t blen = strlen(base);
    size_t nlen = strlen(name);
    if (blen + 1 + nlen + 1 > size)
    {
        log_msg(LOG_WARN, "path_join: path too long ('%s' + '%s')", base, name);
        return 0;
    }
    memcpy(out, base, blen);
    if (blen > 0 && out[blen - 1] != '/' && out[blen - 1] != '\\')
        out[blen++] = '/';
    memcpy(out + blen, name, nlen + 1);
    normalize_path(out);
    return 1;
}

/* -------------------------------------------------------------------------
 * Filesystem queries
 * ---------------------------------------------------------------------- */

int directory_exists(const char *path)
{
    if (!path)
        return 0;
#ifdef _WIN32
    struct _stat st;
    return _stat(path, &st) == 0 && IS_DIRECTORY(st.st_mode);
#else
    struct stat st;
    return stat(path, &st) == 0 && IS_DIRECTORY(st.st_mode);
#endif
}

int file_exists(const char *path)
{
    if (!path)
        return 0;
#ifdef _WIN32
    struct _stat st;
    return _stat(path, &st) == 0 && !IS_DIRECTORY(st.st_mode);
#else
    struct stat st;
    return stat(path, &st) == 0 && !IS_DIRECTORY(st.st_mode);
#endif
}

long file_size(const char *path)
{
    if (!path)
        return -1;
#ifdef _WIN32
    struct _stat st;
    if (_stat(path, &st) != 0)
        return -1;
#else
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
#endif
    return (long)st.st_size;
}

/* -------------------------------------------------------------------------
 * Filesystem mutation
 * ---------------------------------------------------------------------- */

int create_directory(const char *path)
{
    if (!path)
        return 0;
    if (directory_exists(path))
        return 1;
    if (MAKE_DIR(path) != 0)
    {
        if (errno == EEXIST)
            return directory_exists(path);
        log_msg(LOG_ERROR, "create_directory: mkdir '%s' failed: %s", path, strerror(errno));
        return 0;
    }
    log_msg(LOG_DEBUG, "create_directory: created '%s'", path);
    return 1;
}

int create_parent_directories(const char *path)
{
    if (!path)
        return 0;
    char buf[MAX_PATH_LEN];
    size_t len = strlen(path);
    if (len >= sizeof(buf))
        return 0;
    strncpy(buf, path, sizeof(buf));
    normalize_path(buf);
    char *sep = strrchr(buf, '/');
    if (!sep)
        return 1;
    *sep = '\0';
    for (char *p = buf + 1; *p; ++p)
    {
        if (*p == '/')
        {
            *p = '\0';
            if (!directory_exists(buf) && !create_directory(buf))
                return 0;
            *p = '/';
        }
    }
    if (!directory_exists(buf))
        return create_directory(buf);
    return 1;
}

int copy_file(const char *src, const char *dst)
{
    if (!src || !dst)
        return 0;
    if (!create_parent_directories(dst))
        return 0;
    FILE *s = fopen(src, "rb");
    if (!s)
    {
        log_msg(LOG_ERROR, "copy_file: cannot open source '%s': %s", src, strerror(errno));
        return 0;
    }
    FILE *d = fopen(dst, "wb");
    if (!d)
    {
        log_msg(LOG_ERROR, "copy_file: cannot open dest '%s': %s", dst, strerror(errno));
        fclose(s);
        return 0;
    }
    char buf[8192];
    size_t n;
    int ok = 1;
    while ((n = fread(buf, 1, sizeof(buf), s)) > 0)
    {
        if (fwrite(buf, 1, n, d) != n)
        {
            log_msg(LOG_ERROR, "copy_file: write error '%s': %s", dst, strerror(errno));
            ok = 0;
            break;
        }
    }
    fclose(s);
    fclose(d);
    if (!ok)
        remove(dst);
    return ok;
}

/* -------------------------------------------------------------------------
 * Platform-specific: directory recursion
 * ---------------------------------------------------------------------- */

#ifdef _WIN32

int copy_directory_recursive(const char *src_dir, const char *dst_dir)
{
    char search[MAX_PATH_LEN];
    snprintf(search, sizeof(search), "%s\\*", src_dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 1;
    if (!create_directory(dst_dir))
    {
        FindClose(h);
        return 0;
    }
    do
    {
        const char *name = fd.cFileName;
        if (!strcmp(name, ".") || !strcmp(name, ".."))
            continue;
        char cs[MAX_PATH_LEN], cd[MAX_PATH_LEN];
        path_join(src_dir, name, cs, sizeof(cs));
        path_join(dst_dir, name, cd, sizeof(cd));
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (!copy_directory_recursive(cs, cd))
            {
                FindClose(h);
                return 0;
            }
        }
        else
        {
            if (!copy_file(cs, cd))
            {
                FindClose(h);
                return 0;
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 1;
}

int remove_path_recursive(const char *path)
{
    struct _stat st;
    if (_stat(path, &st) != 0)
        return 1;
    if (!IS_DIRECTORY(st.st_mode))
        return remove(path) == 0;
    char search[MAX_PATH_LEN];
    snprintf(search, sizeof(search), "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return REMOVE_DIR(path) == 0;
    do
    {
        const char *name = fd.cFileName;
        if (!strcmp(name, ".") || !strcmp(name, ".."))
            continue;
        char child[MAX_PATH_LEN];
        path_join(path, name, child, sizeof(child));
        if (!remove_path_recursive(child))
        {
            FindClose(h);
            return 0;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return REMOVE_DIR(path) == 0;
}

int collect_tree_paths(const char *root_dir, const char *cur,
                       char paths[][MAX_PATH_LEN], size_t max, size_t *count)
{
    char dir[MAX_PATH_LEN];
    if (!cur || cur[0] == '\0')
        strncpy(dir, root_dir, sizeof(dir) - 1);
    else
        path_join(root_dir, cur, dir, sizeof(dir));
    dir[MAX_PATH_LEN - 1] = '\0';

    char search[MAX_PATH_LEN];
    size_t dl = strlen(dir);
    if (dl + 3 >= sizeof(search))
        return 0;
    memcpy(search, dir, dl);
    search[dl] = '\\';
    search[dl + 1] = '*';
    search[dl + 2] = '\0';

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 1;
    do
    {
        const char *name = fd.cFileName;
        if (!strcmp(name, ".") || !strcmp(name, "..") || !strcmp(name, ".mygit"))
            continue;
        char rel[MAX_PATH_LEN];
        if (!cur || cur[0] == '\0')
            snprintf(rel, sizeof(rel), "%s", name);
        else
            snprintf(rel, sizeof(rel), "%s/%s", cur, name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (!collect_tree_paths(root_dir, rel, paths, max, count))
            {
                FindClose(h);
                return 0;
            }
        }
        else
        {
            if (*count >= max)
            {
                FindClose(h);
                return 0;
            }
            strncpy(paths[*count], rel, MAX_PATH_LEN - 1);
            paths[*count][MAX_PATH_LEN - 1] = '\0';
            (*count)++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 1;
}

#else /* POSIX */

#include <dirent.h>
#include <unistd.h>

int copy_directory_recursive(const char *src_dir, const char *dst_dir)
{
    DIR *dir = opendir(src_dir);
    if (!dir)
        return 1;
    if (!create_directory(dst_dir))
    {
        closedir(dir);
        return 0;
    }
    struct dirent *e;
    while ((e = readdir(dir)) != NULL)
    {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char cs[MAX_PATH_LEN], cd[MAX_PATH_LEN];
        path_join(src_dir, e->d_name, cs, sizeof(cs));
        path_join(dst_dir, e->d_name, cd, sizeof(cd));
        struct stat st;
        if (stat(cs, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
        {
            if (!copy_directory_recursive(cs, cd))
            {
                closedir(dir);
                return 0;
            }
        }
        else
        {
            if (!copy_file(cs, cd))
            {
                closedir(dir);
                return 0;
            }
        }
    }
    closedir(dir);
    return 1;
}

int remove_path_recursive(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return 1;
    if (!S_ISDIR(st.st_mode))
        return remove(path) == 0;
    DIR *dir = opendir(path);
    if (!dir)
        return rmdir(path) == 0;
    struct dirent *e;
    while ((e = readdir(dir)) != NULL)
    {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char child[MAX_PATH_LEN];
        path_join(path, e->d_name, child, sizeof(child));
        if (!remove_path_recursive(child))
        {
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);
    return rmdir(path) == 0;
}

int collect_tree_paths(const char *root_dir, const char *cur,
                       char paths[][MAX_PATH_LEN], size_t max, size_t *count)
{
    char dir[MAX_PATH_LEN];
    if (!cur || cur[0] == '\0')
        strncpy(dir, root_dir, sizeof(dir) - 1);
    else
        path_join(root_dir, cur, dir, sizeof(dir));
    dir[MAX_PATH_LEN - 1] = '\0';

    DIR *d = opendir(dir);
    if (!d)
        return 1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
    {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") ||
            !strcmp(e->d_name, ".mygit"))
            continue;
        char rel[MAX_PATH_LEN];
        if (!cur || cur[0] == '\0')
            snprintf(rel, sizeof(rel), "%s", e->d_name);
        else
            snprintf(rel, sizeof(rel), "%s/%s", cur, e->d_name);
        char full[MAX_PATH_LEN];
        path_join(dir, e->d_name, full, sizeof(full));
        struct stat st;
        if (stat(full, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
        {
            if (!collect_tree_paths(root_dir, rel, paths, max, count))
            {
                closedir(d);
                return 0;
            }
        }
        else
        {
            if (*count >= max)
            {
                closedir(d);
                return 0;
            }
            strncpy(paths[*count], rel, MAX_PATH_LEN - 1);
            paths[*count][MAX_PATH_LEN - 1] = '\0';
            (*count)++;
        }
    }
    closedir(d);
    return 1;
}

#endif /* platform */

/* -------------------------------------------------------------------------
 * File comparison
 * ---------------------------------------------------------------------- */

int compare_files(const char *path_a, const char *path_b)
{
    if (!file_exists(path_a) || !file_exists(path_b))
        return 1;
    FILE *a = fopen(path_a, "rb");
    FILE *b = fopen(path_b, "rb");
    if (!a || !b)
    {
        if (a)
            fclose(a);
        if (b)
            fclose(b);
        return 1;
    }
    int different = 0;
    char ba[8192], bb[8192];
    size_t ra, rb2;
    while (1)
    {
        ra = fread(ba, 1, sizeof(ba), a);
        rb2 = fread(bb, 1, sizeof(bb), b);
        if (ra != rb2 || memcmp(ba, bb, ra) != 0)
        {
            different = 1;
            break;
        }
        if (ra == 0)
            break;
    }
    fclose(a);
    fclose(b);
    return different;
}