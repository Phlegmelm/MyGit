#include "cli.h"
#include "repo.h"
#include "fs_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* =========================================================================
 * Command table
 * ====================================================================== */

static const struct
{
    const char *name;
    CommandType type;
    const char *usage;
    const char *description;
} commands[] = {
    {"init", CMD_INIT, "init", "Initialise a new repository"},
    {"add", CMD_ADD, "add <file|.>", "Stage a file, or '.' for everything"},
    {"rm", CMD_RM, "rm <file>", "Remove a file and stage its deletion"},
    {"commit", CMD_COMMIT, "commit -m <message>", "Record staged changes as a new commit"},
    {"status", CMD_STATUS, "status", "Show staged and unstaged changes"},
    {"log", CMD_LOG, "log [--oneline] [--graph] [--all] [-n N]", "Print commit history"},
    {"show", CMD_SHOW, "show <commit-id>", "Show a commit's details and diff"},
    {"branch", CMD_BRANCH, "branch [-d] [name]", "List, create, or delete branches"},
    {"checkout", CMD_CHECKOUT, "checkout <branch>", "Switch to an existing branch"},
    {"merge", CMD_MERGE, "merge <branch|--abort>", "Merge a branch (or abort)"},
    {"cherry-pick", CMD_CHERRY_PICK, "cherry-pick <commit-id>", "Apply a single commit to current branch"},
    {"diff", CMD_DIFF, "diff [file] [commit-a commit-b]", "Show line-level diff"},
    {"reset", CMD_RESET, "reset [HEAD <file> | --hard <id>]", "Unstage or hard-reset"},
    {"revert", CMD_REVERT, "revert <commit-id>", "Stage the inverse of a commit"},
    {"stash", CMD_STASH, "stash [pop|apply|list|drop]", "Shelve or restore working changes"},
    {"bisect", CMD_BISECT, "bisect <start|good|bad|reset> [id]", "Binary-search history for a bug"},
    {"blame", CMD_BLAME, "blame <file>", "Show which commit last changed each line"},
    {"grep", CMD_GREP, "grep [-i] <pattern>", "Search tracked files for a pattern"},
    {"tag", CMD_TAG, "tag [-d] [name [commit-id]]", "Create, list, or delete tags"},
    {"config", CMD_CONFIG, "config [key [value]]", "Read or write config values"},
    {"help", CMD_HELP, "help", "Show this help"},
    {"exit", CMD_EXIT, "exit", "Quit MyGit"},
};
static const size_t NUM_COMMANDS = sizeof(commands) / sizeof(commands[0]);

/* =========================================================================
 * Input
 * ====================================================================== */

static int get_input_line(char *buf, size_t size)
{
    if (!buf || size == 0)
        return 0;
    fflush(stdout);
    if (fgets(buf, (int)size, stdin) == NULL)
    {
        if (feof(stdin))
        {
            printf("\n");
            return 0;
        }
        clearerr(stdin);
        buf[0] = '\0';
        return 1;
    }
    buf[strcspn(buf, "\r\n")] = '\0';
    return 1;
}

/* =========================================================================
 * Help
 * ====================================================================== */

void show_help(void)
{
    printf("\n");
    printf("  %s+--------------------------------------------------------------------+%s\n", COL_BOLD, COL_RESET);
    printf("  %s|          MyGit v%-6s — Lightweight Version Control             |%s\n", COL_BOLD, MYGIT_VERSION, COL_RESET);
    printf("  %s+--------------------------------------------------------------------+%s\n", COL_BOLD, COL_RESET);
    printf("\n");
    printf("  %sCOMMANDS:%s\n", COL_BOLD, COL_RESET);
    printf("  %-46s  %s\n", "usage", "description");
    printf("  %-46s  %s\n", "-----", "-----------");
    for (size_t i = 0; i < NUM_COMMANDS - 2; i++)
        printf("  %s%-46s%s  %s\n", COL_CYAN, commands[i].usage, COL_RESET, commands[i].description);
    printf("\n");
    printf("  %sENVIRONMENT:%s\n", COL_BOLD, COL_RESET);
    printf("  %-46s  Enable debug output\n", "MYGIT_DEBUG=1");
    printf("  %-46s  Force color on/off\n", "MYGIT_COLOR=0|1");
    printf("\n");
    printf("  %sQUICK EXAMPLES:%s\n", COL_BOLD, COL_RESET);
    printf("  mygit add .                   # stage everything\n");
    printf("  mygit commit -m \"first\"      # commit\n");
    printf("  mygit branch feature          # new branch\n");
    printf("  mygit checkout feature        # switch\n");
    printf("  mygit merge feature           # merge back\n");
    printf("  mygit cherry-pick <id>        # pick one commit\n");
    printf("  mygit blame src/main.c        # who wrote what\n");
    printf("  mygit bisect start            # find the bad commit\n");
    printf("  mygit grep -i \"todo\"         # search codebase\n");
    printf("  mygit log --oneline --all     # full history\n");
    printf("\n");
}

