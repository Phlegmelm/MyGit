#ifndef REPO_H
#define REPO_H

#include "mygit.h"

/* Core repo */
int initialize_repository(void);
int stage_path(const char *path);
int stage_all(void);
int stage_remove(const char *path);
int clear_index(void);
int acquire_index_lock(void);
void release_index_lock(void);
int get_current_commit_id(char *commit_id, size_t size);
int get_current_branch_name(char *branch_name, size_t size);
int update_current_branch_head(const char *commit_id);
int branch_exists(const char *branch_name);
int read_commit_metadata(const char *commit_id, CommitMetadata *metadata);
int read_commit_tree(const char *commit_id, char paths[][MAX_PATH_LEN], size_t max_entries);
int should_ignore_file(const char *path);
int is_binary_file(const char *path);
int restore_snapshot_to_working_tree(const char *commit_id);

/* Commands */
void commit_changes(const char *message);
void remove_file_command(const char *path);
void show_status(void);
void show_log(int oneline, int graph, int limit, int all);
void show_branches(void);
void create_branch(const char *branch_name);
int delete_branch(const char *branch_name);
int checkout_branch(const char *branch_name);
void show_diff(const char *path, const char *commit_a, const char *commit_b);
int reset_file(const char *path);
int reset_hard(const char *commit_id);
void revert_commit(const char *commit_id);
void show_commit(const char *commit_id);

/* Merge */
int merge_branch(const char *branch_name);
int merge_abort(void);

/* Cherry-pick */
int cherry_pick(const char *commit_id);

/* Blame */
void blame_file(const char *path);

/* Bisect */
void bisect_start(void);
void bisect_good(const char *commit_id);
void bisect_bad(const char *commit_id);
void bisect_reset(void);

/* Grep */
void grep_repo(const char *pattern, int ignore_case);

/* Stash */
int stash_save(const char *message);
int stash_pop(void);
int stash_apply(void);
int stash_list(void);
int stash_drop(int index);

/* Tag */
void create_tag(const char *tag_name, const char *commit_id);
void delete_tag(const char *tag_name);
void show_tags(void);

/* Config */
void config_set(const char *key, const char *value);
void config_get(const char *key);
void config_list(void);

#endif /* REPO_H */