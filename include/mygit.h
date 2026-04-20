#ifndef MYGIT_H
#define MYGIT_H

#include <stddef.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(path) _mkdir(path)
#define REMOVE_DIR(path) _rmdir(path)
#define IS_DIRECTORY(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#else
#include <sys/types.h>
#define MAKE_DIR(path) mkdir((path), 0755)
#define REMOVE_DIR(path) rmdir(path)
#define IS_DIRECTORY(mode) S_ISDIR(mode)
#endif

#define MYGIT_VERSION "1.2.0"
#define REPO_DIR ".mygit"
#define REPO_OBJECTS_DIR ".mygit/objects"
#define REPO_COMMITS_DIR ".mygit/commits"
#define REPO_REFS_DIR ".mygit/refs"
#define REPO_REFS_HEADS_DIR ".mygit/refs/heads"
#define REPO_REFS_TAGS_DIR ".mygit/refs/tags"
#define REPO_INDEX_FILE ".mygit/index"
#define REPO_INDEX_LOCK_FILE ".mygit/index.lock"
#define REPO_HEAD_FILE ".mygit/HEAD"
#define REPO_LOG_FILE ".mygit/log"
#define REPO_DEBUG_LOG_FILE ".mygit/debug.log"
#define REPO_IGNORE_FILE ".mygitignore"
#define REPO_STASH_DIR ".mygit/stash"
#define REPO_STASH_INDEX_FILE ".mygit/stash/index"
#define REPO_MERGE_HEAD_FILE ".mygit/MERGE_HEAD"
#define REPO_MERGE_MSG_FILE ".mygit/MERGE_MSG"
#define REPO_BISECT_DIR ".mygit/bisect"
#define REPO_BISECT_LOG_FILE ".mygit/bisect/log"
#define REPO_BISECT_GOOD_FILE ".mygit/bisect/good"
#define REPO_BISECT_BAD_FILE ".mygit/bisect/bad"
#define REPO_BISECT_HEAD_FILE ".mygit/bisect/head"
#define REPO_CONFIG_FILE ".mygit/config"

#define MAX_INPUT 1024
#define MAX_PATH_LEN 1024
#define MAX_COMMIT_ID 64
#define MAX_MESSAGE_LEN 512
#define MAX_TREE_ENTRIES 4096
#define MAX_STASH_ENTRIES 64
#define MAX_BISECT_COMMITS 512

extern int g_color_enabled;
#define COL_RESET (g_color_enabled ? "\033[0m" : "")
#define COL_RED (g_color_enabled ? "\033[31m" : "")
#define COL_GREEN (g_color_enabled ? "\033[32m" : "")
#define COL_YELLOW (g_color_enabled ? "\033[33m" : "")
#define COL_BLUE (g_color_enabled ? "\033[34m" : "")
#define COL_MAGENTA (g_color_enabled ? "\033[35m" : "")
#define COL_CYAN (g_color_enabled ? "\033[36m" : "")
#define COL_BOLD (g_color_enabled ? "\033[1m" : "")
#define COL_DIM (g_color_enabled ? "\033[2m" : "")

typedef enum
{
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} LogLevel;

typedef enum
{
    CMD_INIT,
    CMD_ADD,
    CMD_RM,
    CMD_COMMIT,
    CMD_STATUS,
    CMD_LOG,
    CMD_BRANCH,
    CMD_CHECKOUT,
    CMD_DIFF,
    CMD_RESET,
    CMD_REVERT,
    CMD_MERGE,
    CMD_STASH,
    CMD_SHOW,
    CMD_TAG,
    CMD_CONFIG,
    CMD_CHERRY_PICK,
    CMD_BLAME,
    CMD_BISECT,
    CMD_GREP,
    CMD_HELP,
    CMD_EXIT,
    CMD_INVALID
} CommandType;

typedef struct
{
    char action;
    char path[MAX_PATH_LEN];
} IndexEntry;

typedef struct
{
    char commit_id[MAX_COMMIT_ID];
    char parent_id[MAX_COMMIT_ID];
    char parent2_id[MAX_COMMIT_ID];
    char branch[MAX_PATH_LEN];
    char author[128];
    char date[32];
    char message[MAX_MESSAGE_LEN];
} CommitMetadata;

typedef struct
{
    int index;
    char message[MAX_MESSAGE_LEN];
    char commit_id[MAX_COMMIT_ID];
    char date[32];
    char branch[MAX_PATH_LEN];
} StashEntry;

extern int g_debug_enabled;
extern int g_color_enabled;

#endif /* MYGIT_H */