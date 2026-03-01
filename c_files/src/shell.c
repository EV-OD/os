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
#include "rosc.h"
#include "kheap.h"
#include "log.h"
#include "pit.h"
/* GUI mode – only the opaque launcher; no color/wm/fb types visible here.
 * This avoids the 32-bit color_t vs unsigned-char conflict in puts_color(). */
#ifdef GUI_MODE
#include "multiboot.h"
#include "gui/gui_init.h"
#include "gui/mu_backend.h"
#include "process.h"
#include "sched.h"
#endif

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
static int cmd_rosc(int argc, char **argv);
static int cmd_sample(int argc, char **argv);
static int cmd_microui(int argc, char **argv);

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
    { "rosc",   "Compile .ros source to .rox",      cmd_rosc   },
    { "sample", "Create sample .ros program (try: sample list)", cmd_sample },
    { "microui","Launch microui demo window",        cmd_microui},
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
/* =========================================================================
 * Helper: check if a string ends with a given suffix
 * ========================================================================= */
static int ends_with(const char *str, const char *suffix)
{
    int slen = strlen(str);
    int xlen = strlen(suffix);
    if (xlen > slen) return 0;
    return strcmp(str + slen - xlen, suffix) == 0;
}

/* =========================================================================
 * Command resolver
 *
 * Resolution order:
 *   1. If cmd starts with "./" or "/" → treat as literal .rox path
 *   2. If cmd ends with ".rox" → resolve relative to cwd
 *   3. Try /bin/<cmd>.rox
 *   4. Search built-in command table
 *   5. "command not found"
 * ========================================================================= */
