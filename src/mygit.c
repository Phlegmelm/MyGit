#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "cli.h"
#include "fs_utils.h"

int main(int argc, char *argv[])
{
    const char *debug_env = getenv("MYGIT_DEBUG");
    if (debug_env && !strcmp(debug_env, "1"))
        g_debug_enabled = 1;

    init_color_support();
    srand((unsigned)time(NULL));

    if (argc > 1)
    {
        /* Command-line mode: join args into one string */
        char command[MAX_INPUT] = "";
        for (int i = 1; i < argc; i++)
        {
            if (i > 1)
                strncat(command, " ", sizeof(command) - strlen(command) - 1);
            strncat(command, argv[i], sizeof(command) - strlen(command) - 1);
        }
        log_msg(LOG_DEBUG, "CLI mode: '%s'", command);
        parse_and_execute(command);
    }
    else
    {
        log_msg(LOG_DEBUG, "Interactive mode");
        run_cli();
    }

    return EXIT_SUCCESS;
}