/* =========================================================================
 * Dispatch
 * ====================================================================== */

static CommandType command_from_name(const char *name)
{
    if (!name)
        return CMD_INVALID;
    for (size_t i = 0; i < NUM_COMMANDS; i++)
        if (!strcmp(name, commands[i].name))
            return commands[i].type;
    return CMD_INVALID;
}

void parse_and_execute(const char *input)
{
    char buf[MAX_INPUT];
    strncpy(buf, input, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *cmd_token = strtok(buf, " ");
    if (!cmd_token)
        return;

    log_msg(LOG_DEBUG, "execute: '%s'", cmd_token);
    CommandType cmd = command_from_name(cmd_token);

    switch (cmd)
    {
    /* ------------------------------------------------------------------ */
    case CMD_INIT:
        initialize_repository();
        break;

    /* ------------------------------------------------------------------ */
    case CMD_ADD:
    {
        char *path = strtok(NULL, "");
        if (!path)
        {
            log_msg(LOG_ERROR, "Usage: add <file-path|.>");
            break;
        }
        trim_whitespace(path);
        if (!strcmp(path, "."))
            stage_all();
        else
            stage_path(path);
        break;
    }

    /* ------------------------------------------------------------------ */
    case CMD_RM:
    {
        char *path = strtok(NULL, "");
        if (!path)
        {
            log_msg(LOG_ERROR, "Usage: rm <file-path>");
            break;
        }
        trim_whitespace(path);
        remove_file_command(path);
        break;
    }

    /* ------------------------------------------------------------------ */
    case CMD_COMMIT:
    {
        char *flag = strtok(NULL, " ");
        if (!flag || strcmp(flag, "-m") != 0)
        {
            log_msg(LOG_ERROR, "Usage: commit -m <message>");
            break;
        }
        const char *rem = strstr(input, "-m");
        if (!rem)
        {
            log_msg(LOG_ERROR, "Usage: commit -m <message>");
            break;
        }
        rem += 2;
        while (*rem == ' ')
            rem++;
        if (!*rem)
        {
            log_msg(LOG_ERROR, "Commit message cannot be empty.");
            break;
        }
        char msg[MAX_MESSAGE_LEN];
        strncpy(msg, rem, sizeof(msg) - 1);
        msg[sizeof(msg) - 1] = '\0';
        trim_whitespace(msg);
        if (!acquire_index_lock())
            break;
        commit_changes(msg);
        release_index_lock();
        break;
    }

    /* ------------------------------------------------------------------ */
    case CMD_STATUS:
        show_status();
        break;

    /* ------------------------------------------------------------------ */
    case CMD_LOG:
    {
        int oneline = 0, graph = 0, limit = 0, all = 0;
        char *arg;
        while ((arg = strtok(NULL, " ")) != NULL)
        {
            if (!strcmp(arg, "--oneline"))
                oneline = 1;
            else if (!strcmp(arg, "--graph"))
                graph = 1;
            else if (!strcmp(arg, "--all"))
                all = 1;
            else if (!strcmp(arg, "-n"))
            {
                char *n = strtok(NULL, " ");
                if (n)
                    limit = atoi(n);
            }
            else if (arg[0] == '-' && arg[1] == 'n' && arg[2])
                limit = atoi(arg + 2);
        }
        show_log(oneline, graph, limit, all);
        break;
    }

    /* ------------------------------------------------------------------ */
    case CMD_SHOW:
    {
        char *cid = strtok(NULL, "");
        if (!cid)
        {
            log_msg(LOG_ERROR, "Usage: show <commit-id>");
            break;
        }
        trim_whitespace(cid);
        show_commit(cid);
        break;
    }

    /* ------------------------------------------------------------------ */
    case CMD_BRANCH:
    {
        char *arg = strtok(NULL, " ");
        if (!arg)
        {
            show_branches();
        }
        else if (!strcmp(arg, "-d"))
        {
            char *name = strtok(NULL, "");
            if (!name)
            {
                log_msg(LOG_ERROR, "Usage: branch -d <name>");
                break;
            }
            trim_whitespace(name);
            delete_branch(name);
        }
        else
        {
            trim_whitespace(arg);
            create_branch(arg);
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case CMD_CHECKOUT:
    {
        char *name = strtok(NULL, "");
        if (!name)
        {
            log_msg(LOG_ERROR, "Usage: checkout <branch>");
            break;
        }
        trim_whitespace(name);
        checkout_branch(name);
        break;
    }

    /* ------------------------------------------------------------------ */
    case CMD_MERGE:
    {
        char *arg = strtok(NULL, "");
        if (!arg)
        {
            log_msg(LOG_ERROR, "Usage: merge <branch|--abort>");
            break;
        }
        trim_whitespace(arg);
        if (!strcmp(arg, "--abort"))
            merge_abort();
        else
            merge_branch(arg);
        break;
    }

    /* ------------------------------------------------------------------ */
    case CMD_CHERRY_PICK:
    {
        char *cid = strtok(NULL, "");
        if (!cid)
        {
            log_msg(LOG_ERROR, "Usage: cherry-pick <commit-id>");
            break;
        }
        trim_whitespace(cid);
        cherry_pick(cid);
        break;
    }

    /* ------------------------------------------------------------------ */
    case CMD_DIFF:
    {
        char *a1 = strtok(NULL, " "), *a2 = strtok(NULL, " "), *a3 = strtok(NULL, " ");
        if (!a1)
            show_diff(NULL, NULL, NULL);
        else if (!a2)
        {
            trim_whitespace(a1);
            show_diff(a1, NULL, NULL);
        }
        else if (!a3)
        {
            trim_whitespace(a1);
            trim_whitespace(a2);
            show_diff(NULL, a1, a2);
        }
        else
        {
            trim_whitespace(a1);
            trim_whitespace(a2);
            trim_whitespace(a3);
            show_diff(a1, a2, a3);
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case CMD_RESET:
    {
        char *first = strtok(NULL, " ");
        if (!first)
        {
            log_msg(LOG_ERROR, "Usage: reset [HEAD <file> | --hard <id>]");
            break;
        }
        if (!strcmp(first, "HEAD"))
        {
            char *fp = strtok(NULL, "");
            if (!fp)
            {
                log_msg(LOG_ERROR, "Usage: reset HEAD <file>");
                break;
            }
            trim_whitespace(fp);
            reset_file(fp);
        }
        else if (!strcmp(first, "--hard"))
        {
            char *cid = strtok(NULL, "");
            if (!cid)
            {
                log_msg(LOG_ERROR, "Usage: reset --hard <commit-id>");
                break;
            }
            trim_whitespace(cid);
            reset_hard(cid);
        }
        else
            log_msg(LOG_ERROR, "Usage: reset [HEAD <file> | --hard <id>]");
        break;
    }

    /* ------------------------------------------------------------------ */
    case CMD_REVERT:
    {
        char *cid = strtok(NULL, "");
        if (!cid)
        {
            log_msg(LOG_ERROR, "Usage: revert <commit-id>");
            break;
        }
        trim_whitespace(cid);
        revert_commit(cid);
        break;
    }

    /* ------------------------------------------------------------------ */
    case CMD_STASH:
    {
        char *sub = strtok(NULL, " ");
        if (!sub || !strcmp(sub, "save"))
        {
            char *msg = sub ? strtok(NULL, "") : NULL;
            if (msg)
                trim_whitespace(msg);
            stash_save(msg);
        }
        else if (!strcmp(sub, "pop"))
            stash_pop();
        else if (!strcmp(sub, "apply"))
            stash_apply();
        else if (!strcmp(sub, "list"))
            stash_list();
        else if (!strcmp(sub, "drop"))
        {
            char *idx = strtok(NULL, "");
            stash_drop(idx ? atoi(idx) : -1);
        }
        else
            log_msg(LOG_ERROR, "Usage: stash [save [msg] | pop | apply | list | drop]");
        break;
    }

    /* ------------------------------------------------------------------ */
    case CMD_BISECT:
    {
        char *sub = strtok(NULL, " ");
        if (!sub)
        {
            log_msg(LOG_ERROR, "Usage: bisect <start|good|bad|reset> [commit-id]");
            break;
        }
        if (!strcmp(sub, "start"))
            bisect_start();
        else if (!strcmp(sub, "reset"))
            bisect_reset();
        else if (!strcmp(sub, "good"))
        {
            char *cid = strtok(NULL, "");
            if (cid)
                trim_whitespace(cid);
            bisect_good(cid);
        }
        else if (!strcmp(sub, "bad"))
        {
            char *cid = strtok(NULL, "");
            if (cid)
                trim_whitespace(cid);
            bisect_bad(cid);
        }
        else
            log_msg(LOG_ERROR, "Usage: bisect <start|good|bad|reset> [commit-id]");
        break;
    }

    /* ------------------------------------------------------------------ */
    case CMD_BLAME:
    {
        char *path = strtok(NULL, "");
        if (!path)
        {
            log_msg(LOG_ERROR, "Usage: blame <file>");
            break;
        }
        trim_whitespace(path);
        blame_file(path);
        break;
    }

    /* ------------------------------------------------------------------ */
    case CMD_GREP:
    {
        int ignore_case = 0;
        char *arg = strtok(NULL, " ");
        if (!arg)
        {
            log_msg(LOG_ERROR, "Usage: grep [-i] <pattern>");
            break;
        }
        if (!strcmp(arg, "-i"))
        {
            ignore_case = 1;
            arg = strtok(NULL, "");
            if (!arg)
            {
                log_msg(LOG_ERROR, "Usage: grep [-i] <pattern>");
                break;
            }
        }
        trim_whitespace(arg);
        grep_repo(arg, ignore_case);
        break;
    }

    /* ------------------------------------------------------------------ */
    case CMD_TAG:
    {
        char *arg = strtok(NULL, " ");
        if (!arg)
        {
            show_tags();
            break;
        }
        if (!strcmp(arg, "-d"))
        {
            char *name = strtok(NULL, "");
            if (!name)
            {
                log_msg(LOG_ERROR, "Usage: tag -d <name>");
                break;
            }
            trim_whitespace(name);
            delete_tag(name);
        }
        else
        {
            trim_whitespace(arg);
            char *cid = strtok(NULL, "");
            if (cid)
                trim_whitespace(cid);
            create_tag(arg, cid);
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case CMD_CONFIG:
    {
        char *key = strtok(NULL, " ");
        if (!key)
        {
            config_list();
            break;
        }
        trim_whitespace(key);
        char *value = strtok(NULL, "");
        if (!value)
        {
            config_get(key);
            break;
        }
        trim_whitespace(value);
        config_set(key, value);
        break;
    }

    /* ------------------------------------------------------------------ */
    case CMD_HELP:
        show_help();
        break;

    case CMD_EXIT:
        log_msg(LOG_INFO, "Goodbye.");
        exit(EXIT_SUCCESS);

    default:
        log_msg(LOG_WARN, "Unknown command '%s'. Type 'help'.", cmd_token);
        break;
    }
}

/* =========================================================================
 * REPL
 * ====================================================================== */

void run_cli(void)
{
    init_color_support();
    const char *dbg = getenv("MYGIT_DEBUG");
    if (dbg && !strcmp(dbg, "1"))
        g_debug_enabled = 1;

    printf("%s+--------------------------------------+%s\n", COL_BOLD, COL_RESET);
    printf("%s|   MyGit v%-6s  Version Control      |%s\n", COL_BOLD, MYGIT_VERSION, COL_RESET);
    printf("%s+--------------------------------------+%s\n", COL_BOLD, COL_RESET);
    printf("\n");

    if (directory_exists(REPO_DIR))
    {
        char cid[MAX_COMMIT_ID] = "", branch[MAX_PATH_LEN] = "";
        get_current_commit_id(cid, sizeof(cid));
        get_current_branch_name(branch, sizeof(branch));
        log_msg(LOG_INFO, "Repository found at '%s'.", REPO_DIR);
        log_msg(LOG_INFO, "  branch : %s%s%s", COL_GREEN, branch[0] ? branch : "(unknown)", COL_RESET);
        log_msg(LOG_INFO, "  HEAD   : %s%s%s", COL_YELLOW, cid[0] ? cid : "(no commits yet)", COL_RESET);
        if (file_exists(REPO_MERGE_HEAD_FILE))
            log_msg(LOG_WARN, "%sMerge in progress. Resolve conflicts then commit, or 'merge --abort'.%s",
                    COL_MAGENTA, COL_RESET);
        if (directory_exists(REPO_BISECT_DIR))
            log_msg(LOG_WARN, "%sBisect in progress. Use 'bisect good/bad' or 'bisect reset'.%s",
                    COL_YELLOW, COL_RESET);
    }
    else
        log_msg(LOG_INFO, "No repository. Use 'init' to create one.");

    show_help();

    char input[MAX_INPUT];
    while (1)
    {
        char branch[MAX_PATH_LEN] = "";
        if (directory_exists(REPO_DIR))
            get_current_branch_name(branch, sizeof(branch));

        const char *merge_flag = file_exists(REPO_MERGE_HEAD_FILE) ? "|MERGING" : "";
        const char *bisect_flag = directory_exists(REPO_BISECT_DIR) ? "|BISECT" : "";

        if (branch[0])
            printf("%smygit%s(%s%s%s%s%s%s)%s> ",
                   COL_BOLD, COL_RESET,
                   COL_GREEN, branch, COL_RESET,
                   COL_MAGENTA, merge_flag, COL_RESET,
                   COL_RESET);
        else
            printf("%smygit%s%s%s%s> ",
                   COL_BOLD, COL_RESET,
                   COL_YELLOW, bisect_flag, COL_RESET);

        if (!get_input_line(input, sizeof(input)))
        {
            log_msg(LOG_INFO, "Goodbye.");
            break;
        }
        trim_whitespace(input);
        if (!input[0])
            continue;
        parse_and_execute(input);
    }
}