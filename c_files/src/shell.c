/* =========================================================================
 * shell.c – Rabin OS Shell (rosh)
 *
 * An interactive command-line shell that runs in kernel mode.
 *
 * Features:
 *   - Line editor with backspace support (via readline())
 *   - Tokeniser: splits input on whitespace into argc/argv
 *   - Command resolver: /bin/<cmd>.rox first, then built-in table
 *   - Built-in commands: help, ls, cd, cat, echo, clear, mkdir, touch,
 *                        rm, pwd, mode, stat, write, uname, whoami
 *   - OS directory structure creation (/bin, /etc, /home, /tmp)
 *   - Coloured prompt
 *
 * The shell runs as a scheduled kernel task (called from kmain or a
 * process entry point).
 * ========================================================================= */

#include "shell.h"
#include "stdio.h"
#include "string.h"
#include "vfs.h"
#include "rox.h"
#include "kheap.h"
#include "log.h"
#include "pit.h"

/* -------------------------------------------------------------------------
 * OS mode (default: nerd mode)
 * ------------------------------------------------------------------------- */
static os_mode_t current_mode = OS_MODE_NERD;

os_mode_t os_get_mode(void) { return current_mode; }
void os_set_mode(os_mode_t mode) { current_mode = mode; }

/* -------------------------------------------------------------------------
 * Current working directory (max 256 chars).
 * Starts at "/".
 * ------------------------------------------------------------------------- */
static char cwd[256] = "/";

/* -------------------------------------------------------------------------
 * Built-in command table
 * ------------------------------------------------------------------------- */
typedef int (*builtin_fn_t)(int argc, char **argv);

typedef struct builtin_cmd {
    const char   *name;
    const char   *description;
    builtin_fn_t  handler;
} builtin_cmd_t;

/* Forward declarations of built-in handlers */
static int cmd_help(int argc, char **argv);
static int cmd_ls(int argc, char **argv);
static int cmd_cd(int argc, char **argv);
static int cmd_cat(int argc, char **argv);
static int cmd_echo(int argc, char **argv);
static int cmd_clear(int argc, char **argv);
static int cmd_mkdir(int argc, char **argv);
static int cmd_touch(int argc, char **argv);
static int cmd_rm(int argc, char **argv);
static int cmd_pwd(int argc, char **argv);
static int cmd_mode(int argc, char **argv);
static int cmd_stat(int argc, char **argv);
static int cmd_write(int argc, char **argv);
static int cmd_uname(int argc, char **argv);
static int cmd_whoami(int argc, char **argv);

static const builtin_cmd_t builtins[] = {
    { "help",   "Show available commands",          cmd_help   },
    { "ls",     "List directory contents",          cmd_ls     },
    { "cd",     "Change current directory",         cmd_cd     },
    { "cat",    "Display file contents",            cmd_cat    },
    { "echo",   "Print text to screen",             cmd_echo   },
    { "clear",  "Clear the screen",                 cmd_clear  },
    { "mkdir",  "Create a directory",               cmd_mkdir  },
    { "touch",  "Create an empty file",             cmd_touch  },
    { "rm",     "Remove a file",                    cmd_rm     },
    { "pwd",    "Print working directory",          cmd_pwd    },
    { "mode",   "Show/set OS mode (nerd|gui)",      cmd_mode   },
    { "stat",   "Show file/directory info",         cmd_stat   },
    { "write",  "Write text to a file",             cmd_write  },
    { "uname",  "Print system information",         cmd_uname  },
    { "whoami", "Print current user",               cmd_whoami },
    { (void *)0, (void *)0, (void *)0 } /* sentinel */
};

/* =========================================================================
 * Path helpers
 * ========================================================================= */

/**
 * resolve_path – build an absolute path from the cwd and a user-supplied
 * path.  If the path starts with '/', use it as-is.  Otherwise, join
 * cwd + "/" + path.  Result written into out (must be >= 256 bytes).
 */
