/* shell.c - RabinOS Shell (rosh) - Main module */

#include "shell.h"
#include "shell/shell_core.h"
#include "shell/shell_cmds.h"
#include "shell/shell_stdlib.h"
#include "shell/shell_apps.h"
#include "shell/shell_sample.h"
#include "stdio.h"
#include "string.h"
#include "vfs.h"
#include "log.h"

/* -------------------------------------------------------------------------
 * OS mode state
 * ------------------------------------------------------------------------- */
os_mode_t current_mode = OS_MODE_NERD;

os_mode_t os_get_mode(void) { return current_mode; }
void os_set_mode(os_mode_t mode) { current_mode = mode; }

/* -------------------------------------------------------------------------
 * Current working directory
 * ------------------------------------------------------------------------- */
static char cwd[256] = "/";

const char* shell_get_cwd(void) { return cwd; }

void shell_set_cwd(const char* path)
{
    strncpy(cwd, path, sizeof(cwd) - 1);
    cwd[sizeof(cwd) - 1] = '\0';
}

/* -------------------------------------------------------------------------
 * Built-in command table
 * ------------------------------------------------------------------------- */
const builtin_cmd_t builtins[] = {
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
    { "rosc",   "Compile .ros source to .rox",      cmd_rosc   },
    { "sample", "Create sample .ros program (try: sample list)", cmd_sample },
    { "microui","Launch microui demo window",        cmd_microui},
    { "setup",  "Install rxt, term, paint to /bin",  cmd_setup  },
    { "desktop","Manage desktop icons (desktop add <name|path.rox>)", cmd_desktop },
    { (void *)0, (void *)0, (void *)0 }
};

/* -------------------------------------------------------------------------
 * Shell initialization
 * ------------------------------------------------------------------------- */
void shell_init(void)
{
    log_info("[shell] creating OS directory structure");

    static const char *dirs[] = {
        "/bin", "/etc", "/home", "/tmp", "/var", "/var/log", "/lib", "/lib/ros",
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

    /* Create /etc/motd */
    fd = vfs_open("/etc/motd", VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd >= 0) {
        const char *motd =
            "Welcome to RabinOS v0.1.0\n"
            "Type 'help' for available commands.\n";
        vfs_write(fd, motd, strlen(motd));
        vfs_close(fd);
    }

    shell_init_stdlib();

    log_info("[shell] OS directory structure ready");
}

/* -------------------------------------------------------------------------
 * Shell main loop
 * ------------------------------------------------------------------------- */
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
        if (len < 0) break;
        if (len == 0) continue;

        int argc = tokenise(input, argv, SHELL_MAX_ARGS);
        if (argc == 0) continue;

        resolve_and_execute(argc, argv);
    }
}
