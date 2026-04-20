#ifndef FS_UTILS_H
#define FS_UTILS_H

#include "mygit.h"

extern int g_debug_enabled;
extern int g_color_enabled;

/* Logging */
void log_msg(LogLevel level, const char *fmt, ...);
void print_error(const char *message);

/* String helpers */
void trim_whitespace(char *text);
void normalize_path(char *path);
int match_pattern(const char *pattern, const char *str);

/* File I/O */
int read_text_file(const char *path, char *output, size_t size);
int write_text_file(const char *path, const char *contents);
int append_text_file(const char *path, const char *contents);

/* Path helpers */
int path_join(const char *base, const char *name, char *out, size_t size);

/* Filesystem queries */
int directory_exists(const char *path);
int file_exists(const char *path);
long file_size(const char *path);

/* Filesystem mutation */
int create_directory(const char *path);
int create_parent_directories(const char *path);
int copy_file(const char *src, const char *dst);
int copy_directory_recursive(const char *src_dir, const char *dst_dir);
int remove_path_recursive(const char *path);

/* Tree traversal */
int collect_tree_paths(const char *root_dir, const char *current_path,
                       char paths[][MAX_PATH_LEN], size_t max_count, size_t *count);

/* File comparison */
int compare_files(const char *path_a, const char *path_b);

/* Platform colour init */
void init_color_support(void);

#endif /* FS_UTILS_H */