static void resolve_path(const char *input, char *out, unsigned int out_size)
{
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

/* =========================================================================
 * Tokeniser – split input into argv[]
 * ========================================================================= */
static int tokenise(char *input, char **argv, int max_args)
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

/* =========================================================================
 * Command resolver
 *
 * 1. Build "/bin/<cmd>.rox" and check if it exists via vfs_stat
 * 2. If found, execute via rox_load_and_run
 * 3. Otherwise, search the built-in table
 * 4. If neither, print "command not found"
 * ========================================================================= */
static int resolve_and_execute(int argc, char **argv)
{
    if (argc == 0) return 0;

    const char *cmd = argv[0];

    /* --- Step 1: Try /bin/<cmd>.rox ------------------------------------ */
    {
        char rox_path[128];
        sprintf(rox_path, "/bin/%s.rox", cmd);

        vfs_stat_t st;
        if (vfs_stat(rox_path, &st) == 0 && !(st.attributes & 0x10)) {
            return rox_load_and_run(rox_path, argc, argv);
        }
    }

    /* --- Step 2: Built-in command table -------------------------------- */
    for (int i = 0; builtins[i].name != (void *)0; i++) {
        if (strcmp(cmd, builtins[i].name) == 0) {
            return builtins[i].handler(argc, argv);
        }
    }

    /* --- Step 3: Not found --------------------------------------------- */
    puts_color("rosh: ", COLOR_LIGHT_RED);
    puts((char *)cmd);
    puts(": command not found\n");
    return -1;
}

/* =========================================================================
 * Shell prompt
 * ========================================================================= */
static void print_prompt(void)
{
    puts_color("rabin", COLOR_LIGHT_GREEN);
    puts_color("@", COLOR_WHITE);
    puts_color(SHELL_HOSTNAME, COLOR_LIGHT_GREEN);
    puts_color(":", COLOR_WHITE);
    puts_color(cwd, COLOR_LIGHT_CYAN);
    puts_color("$ ", COLOR_WHITE);
}

/* =========================================================================
 * OS directory structure creation
 * ========================================================================= */
void shell_init(void)
{
    log_info("[shell] creating OS directory structure");

    static const char *dirs[] = {
        "/bin",
        "/etc",
        "/home",
        "/tmp",
        "/var",
        "/var/log",
        (void *)0
    };

    for (int i = 0; dirs[i] != (void *)0; i++) {
        int ret = vfs_mkdir(dirs[i]);
        if (ret == 0) {
            log_info("[shell] created %s", dirs[i]);
        } else if (ret == VFS_ERR_EXIST) {
            log_info("[shell] %s already exists", dirs[i]);
        } else {
            log_error("[shell] failed to create %s (err=%d)", dirs[i], ret);
        }
    }

    /* Create /etc/hostname */
    int fd = vfs_open("/etc/hostname", VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd >= 0) {
        vfs_write(fd, SHELL_HOSTNAME, strlen(SHELL_HOSTNAME));
        vfs_write(fd, "\n", 1);
        vfs_close(fd);
    }

    /* Create /etc/motd (message of the day) */
    fd = vfs_open("/etc/motd", VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd >= 0) {
        const char *motd =
            "Welcome to RabinOS v0.1.0\n"
            "Type 'help' for available commands.\n";
        vfs_write(fd, motd, strlen(motd));
        vfs_close(fd);
    }

    log_info("[shell] OS directory structure ready");
}

/* =========================================================================
 * shell_run – main interactive loop
 * ========================================================================= */
void shell_run(void)
{
    /* Display MOTD */
    {
        int fd = vfs_open("/etc/motd", VFS_O_RDONLY);
        if (fd >= 0) {
            char motd_buf[256];
            memset(motd_buf, 0, sizeof(motd_buf));
            int n = vfs_read(fd, motd_buf, sizeof(motd_buf) - 1);
            vfs_close(fd);
            if (n > 0) {
                puts_color(motd_buf, COLOR_LIGHT_BROWN);
            }
        }
    }

    putchar('\n');

    /* Command loop */
    char input[SHELL_MAX_INPUT];
    char *argv[SHELL_MAX_ARGS];

    while (1) {
        print_prompt();

        int len = readline(input, SHELL_MAX_INPUT);
        if (len == 0) continue;  /* empty line */

        int argc = tokenise(input, argv, SHELL_MAX_ARGS);
        if (argc == 0) continue;

        resolve_and_execute(argc, argv);
    }
}

/* =========================================================================
 * Built-in command implementations
 * ========================================================================= */

/* --- help -------------------------------------------------------------- */
static int cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;

    puts_color("\nRabinOS Shell (rosh) - Available Commands\n", COLOR_LIGHT_CYAN);
    puts_color("=========================================\n", COLOR_DARK_GREY);

    for (int i = 0; builtins[i].name != (void *)0; i++) {
        puts_color("  ", COLOR_WHITE);
        /* Print command name in green, padded to 10 chars */
        char padded[16];
        memset(padded, ' ', sizeof(padded));
        strncpy(padded, builtins[i].name, strlen(builtins[i].name));
        padded[10] = '\0';
        puts_color(padded, COLOR_LIGHT_GREEN);
        puts_color(builtins[i].description, COLOR_LIGHT_GREY);
        putchar('\n');
    }

    puts_color("\n  External commands in /bin/*.rox are also available.\n\n",
               COLOR_DARK_GREY);
    return 0;
}

