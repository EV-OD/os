#include "shell/shell_core.h"
#include "stdio.h"
#include "string.h"
#include "vfs.h"
#include "rox.h"

/* -------------------------------------------------------------------------
 * Shared state (managed in shell.c, accessed here via accessors)
 * ------------------------------------------------------------------------- */
extern const char* shell_get_cwd(void);

/* -------------------------------------------------------------------------
 * Path helpers
 * ------------------------------------------------------------------------- */

void resolve_path(const char *input, char *out, unsigned int out_size)
{
    const char *cwd = shell_get_cwd();
    if (input[0] == '/') {
        strncpy(out, input, out_size - 1);
        out[out_size - 1] = '\0';
    } else {
        /* cwd + "/" + input */
        unsigned int cwd_len = strlen(cwd);
        strncpy(out, cwd, out_size - 1);
        out[out_size - 1] = '\0';

        /* Append '/' if cwd doesn't end with one */
        if (cwd_len > 0 && cwd[cwd_len - 1] != '/' && cwd_len + 1 < out_size) {
            out[cwd_len] = '/';
            out[cwd_len + 1] = '\0';
        }

        /* Append the relative path */
        strncat(out, input, out_size - strlen(out) - 1);
    }

    /* Remove trailing slash (unless root) */
    unsigned int len = strlen(out);
    if (len > 1 && out[len - 1] == '/') {
        out[len - 1] = '\0';
    }
}

int tokenise(char *input, char **argv, int max_args)
{
    int argc = 0;
    char *p = input;

    while (*p && argc < max_args - 1) {
        /* Skip leading whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        argv[argc++] = p;

        /* Find end of token */
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) {
            *p = '\0';
            p++;
        }
    }
    argv[argc] = (void *)0;
    return argc;
}

int ends_with(const char *str, const char *suffix)
{
    int slen = strlen(str);
    int xlen = strlen(suffix);
    if (xlen > slen) return 0;
    return strcmp(str + slen - xlen, suffix) == 0;
}

/* -------------------------------------------------------------------------
 * Shell prompt
 * ------------------------------------------------------------------------- */

void print_prompt(void)
{
    puts_color("rabin", COLOR_LIGHT_GREEN);
    puts_color("@", COLOR_WHITE);
    puts_color(SHELL_HOSTNAME, COLOR_LIGHT_GREEN);
    puts_color(":", COLOR_WHITE);
    puts_color(shell_get_cwd(), COLOR_LIGHT_CYAN);
    puts_color("$ ", COLOR_WHITE);
}

/* -------------------------------------------------------------------------
 * Command resolver
 * ------------------------------------------------------------------------- */

int resolve_and_execute(int argc, char **argv)
{
    if (argc == 0) return 0;

    const char *cmd = argv[0];
    vfs_stat_t st;

    /*
     * Resolve any relative path arguments (argv[1..]) to absolute paths
     * before passing them to the child process.
     */
    {
        static char resolved_args[SHELL_MAX_ARGS][256];
        int ai;
        for (ai = 1; ai < argc; ai++) {
            const char *a = argv[ai];
            if (a[0] == '.' && a[1] == '/') {
                resolve_path(a + 2, resolved_args[ai], sizeof(resolved_args[ai]));
                argv[ai] = resolved_args[ai];
            } else if (a[0] == '.' && a[1] == '.' && a[2] == '/') {
                resolve_path(a, resolved_args[ai], sizeof(resolved_args[ai]));
                argv[ai] = resolved_args[ai];
            }
        }
    }

    /* --- Step 1: Explicit path (starts with / or ./) ------------------- */
    if (cmd[0] == '/') {
        if (vfs_stat(cmd, &st) == 0 && !(st.attributes & 0x10)) {
            return rox_load_and_run(cmd, argc, argv);
        }
        puts_color("rosh: ", COLOR_LIGHT_RED);
        puts((char *)cmd);
        puts(": no such file\n");
        return -1;
    }

    if (cmd[0] == '.' && cmd[1] == '/') {
        char rox_path[256];
        resolve_path(cmd + 2, rox_path, sizeof(rox_path));
        if (vfs_stat(rox_path, &st) == 0 && !(st.attributes & 0x10)) {
            return rox_load_and_run(rox_path, argc, argv);
        }
        puts_color("rosh: ", COLOR_LIGHT_RED);
        puts((char *)cmd);
        puts(": no such file\n");
        return -1;
    }

    /* --- Step 2: If cmd ends with .rox, resolve relative to cwd -------- */
    if (ends_with(cmd, ".rox")) {
        char rox_path[256];
        resolve_path(cmd, rox_path, sizeof(rox_path));
        if (vfs_stat(rox_path, &st) == 0 && !(st.attributes & 0x10)) {
            return rox_load_and_run(rox_path, argc, argv);
        }
        puts_color("rosh: ", COLOR_LIGHT_RED);
        puts(rox_path);
        puts(": no such file\n");
        return -1;
    }

    /* --- Step 3: Try /bin/<cmd>.rox ------------------------------------ */
    {
        char rox_path[128];
        sprintf(rox_path, "/bin/%s.rox", cmd);
        if (vfs_stat(rox_path, &st) == 0 && !(st.attributes & 0x10)) {
            return rox_load_and_run(rox_path, argc, argv);
        }
    }

    /* --- Step 4: Built-in command table -------------------------------- */
    for (int i = 0; builtins[i].name != (void *)0; i++) {
        if (strcmp(cmd, builtins[i].name) == 0) {
            return builtins[i].handler(argc, argv);
        }
    }

    /* --- Step 5: Not found --------------------------------------------- */
    puts_color("rosh: ", COLOR_LIGHT_RED);
    puts((char *)cmd);
    puts(": command not found\n");
    return -1;
}