static int resolve_and_execute(int argc, char **argv)
{
    if (argc == 0) return 0;

    const char *cmd = argv[0];
    vfs_stat_t st;

    /* --- Step 1: Explicit path (starts with / or ./) ------------------- */
    if (cmd[0] == '/') {
        /* Absolute path – run directly */
        if (vfs_stat(cmd, &st) == 0 && !(st.attributes & 0x10)) {
            return rox_load_and_run(cmd, argc, argv);
        }
        puts_color("rosh: ", COLOR_LIGHT_RED);
        puts((char *)cmd);
        puts(": no such file\n");
        return -1;
    }

    if (cmd[0] == '.' && cmd[1] == '/') {
        /* Relative path from cwd */
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
 * Standard library source – embedded strings written to /lib/ros/ at boot
 * ========================================================================= */

static const char LIB_GUI_ROS[] =
    "// /lib/ros/gui.ros  --  RandomOS Standard GUI Library\n"
    "// Usage:  import gui\n"
    "//\n"
    "// Color accessors\n"
    "fn COL_BLACK()  -> i32 { return 0x000000 }\n"
    "fn COL_WHITE()  -> i32 { return 0xFFFFFF }\n"
    "fn COL_RED()    -> i32 { return 0xFF4040 }\n"
    "fn COL_GREEN()  -> i32 { return 0x40CC55 }\n"
    "fn COL_BLUE()   -> i32 { return 0x4488FF }\n"
    "fn COL_YELLOW() -> i32 { return 0xFFDD00 }\n"
    "fn COL_GRAY()   -> i32 { return 0x888888 }\n"
    "fn COL_DGRAY()  -> i32 { return 0x333333 }\n"
    "fn COL_BG()     -> i32 { return 0x0D1117 }\n"
    "fn COL_ACCENT() -> i32 { return 0x4F8EF7 }\n"
    "fn COL_BORDER() -> i32 { return 0x30363D }\n"
    "// Grid layout\n"
    "fn gcell_w(avail: i32, cols: i32, gap: i32) -> i32 {\n"
    "    return (avail - gap * (cols + 1)) / cols\n"
    "}\n"
    "fn gcell_h(avail: i32, rows: i32, gap: i32) -> i32 {\n"
    "    return (avail - gap * (rows + 1)) / rows\n"
    "}\n"
    "fn gcell_x(ox: i32, avail: i32, cols: i32, gap: i32, c: i32) -> i32 {\n"
    "    let cw: i32 = gcell_w(avail, cols, gap)\n"
    "    return ox + gap + c * (cw + gap)\n"
    "}\n"
    "fn gcell_y(oy: i32, avail: i32, rows: i32, gap: i32, r: i32) -> i32 {\n"
    "    let ch: i32 = gcell_h(avail, rows, gap)\n"
    "    return oy + gap + r * (ch + gap)\n"
    "}\n"
    "// Flex layout (horizontal)\n"
    "fn flex_w(avail: i32, n: i32, gap: i32) -> i32 {\n"
    "    return (avail - gap * (n + 1)) / n\n"
    "}\n"
    "fn flex_x(ox: i32, avail: i32, n: i32, gap: i32, i: i32) -> i32 {\n"
    "    let iw: i32 = flex_w(avail, n, gap)\n"
    "    return ox + gap + i * (iw + gap)\n"
    "}\n"
    "// Filled rounded rect (packs x|y and w|h for gui_fill_round syscall)\n"
    "fn gui_rrect(win: i32, x: i32, y: i32, w: i32, h: i32, r: i32, color: i32) {\n"
    "    let xy: i32 = x | (y << 16)\n"
    "    let wh: i32 = w | (h << 16)\n"
    "    gui_fill_round(win, xy, wh, r, color)\n"
    "}\n"
    "// Widget: filled panel\n"
    "fn gui_panel(win: i32, x: i32, y: i32, w: i32, h: i32, color: i32) {\n"
    "    gui_rrect(win, x, y, w, h, 8, color)\n"
    "}\n"
    "// Widget: header bar\n"
    "fn gui_header(win: i32, x: i32, y: i32, w: i32, text: str) {\n"
    "    gui_rrect(win, x, y, w, 22, 5, 0x4F8EF7)\n"
    "    gui_text(win, x + 8, y + 7, text, 0xFFFFFF)\n"
    "}\n"
    "// Widget: text label\n"
    "fn gui_label(win: i32, x: i32, y: i32, text: str, fg: i32) {\n"
    "    gui_text(win, x, y, text, fg)\n"
    "}\n"
    "// Widget: button\n"
    "fn gui_btn(win: i32, x: i32, y: i32, w: i32, h: i32, label: str) {\n"
    "    gui_rrect(win, x, y, w, h, 6, 0x4F8EF7)\n"
    "    gui_pen(win, 0x7ABAFF)\n"
    "    gui_rect(win, x, y, w, h)\n"
    "    let tx: i32 = x + (w - 40) / 2\n"
    "    let ty: i32 = y + (h - 8) / 2\n"
    "    gui_text(win, tx, ty, label, 0xFFFFFF)\n"
    "}\n"
    "// Widget: horizontal separator\n"
    "fn gui_hsep(win: i32, x: i32, y: i32, w: i32) {\n"
    "    gui_pen(win, 0x30363D)\n"
    "    gui_line(win, x, y, x + w, y)\n"
    "}\n"
    "// Widget: progress bar\n"
    "fn gui_progress(win: i32, x: i32, y: i32, w: i32, h: i32, val: i32, maxv: i32, color: i32) {\n"
    "    gui_rrect(win, x, y, w, h, 3, 0x333333)\n"
    "    let fw: i32 = w * val / maxv\n"
    "    gui_rrect(win, x, y, fw, h, 3, color)\n"
    "    gui_pen(win, 0x30363D)\n"
    "    gui_rect(win, x, y, w, h)\n"
    "}\n"
    "// Widget: filled circle badge\n"
    "fn gui_badge(win: i32, cx: i32, cy: i32, r: i32, color: i32) {\n"
    "    gui_fill_circle(win, cx, cy, r, color)\n"
    "    gui_pen(win, 0xFFFFFF)\n"
    "    gui_circle(win, cx, cy, r, 0xFFFFFF)\n"
    "}\n"
    "// Widget: square icon placeholder\n"
    "fn gui_icon(win: i32, x: i32, y: i32, size: i32, color: i32) {\n"
    "    gui_rrect(win, x, y, size, size, 6, color)\n"
    "}\n";

static const char LIB_MATH_ROS[] =
    "// /lib/ros/math.ros  --  RandomOS Standard Math Library\n"
    "// Usage:  import math\n"
    "fn abs(x: i32) -> i32 {\n"
    "    if x < 0 { return 0 - x }\n"
    "    return x\n"
    "}\n"
    "fn min(a: i32, b: i32) -> i32 {\n"
    "    if a < b { return a }\n"
    "    return b\n"
    "}\n"
    "fn max(a: i32, b: i32) -> i32 {\n"
    "    if a > b { return a }\n"
    "    return b\n"
    "}\n"
    "fn clamp(v: i32, lo: i32, hi: i32) -> i32 {\n"
    "    if v < lo { return lo }\n"
    "    if v > hi { return hi }\n"
    "    return v\n"
    "}\n"
    "fn isqrt(n: i32) -> i32 {\n"
    "    if n <= 0 { return 0 }\n"
    "    let mut x: i32 = n\n"
    "    let mut y: i32 = (x + 1) / 2\n"
    "    while y < x {\n"
    "        x = y\n"
    "        y = (x + n / x) / 2\n"
    "    }\n"
    "    return x\n"
    "}\n"
    "fn ipow(base: i32, exp: i32) -> i32 {\n"
    "    let mut r: i32 = 1\n"
    "    let mut e: i32 = exp\n"
    "    let mut b: i32 = base\n"
    "    while e > 0 {\n"
    "        if (e & 1) == 1 { r = r * b }\n"
    "        b = b * b\n"
    "        e = e >> 1\n"
    "    }\n"
    "    return r\n"
    "}\n"
    "fn lerp(a: i32, b: i32, t: i32, scale: i32) -> i32 {\n"
    "    return a + (b - a) * t / scale\n"
    "}\n"
    "fn map_range(v: i32, in_min: i32, in_max: i32, out_min: i32, out_max: i32) -> i32 {\n"
    "    return out_min + (v - in_min) * (out_max - out_min) / (in_max - in_min)\n"
    "}\n";

static const char LIB_IO_ROS[] =
    "// /lib/ros/io.ros  --  RandomOS Standard I/O Library\n"
    "// Usage:  import io\n"
    "fn print_sep() {\n"
    "    println(\"----------------------------------------\")\n"
    "}\n"
    "fn print_header(title: str) {\n"
    "    print_sep()\n"
    "    println(title)\n"
    "    print_sep()\n"
    "}\n";

static void shell_init_stdlib(void)
{
    int fd;
    vfs_mkdir("/lib");
    vfs_mkdir("/lib/ros");

    fd = vfs_open("/lib/ros/gui.ros",  VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd >= 0) { vfs_write(fd, LIB_GUI_ROS,  strlen(LIB_GUI_ROS));  vfs_close(fd); }

    fd = vfs_open("/lib/ros/math.ros", VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd >= 0) { vfs_write(fd, LIB_MATH_ROS, strlen(LIB_MATH_ROS)); vfs_close(fd); }

    fd = vfs_open("/lib/ros/io.ros",   VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd >= 0) { vfs_write(fd, LIB_IO_ROS,   strlen(LIB_IO_ROS));   vfs_close(fd); }

    log_info("[shell] stdlib written to /lib/ros/");
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
        "/lib",
        "/lib/ros",
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

    shell_init_stdlib();

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
#ifdef GUI_MODE
        extern multiboot_info_t *g_multiboot_info;
        if (current_mode == OS_MODE_GUI) {
            puts_color("Already in GUI mode.\n", COLOR_LIGHT_CYAN);
            return 0;
        }
        puts("Initialising GUI...\n");
        if (gui_launch(g_multiboot_info) == 0) {
            os_set_mode(OS_MODE_GUI);
            puts("GUI mode active. Type commands as usual.\n");
        } else {
            puts("GUI init failed: no VESA framebuffer.\n");
            puts("Tip: run QEMU with -vga std\n");
        }
#else
        puts("GUI support not compiled in.\n");
        puts("Rebuild: cmake -B build -DGUI_MODE=ON\n");
#endif
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

/* --- rosc -------------------------------------------------------------- */
static int cmd_rosc(int argc, char **argv)
{
    if (argc < 2) {
        puts("Usage: rosc [-f] <input.ros> [output.rox]\n");
        puts("  -f  overwrite existing output file\n");
        puts("  Compile a .ros source file into a .rox executable.\n");
        return -1;
    }

    int  force = 0;
    int  arg_start = 1;
    if (strcmp(argv[1], "-f") == 0) {
        force = 1;
        arg_start = 2;
        if (argc < 3) {
            puts("Usage: rosc -f <input.ros> [output.rox]\n");
            return -1;
        }
    }

    char src_path[256];
    resolve_path(argv[arg_start], src_path, sizeof(src_path));

    const char *out = (argc > arg_start + 1) ? argv[arg_start + 1] : (const char *)0;
    char out_path[256];

    if (out) {
        resolve_path(out, out_path, sizeof(out_path));
        return rosc_compile(src_path, out_path, force);
    }

    return rosc_compile(src_path, (const char *)0, force);
}

/* --- sample ------------------------------------------------------------ */

/* Sample program templates */
static const char sample_hello[] =
    "// Hello from RandomOS!\n"
    "//\n"
    "// Compile:  rosc hello.ros\n"
    "// Run:      ./hello.rox\n"
    "\n"
    "print(\"Hello, RandomOS!\\n\")\n"
    "\n"
    "let x: i32 = 42\n"
    "let y: i32 = x * 2 + 8\n"
    "let answer: i32 = (x + y) / 2\n"
    "\n"
    "print(\"The answer is: \")\n"
    "print(answer)\n";

static const char sample_strings[] =
    "// String printing demo\n"
    "//\n"
    "// Compile:  rosc strings.ros\n"
    "// Run:      ./strings.rox\n"
    "\n"
    "print(\"=== RandomOS String Demo ===\\n\")\n"
    "print(\"\\n\")\n"
    "print(\"Welcome to RandomOS!\\n\")\n"
    "print(\"This program prints strings.\\n\")\n"
    "print(\"\\n\")\n"
    "print(\"Tab stops:\\n\")\n"
    "print(\"\\tFirst\\n\")\n"
    "print(\"\\t\\tSecond\\n\")\n"
    "print(\"\\t\\t\\tThird\\n\")\n"
    "print(\"\\n\")\n"
    "print(\"Multi-line:\\n\")\n"
    "print(\"Line 1\\nLine 2\\nLine 3\\n\")\n"
    "print(\"\\n\")\n"
    "print(\"Goodbye!\\n\")\n";

static const char sample_math[] =
    "// Math operations demo\n"
    "//\n"
    "// Compile:  rosc math.ros\n"
    "// Run:      ./math.rox\n"
    "\n"
    "print(\"=== Math Demo ===\\n\")\n"
    "\n"
    "let a: i32 = 100\n"
    "let b: i32 = 37\n"
    "\n"
    "let sum: i32 = a + b\n"
    "let diff: i32 = a - b\n"
    "let prod: i32 = a * b\n"
    "let quot: i32 = a / b\n"
    "\n"
    "print(\"100 + 37 = \")\n"
    "print(sum)\n"
    "print(\"100 - 37 = \")\n"
    "print(diff)\n"
    "print(\"100 * 37 = \")\n"
    "print(prod)\n"
    "print(\"100 / 37 = \")\n"
    "print(quot)\n"
    "\n"
    "let nested: i32 = (a + b) * (a - b) / 10\n"
    "print(\"(100+37)*(100-37)/10 = \")\n"
    "print(nested)\n";

static const char sample_fib[] =
    "// Fibonacci sequence (compile-time)\n"
    "//\n"
    "// Compile:  rosc fib.ros\n"
    "// Run:      ./fib.rox\n"
    "\n"
    "print(\"=== Fibonacci ===\\n\")\n"
    "\n"
    "let f0: i32 = 0\n"
    "let f1: i32 = 1\n"
    "let f2: i32 = f0 + f1\n"
    "let f3: i32 = f1 + f2\n"
    "let f4: i32 = f2 + f3\n"
    "let f5: i32 = f3 + f4\n"
    "let f6: i32 = f4 + f5\n"
    "let f7: i32 = f5 + f6\n"
    "let f8: i32 = f6 + f7\n"
    "let f9: i32 = f7 + f8\n"
    "let f10: i32 = f8 + f9\n"
    "\n"
    "print(f0)\n"
    "print(f1)\n"
    "print(f2)\n"
    "print(f3)\n"
    "print(f4)\n"
    "print(f5)\n"
    "print(f6)\n"
    "print(f7)\n"
    "print(f8)\n"
    "print(f9)\n"
    "print(f10)\n";

static const char sample_loops[] =
    "// Loops and conditionals demo\n"
    "//\n"
    "// Compile:  rosc loops.ros\n"
    "// Run:      ./loops.rox\n"
    "\n"
    "fn main() {\n"
    "    print(\"=== Loops Demo ===\\n\")\n"
    "\n"
    "    // Count 1 to 5 with while\n"
    "    print(\"Counting up:\\n\")\n"
    "    let mut i: i32 = 1\n"
    "    while i <= 5 {\n"
    "        print(i)\n"
    "        i = i + 1\n"
    "    }\n"
    "\n"
    "    // Countdown with while\n"
    "    print(\"Counting down:\\n\")\n"
    "    let mut n: i32 = 5\n"
    "    while n > 0 {\n"
    "        print(n)\n"
    "        n = n - 1\n"
    "    }\n"
    "\n"
    "    // if / else\n"
    "    print(\"Even/odd check:\\n\")\n"
    "    let mut k: i32 = 0\n"
    "    while k < 6 {\n"
    "        if k % 2 == 0 {\n"
    "            print(\"even\\n\")\n"
    "        } else {\n"
    "            print(\"odd\\n\")\n"
    "        }\n"
    "        k = k + 1\n"
    "    }\n"
    "}\n";

static const char sample_funcs[] =
    "// Functions demo\n"
    "//\n"
    "// Compile:  rosc funcs.ros\n"
    "// Run:      ./funcs.rox\n"
    "\n"
    "fn add(a: i32, b: i32) -> i32 {\n"
    "    return a + b\n"
    "}\n"
    "\n"
    "fn factorial(n: i32) -> i32 {\n"
    "    if n <= 1 {\n"
    "        return 1\n"
    "    }\n"
    "    return n * factorial(n - 1)\n"
    "}\n"
    "\n"
    "fn max(a: i32, b: i32) -> i32 {\n"
    "    if a > b {\n"
    "        return a\n"
    "    }\n"
    "    return b\n"
    "}\n"
    "\n"
    "fn main() {\n"
    "    print(\"=== Functions Demo ===\\n\")\n"
    "\n"
    "    let s: i32 = add(12, 30)\n"
    "    print(\"12 + 30 = \")\n"
    "    print(s)\n"
    "\n"
    "    print(\"5! = \")\n"
    "    print(factorial(5))\n"
    "\n"
    "    print(\"max(18, 42) = \")\n"
    "    print(max(18, 42))\n"
    "}\n";

static const char sample_gui[] =
    "// GUI Library Demo\n"
    "//\n"
    "// Uses the standard GUI library from /lib/ros/gui.ros.\n"
    "// The 'import gui' line inlines all helper functions\n"
    "// (grid layout, flex layout, widgets) before compilation.\n"
    "//\n"
    "// Compile:  rosc gui.ros\n"
    "// Run:      ./gui.rox\n"
    "\n"
    "import gui\n"
    "\n"
    "fn main() {\n"
    "    // Open window\n"
    "    let win: i32 = gui_window(60, 40, 380, 240, \"GUI Library Demo\")\n"
    "    gui_fill(win, COL_BG())\n"
    "\n"
    "    // Header bar + separator\n"
    "    gui_header(win, 10, 10, 360, \"RandomOS  GUI  Demo\")\n"
    "    gui_hsep(win, 10, 40, 360)\n"
    "\n"
    "    // Three-column grid of coloured panels\n"
    "    let cw: i32 = gcell_w(360, 3, 8)\n"
    "    let p0: i32 = gcell_x(10, 360, 3, 8, 0)\n"
    "    let p1: i32 = gcell_x(10, 360, 3, 8, 1)\n"
    "    let p2: i32 = gcell_x(10, 360, 3, 8, 2)\n"
    "    gui_panel(win, p0, 50, cw, 58, COL_RED())\n"
    "    gui_panel(win, p1, 50, cw, 58, COL_GREEN())\n"
    "    gui_panel(win, p2, 50, cw, 58, COL_ACCENT())\n"
    "    gui_label(win, p0 + 8, 76, \"Red\",   COL_WHITE())\n"
    "    gui_label(win, p1 + 8, 76, \"Green\", COL_WHITE())\n"
    "    gui_label(win, p2 + 8, 76, \"Blue\",  COL_WHITE())\n"
    "\n"
    "    // Progress bar\n"
    "    gui_label(win, 10, 122, \"Progress:\", COL_WHITE())\n"
    "    gui_progress(win, 10, 137, 360, 14, 70, 100, COL_ACCENT())\n"
    "\n"
    "    // Badge row\n"
    "    gui_badge(win, 28,  175, 13, COL_RED())\n"
    "    gui_badge(win, 62,  175, 13, COL_GREEN())\n"
    "    gui_badge(win, 96,  175, 13, COL_ACCENT())\n"
    "    gui_badge(win, 130, 175, 13, COL_YELLOW())\n"
    "\n"
    "    // Flex-layout buttons\n"
    "    let bw: i32 = flex_w(360, 2, 10)\n"
    "    gui_btn(win, flex_x(10, 360, 2, 10, 0), 158, bw, 24, \"Confirm\")\n"
    "    gui_btn(win, flex_x(10, 360, 2, 10, 1), 158, bw, 24, \"Cancel\")\n"
    "\n"
    "    // Icons\n"
    "    gui_icon(win, 160, 158, 24, COL_RED())\n"
    "    gui_icon(win, 192, 158, 24, COL_GREEN())\n"
    "\n"
    "    gui_flush(win)\n"
    "\n"
    "    // Wait for 'q' (ASCII 113) to quit\n"
    "    let mut key: i32 = 0\n"
    "    while key != 113 {\n"
    "        key = gui_wait(win)\n"
    "    }\n"
    "    gui_close(win)\n"
    "}\n";

static const char sample_test[] =
    "// test_all.ros -- ROX Language Feature Test Suite\n"
    "// Tests all non-GUI features: arithmetic, variables, comparisons,\n"
    "// logical, bitwise, if/else, while, for, break, continue,\n"
    "// functions, recursion, nested calls, type cast.\n"
    "//\n"
    "// Compile:  rosc test_all.ros\n"
    "// Run:      ./test_all.rox\n"
    "\n"
    "fn add(a: i32, b: i32) -> i32 { return a + b }\n"
    "fn mul(a: i32, b: i32) -> i32 { return a * b }\n"
    "fn factorial(n: i32) -> i32 {\n"
    "    if n <= 1 { return 1 }\n"
    "    return n * factorial(n - 1)\n"
    "}\n"
    "fn fib(n: i32) -> i32 {\n"
    "    if n <= 1 { return n }\n"
    "    return fib(n - 1) + fib(n - 2)\n"
    "}\n"
    "fn sum_to(n: i32) -> i32 {\n"
    "    let mut s: i32 = 0\n"
    "    let mut i: i32 = 1\n"
    "    while i <= n { s += i    i += 1 }\n"
    "    return s\n"
    "}\n"
    "\n"
    "fn main() {\n"
    "    println(\"=== ROX Test Suite ===\")\n"
    "    let mut pass: i32 = 0\n"
    "    let mut fail: i32 = 0\n"
    "\n"
    "    // 1. Arithmetic\n"
    "    println(\"-- Arithmetic --\")\n"
    "    if 2 + 3 == 5       { pass += 1 } else { fail += 1 }\n"
    "    if 10 - 4 == 6      { pass += 1 } else { fail += 1 }\n"
    "    if 6 * 7 == 42      { pass += 1 } else { fail += 1 }\n"
    "    if 10 / 3 == 3      { pass += 1 } else { fail += 1 }\n"
    "    if 10 % 3 == 1      { pass += 1 } else { fail += 1 }\n"
    "    if 2 + 3 * 4 == 14  { pass += 1 } else { fail += 1 }\n"
    "    if (2 + 3) * 4 == 20 { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 2. Variables + compound assignment\n"
    "    println(\"-- Variables --\")\n"
    "    let mut v: i32 = 10\n"
    "    v += 5    if v == 15 { pass += 1 } else { fail += 1 }\n"
    "    v -= 3    if v == 12 { pass += 1 } else { fail += 1 }\n"
    "    v *= 2    if v == 24 { pass += 1 } else { fail += 1 }\n"
    "    v /= 3    if v == 8  { pass += 1 } else { fail += 1 }\n"
    "    v %= 5    if v == 3  { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 3. Comparisons\n"
    "    println(\"-- Comparisons --\")\n"
    "    if 5 < 10   { pass += 1 } else { fail += 1 }\n"
    "    if 10 > 5   { pass += 1 } else { fail += 1 }\n"
    "    if 5 <= 5   { pass += 1 } else { fail += 1 }\n"
    "    if 10 >= 10 { pass += 1 } else { fail += 1 }\n"
    "    if 5 == 5   { pass += 1 } else { fail += 1 }\n"
    "    if 5 != 10  { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 4. Logical\n"
    "    println(\"-- Logical --\")\n"
    "    if 1 && 1        { pass += 1 } else { fail += 1 }\n"
    "    if !(1 && 0)     { pass += 1 } else { fail += 1 }\n"
    "    if 1 || 0        { pass += 1 } else { fail += 1 }\n"
    "    if !(0 || 0)     { pass += 1 } else { fail += 1 }\n"
    "    if !0            { pass += 1 } else { fail += 1 }\n"
    "    if !!1           { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 5. Bitwise\n"
    "    println(\"-- Bitwise --\")\n"
    "    if (0xFF & 0x0F) == 15  { pass += 1 } else { fail += 1 }\n"
    "    if (0xF0 | 0x0F) == 255 { pass += 1 } else { fail += 1 }\n"
    "    if (0xFF ^ 0x0F) == 240 { pass += 1 } else { fail += 1 }\n"
    "    if (1 << 4) == 16       { pass += 1 } else { fail += 1 }\n"
    "    if (256 >> 3) == 32     { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 6. If / else-if / else\n"
    "    println(\"-- If/Else --\")\n"
    "    let mut r: i32 = 0\n"
    "    if 7 > 10 { r = 1 } else if 7 > 5 { r = 2 } else { r = 3 }\n"
    "    if r == 2 { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 7. While loop + break + continue\n"
    "    println(\"-- Loops --\")\n"
    "    if sum_to(10) == 55    { pass += 1 } else { fail += 1 }\n"
    "    if sum_to(100) == 5050 { pass += 1 } else { fail += 1 }\n"
    "    let mut cnt: i32 = 0\n"
    "    let mut idx: i32 = 0\n"
    "    while idx < 100 {\n"
    "        if cnt == 5 { break }\n"
    "        cnt += 1    idx += 1\n"
    "    }\n"
    "    if cnt == 5 { pass += 1 } else { fail += 1 }\n"
    "    let mut evens: i32 = 0\n"
    "    let mut k: i32 = 0\n"
    "    while k < 10 {\n"
    "        k += 1\n"
    "        if k % 2 != 0 { continue }\n"
    "        evens += k\n"
    "    }\n"
    "    if evens == 30 { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 8. For loop (range)\n"
    "    println(\"-- For --\")\n"
    "    let mut fs: i32 = 0\n"
    "    for fi in 1..11 { fs += fi }\n"
    "    if fs == 55 { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 9. Functions\n"
    "    println(\"-- Functions --\")\n"
    "    if add(3, 4) == 7  { pass += 1 } else { fail += 1 }\n"
    "    if mul(6, 7) == 42 { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 10. Recursion\n"
    "    println(\"-- Recursion --\")\n"
    "    if factorial(5) == 120 { pass += 1 } else { fail += 1 }\n"
    "    if factorial(10) == 3628800 { pass += 1 } else { fail += 1 }\n"
    "    if fib(7) == 13  { pass += 1 } else { fail += 1 }\n"
    "    if fib(10) == 55 { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 11. Nested calls\n"
    "    println(\"-- Nested calls --\")\n"
    "    if add(fib(5), factorial(3)) == 11 { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // Summary\n"
    "    println(\"=== Results ===\")\n"
    "    print(\"PASS: \")    println(pass)\n"
    "    print(\"FAIL: \")    println(fail)\n"
    "    if fail == 0 { println(\"ALL TESTS PASSED!\") }\n"
    "    else         { println(\"SOME TESTS FAILED\") }\n"
    "}\n";

struct sample_entry {
    const char *name;
    const char *file;
    const char *src;
    const char *desc;
};

static const char sample_mu[] =
    "// mu_demo.ros - A microui-style dark-themed GUI demo\n"
    "// Written in ROX (RandomOS's own programming language)\n"
    "// Demonstrates panels, buttons, labels, progress bar, and keyboard interaction\n"
    "// Uses only the built-in gui_* syscalls\n"
    "\n"
    "fn draw_button(win: i32, x: i32, y: i32, w: i32, h: i32, label: str, hover: i32) {\n"
    "    // Button background\n"
    "    if hover == 1 {\n"
    "        gui_pen(win, 0x5F5F5F)\n"
    "    } else {\n"
    "        gui_pen(win, 0x4B4B4B)\n"
    "    }\n"
    "    gui_fill_rect(win, x, y, w, h)\n"
    "\n"
    "    // Button border\n"
    "    gui_pen(win, 0x191919)\n"
    "    gui_rect(win, x, y, w, h)\n"
    "\n"
    "    // Button label (centered-ish)\n"
    "    gui_text(win, x + 8, y + 6, label, 0xE6E6E6)\n"
    "}\n"
    "\n"
    "fn draw_checkbox(win: i32, x: i32, y: i32, label: str, checked: i32) {\n"
    "    // Box\n"
    "    gui_pen(win, 0x1E1E1E)\n"
    "    gui_fill_rect(win, x, y, 16, 16)\n"
    "    gui_pen(win, 0x191919)\n"
    "    gui_rect(win, x, y, 16, 16)\n"
    "\n"
    "    // Check mark\n"
    "    if checked == 1 {\n"
    "        gui_text(win, x + 4, y + 4, \"*\", 0xE6E6E6)\n"
    "    }\n"
    "\n"
    "    // Label\n"
    "    gui_text(win, x + 22, y + 4, label, 0xE6E6E6)\n"
    "}\n"
    "\n"
    "fn draw_progress(win: i32, x: i32, y: i32, w: i32, h: i32, value: i32, max_val: i32) {\n"
    "    // Background\n"
    "    gui_pen(win, 0x1E1E1E)\n"
    "    gui_fill_rect(win, x, y, w, h)\n"
    "\n"
    "    // Filled portion\n"
    "    let fill_w: i32 = (value * w) / max_val\n"
    "    if fill_w > w {\n"
    "        fill_w = w\n"
    "    }\n"
    "    gui_pen(win, 0x2255AA)\n"
    "    gui_fill_rect(win, x, y, fill_w, h)\n"
    "\n"
    "    // Border\n"
    "    gui_pen(win, 0x191919)\n"
    "    gui_rect(win, x, y, w, h)\n"
    "}\n"
    "\n"
    "fn draw_panel(win: i32, x: i32, y: i32, w: i32, h: i32) {\n"
    "    gui_pen(win, 0x323232)\n"
    "    gui_fill_rect(win, x, y, w, h)\n"
    "    gui_pen(win, 0x191919)\n"
    "    gui_rect(win, x, y, w, h)\n"
    "}\n"
    "\n"
    "fn main() {\n"
    "    let win: i32 = gui_window(60, 40, 400, 310, \"ROX UI Demo\")\n"
    "    let c: i32 = 0\n"
    "    mut counter: i32 = 0\n"
    "    mut checked: i32 = 0\n"
    "    mut progress: i32 = 25\n"
    "    mut selected: i32 = 0\n"
    "    mut redraw: i32 = 1\n"
    "\n"
    "    while c != 113 {\n"
    "        if redraw == 1 {\n"
    "            // Clear canvas with dark background\n"
    "            gui_fill(win, 0x2E2E2E)\n"
    "\n"
    "            // Title label\n"
    "            gui_text(win, 10, 8, \"microui-style Demo (ROX)\", 0xE6E6E6)\n"
    "\n"
    "            // Separator line\n"
    "            gui_pen(win, 0x191919)\n"
    "            gui_line(win, 10, 22, 380, 22)\n"
    "\n"
    "            // --- Buttons panel ---\n"
    "            draw_panel(win, 8, 30, 380, 50)\n"
    "            gui_text(win, 14, 36, \"Buttons:\", 0xA0A0A0)\n"
    "\n"
    "            // Highlight the selected button\n"
    "            if selected == 0 {\n"
    "                draw_button(win, 80, 36, 90, 20, \"Click [1]\", 1)\n"
    "            } else {\n"
    "                draw_button(win, 80, 36, 90, 20, \"Click [1]\", 0)\n"
    "            }\n"
    "\n"
    "            if selected == 1 {\n"
    "                draw_button(win, 180, 36, 90, 20, \"Reset [2]\", 1)\n"
    "            } else {\n"
    "                draw_button(win, 180, 36, 90, 20, \"Reset [2]\", 0)\n"
    "            }\n"
    "\n"
    "            // Counter display\n"
    "            if counter == 0 { gui_text(win, 280, 42, \"Count: 0\", 0xE6E6E6) }\n"
    "            if counter == 1 { gui_text(win, 280, 42, \"Count: 1\", 0xE6E6E6) }\n"
    "            if counter == 2 { gui_text(win, 280, 42, \"Count: 2\", 0xE6E6E6) }\n"
    "            if counter == 3 { gui_text(win, 280, 42, \"Count: 3\", 0xE6E6E6) }\n"
    "            if counter == 4 { gui_text(win, 280, 42, \"Count: 4\", 0xE6E6E6) }\n"
    "            if counter == 5 { gui_text(win, 280, 42, \"Count: 5\", 0xE6E6E6) }\n"
    "            if counter > 5  { gui_text(win, 280, 42, \"Count: 5+\", 0xE6E6E6) }\n"
    "\n"
    "            // Status line at bottom of buttons panel\n"
    "            gui_text(win, 14, 60, \"Press 1/2 to click buttons\", 0x606060)\n"
    "\n"
    "            // --- Checkbox panel ---\n"
    "            draw_panel(win, 8, 86, 380, 40)\n"
    "            draw_checkbox(win, 14, 96, \"Enable feature [space]\", checked)\n"
    "\n"
    "            // --- Progress panel ---\n"
    "            draw_panel(win, 8, 132, 380, 50)\n"
    "            gui_text(win, 14, 138, \"Progress (A/D to adjust):\", 0xA0A0A0)\n"
    "            draw_progress(win, 14, 156, 368, 16, progress, 100)\n"
    "\n"
    "            // --- Info panel ---\n"
    "            draw_panel(win, 8, 188, 380, 72)\n"
    "            gui_text(win, 14, 194, \"Keyboard Controls:\", 0xA0A0A0)\n"
    "            gui_text(win, 14, 210, \"1 = Click  2 = Reset  Space = Toggle\", 0x808080)\n"
    "            gui_text(win, 14, 226, \"A/D = Progress  Q = Quit\", 0x808080)\n"
    "            gui_text(win, 14, 242, \"Tab = Switch button focus\", 0x808080)\n"
    "\n"
    "            gui_flush(win)\n"
    "            redraw = 0\n"
    "        }\n"
    "\n"
    "        c = gui_wait(win)\n"
    "\n"
    "        // Handle key: '1' = click button\n"
    "        if c == 49 { selected = 0    counter = counter + 1    redraw = 1 }\n"
    "\n"
    "        // Handle key: '2' = reset\n"
    "        if c == 50 { selected = 1    counter = 0    progress = 25    checked = 0    redraw = 1 }\n"
    "\n"
    "        // Handle key: space = toggle checkbox\n"
    "        if c == 32 {\n"
    "            if checked == 0 { checked = 1 } else { checked = 0 }\n"
    "            redraw = 1\n"
    "        }\n"
    "\n"
    "        // Handle key: 'a' = decrease progress\n"
    "        if c == 97 {\n"
    "            progress = progress - 5\n"
    "            if progress < 0 { progress = 0 }\n"
    "            redraw = 1\n"
    "        }\n"
    "\n"
    "        // Handle key: 'd' = increase progress\n"
    "        if c == 100 {\n"
    "            progress = progress + 5\n"
    "            if progress > 100 { progress = 100 }\n"
    "            redraw = 1\n"
    "        }\n"
    "\n"
    "        // Handle key: tab = switch focus\n"
    "        if c == 9 {\n"
    "            if selected == 0 { selected = 1 } else { selected = 0 }\n"
    "            redraw = 1\n"
    "        }\n"
    "    }\n"
    "\n"
    "    gui_close(win)\n"
    "}\n";

static const struct sample_entry sample_table[] = {
    { "hello",   "hello.ros",   sample_hello,   "Hello + variables"       },
    { "strings", "strings.ros", sample_strings, "String printing"         },
    { "math",    "math.ros",    sample_math,    "Math operations"         },
    { "fib",     "fib.ros",     sample_fib,     "Fibonacci sequence"      },
    { "loops",   "loops.ros",   sample_loops,   "While loops + if/else"   },
    { "funcs",   "funcs.ros",   sample_funcs,   "Functions + recursion"   },
    { "gui",     "gui.ros",     sample_gui,     "GUI library: import, widgets, grid/flex layout" },
    { "mu",      "mu_demo.ros", sample_mu,      "microui-style GUI: panels, buttons, checkbox, progress bar" },
    { "test",    "test_all.ros", sample_test,   "Full language feature test (PASS/FAIL report)"  },
};

#define SAMPLE_COUNT (sizeof(sample_table) / sizeof(sample_table[0]))

static int cmd_sample(int argc, char **argv)
{
    /* No arguments or "list" → show available templates */
    if (argc < 2 || strcmp(argv[1], "list") == 0) {
        puts_color("Available sample programs:\n", COLOR_WHITE);
        unsigned int i;
        for (i = 0; i < SAMPLE_COUNT; i++) {
            puts_color("  sample ", COLOR_DARK_GREY);
            puts_color(sample_table[i].name, COLOR_LIGHT_GREEN);
            puts_color("  - ", COLOR_DARK_GREY);
            puts((char *)sample_table[i].desc);
            putchar('\n');
        }
        puts_color("\nUsage: sample <name>\n", COLOR_DARK_GREY);
        return 0;
    }

    /* Find the matching template */
    const struct sample_entry *entry = (const struct sample_entry *)0;
    unsigned int i;
    for (i = 0; i < SAMPLE_COUNT; i++) {
        if (strcmp(argv[1], sample_table[i].name) == 0) {
            entry = &sample_table[i];
            break;
        }
    }

    if (!entry) {
        puts_color("sample: unknown template '", COLOR_LIGHT_RED);
        puts(argv[1]);
        puts("'. Use 'sample list' to see available templates.\n");
        return -1;
    }

    char path[256];
    resolve_path(entry->file, path, sizeof(path));

    /* Check if file already exists */
    vfs_stat_t st;
    if (vfs_stat(path, &st) == 0) {
        puts_color("sample: file already exists: ", COLOR_LIGHT_BROWN);
        puts(path);
        putchar('\n');
        return -1;
    }

    int fd = vfs_open(path, VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) {
        puts_color("sample: cannot create: ", COLOR_LIGHT_RED);
        puts(path);
        putchar('\n');
        return -1;
    }

    vfs_write(fd, entry->src, strlen(entry->src));
    vfs_close(fd);

    puts_color("  created: ", COLOR_LIGHT_GREEN);
    puts(path);
    putchar('\n');
    puts_color("  compile: ", COLOR_DARK_GREY);
    puts("rosc ");
    puts(path);
    putchar('\n');
    return 0;
}

/* -------------------------------------------------------------------------
 * microui – launch the microui demo as a kernel process
 * ------------------------------------------------------------------------- */
static int cmd_microui(int argc, char **argv)
{
    (void)argc;
    (void)argv;
#ifdef GUI_MODE
    process_t *p = process_create("mu_demo", mu_demo_run, 0);
    if (!p) {
        puts_color("microui: failed to create process\n", COLOR_LIGHT_RED);
        return -1;
    }
    sched_add(p);
    puts("microui demo launched\n");
    int status = process_wait(p->pid);
    process_destroy(p);
    return status;
#else
    puts("microui requires GUI mode\n");
    return -1;
#endif
}