/* --- ls ---------------------------------------------------------------- */
struct ls_ctx {
    int count;
    int show_all;   /* -a flag (show . and ..) */
};

static int ls_callback(const vfs_dirent_t *d, void *userdata)
{
    struct ls_ctx *ctx = (struct ls_ctx *)userdata;

    /* Skip . and .. unless -a is given */
    if (!ctx->show_all) {
        if (strcmp(d->name, ".") == 0 || strcmp(d->name, "..") == 0)
            return 0;
    }

    if (d->attributes & 0x10) {
        /* Directory: blue */
        puts_color(d->name, COLOR_LIGHT_BLUE);
        puts_color("/", COLOR_LIGHT_BLUE);
    } else {
        /* Check for .rox extension -> green */
        unsigned int nlen = strlen(d->name);
        if (nlen > 4 && strcmp(d->name + nlen - 4, ".rox") == 0) {
            puts_color(d->name, COLOR_LIGHT_GREEN);
        } else {
            puts((char *)d->name);
        }
    }
    puts("  ");
    ctx->count++;

    /* Newline every 5 entries */
    if (ctx->count % 5 == 0) putchar('\n');

    return 0;
}

static int cmd_ls(int argc, char **argv)
{
    char path[256];
    struct ls_ctx ctx = { 0, 0 };

    /* Parse flags and path argument */
    const char *target = (void *)0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0) {
            ctx.show_all = 1;
        } else if (strcmp(argv[i], "-l") == 0) {
            /* Long format not yet implemented */
        } else {
            target = argv[i];
        }
    }

    if (target) {
        resolve_path(target, path, sizeof(path));
    } else {
        strcpy(path, cwd);
    }

    int ret = vfs_readdir(path, ls_callback, &ctx);
    if (ret < 0) {
        puts_color("ls: cannot access '", COLOR_LIGHT_RED);
        puts(path);
        puts("'\n");
        return -1;
    }

    if (ctx.count > 0 && ctx.count % 5 != 0) putchar('\n');
    if (ctx.count == 0) {
        puts_color("(empty)\n", COLOR_DARK_GREY);
    }

    return 0;
}

/* --- cd ---------------------------------------------------------------- */
static int cmd_cd(int argc, char **argv)
{
    if (argc < 2) {
        /* cd with no args → go to / */
        strcpy(cwd, "/");
        return 0;
    }

    char path[256];
    const char *target = argv[1];

    /* Special case: "cd .." */
    if (strcmp(target, "..") == 0) {
        /* Go up one level */
        unsigned int len = strlen(cwd);
        if (len <= 1) return 0;  /* Already at root */
        /* Find last '/' */
        int last_slash = -1;
        for (int i = (int)len - 1; i >= 0; i--) {
            if (cwd[i] == '/') { last_slash = i; break; }
        }
        if (last_slash <= 0) {
            strcpy(cwd, "/");
        } else {
            cwd[last_slash] = '\0';
        }
        return 0;
    }

    resolve_path(target, path, sizeof(path));

    /* Verify it's a directory */
    vfs_stat_t st;
    if (vfs_stat(path, &st) < 0) {
        puts_color("cd: no such directory: ", COLOR_LIGHT_RED);
        puts(path);
        putchar('\n');
        return -1;
    }
    if (!(st.attributes & 0x10)) {
        puts_color("cd: not a directory: ", COLOR_LIGHT_RED);
        puts(path);
        putchar('\n');
        return -1;
    }

    strcpy(cwd, path);
    return 0;
}

/* --- cat --------------------------------------------------------------- */
static int cmd_cat(int argc, char **argv)
{
    if (argc < 2) {
        puts("Usage: cat <file>\n");
        return -1;
    }

    char path[256];
    resolve_path(argv[1], path, sizeof(path));

    int fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        puts_color("cat: ", COLOR_LIGHT_RED);
        puts(path);
        puts(": no such file\n");
        return -1;
    }

    char buf[512];
    int n;
    while ((n = vfs_read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        puts(buf);
    }

    vfs_close(fd);

    /* Ensure we end on a newline */
    putchar('\n');
    return 0;
}

/* --- echo -------------------------------------------------------------- */
static int cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1) putchar(' ');
        puts(argv[i]);
    }
    putchar('\n');
    return 0;
}

/* --- clear ------------------------------------------------------------- */
static int cmd_clear(int argc, char **argv)
{
    (void)argc; (void)argv;
    fb_clear();
    cursor_move_home();
    return 0;
}

/* --- mkdir ------------------------------------------------------------- */
static int cmd_mkdir(int argc, char **argv)
{
    if (argc < 2) {
        puts("Usage: mkdir <directory>\n");
        return -1;
    }

    char path[256];
    resolve_path(argv[1], path, sizeof(path));

    int ret = vfs_mkdir(path);
    if (ret == VFS_ERR_EXIST) {
        puts_color("mkdir: directory already exists: ", COLOR_LIGHT_RED);
        puts(path);
        putchar('\n');
        return -1;
    } else if (ret < 0) {
        puts_color("mkdir: failed to create: ", COLOR_LIGHT_RED);
        puts(path);
        putchar('\n');
        return -1;
    }

    return 0;
}

/* --- touch ------------------------------------------------------------- */
static int cmd_touch(int argc, char **argv)
{
    if (argc < 2) {
        puts("Usage: touch <file>\n");
        return -1;
    }

    char path[256];
    resolve_path(argv[1], path, sizeof(path));

    /* Create file if it doesn't exist; if it exists, just open/close */
    int fd = vfs_open(path, VFS_O_RDWR | VFS_O_CREAT);
    if (fd < 0) {
        puts_color("touch: failed to create: ", COLOR_LIGHT_RED);
        puts(path);
        putchar('\n');
        return -1;
    }
    vfs_close(fd);
    return 0;
}

/* --- rm ---------------------------------------------------------------- */
static int cmd_rm(int argc, char **argv)
{
    if (argc < 2) {
        puts("Usage: rm <file>\n");
        return -1;
    }

    char path[256];
    resolve_path(argv[1], path, sizeof(path));

    int ret = vfs_unlink(path);
    if (ret < 0) {
        puts_color("rm: cannot remove '", COLOR_LIGHT_RED);
        puts(path);
        puts("'\n");
        return -1;
    }

    return 0;
}

/* --- pwd --------------------------------------------------------------- */
static int cmd_pwd(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts(cwd);
    putchar('\n');
    return 0;
}

/* --- mode -------------------------------------------------------------- */
static int cmd_mode(int argc, char **argv)
{
    if (argc < 2) {
        /* Show current mode */
        puts("Current mode: ");
        if (current_mode == OS_MODE_NERD) {
            puts_color("nerd", COLOR_LIGHT_GREEN);
        } else {
            puts_color("gui", COLOR_LIGHT_CYAN);
        }
        puts(" (");
        puts(current_mode == OS_MODE_NERD ? "shell-only" : "graphical");
        puts(")\n");
        puts("Usage: mode <nerd|gui>\n");
        return 0;
    }

    if (strcmp(argv[1], "nerd") == 0) {
        os_set_mode(OS_MODE_NERD);
        puts("Switched to nerd mode (shell-only)\n");
    } else if (strcmp(argv[1], "gui") == 0) {
        puts_color("GUI mode is not yet implemented.\n", COLOR_LIGHT_BROWN);
        puts("Staying in nerd mode.\n");
    } else {
        puts("Unknown mode. Use: mode <nerd|gui>\n");
        return -1;
    }
    return 0;
}

/* --- stat -------------------------------------------------------------- */
static int cmd_stat(int argc, char **argv)
{
    if (argc < 2) {
        puts("Usage: stat <path>\n");
        return -1;
    }

    char path[256];
    resolve_path(argv[1], path, sizeof(path));

    vfs_stat_t st;
    if (vfs_stat(path, &st) < 0) {
        puts_color("stat: cannot stat '", COLOR_LIGHT_RED);
        puts(path);
        puts("'\n");
        return -1;
    }

    printf("  File: %s\n", path);
    printf("  Size: %d bytes\n", (int)st.size);
    printf("  Type: %s\n", (st.attributes & 0x10) ? "directory" : "file");
    printf("  Cluster: %d\n", (int)st.first_cluster);
    return 0;
}

/* --- write ------------------------------------------------------------- */
static int cmd_write(int argc, char **argv)
{
    if (argc < 3) {
        puts("Usage: write <file> <text...>\n");
        return -1;
    }

    char path[256];
    resolve_path(argv[1], path, sizeof(path));

    int fd = vfs_open(path, VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) {
        puts_color("write: cannot open '", COLOR_LIGHT_RED);
        puts(path);
        puts("'\n");
        return -1;
    }

    /* Write all remaining args space-separated */
    for (int i = 2; i < argc; i++) {
        if (i > 2) vfs_write(fd, " ", 1);
        vfs_write(fd, argv[i], strlen(argv[i]));
    }
    vfs_write(fd, "\n", 1);

    vfs_close(fd);
    return 0;
}

/* --- uname ------------------------------------------------------------- */
static int cmd_uname(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts_color("RabinOS v0.1.0 i386 (rosh)\n", COLOR_LIGHT_CYAN);
    return 0;
}

/* --- whoami ------------------------------------------------------------ */
static int cmd_whoami(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts("rabin\n");
    return 0;
}
