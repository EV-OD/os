/* shell_cmds.c - Built-in shell commands */

#include "shell/shell_cmds.h"
#include "shell/shell_core.h"
#include "stdio.h"
#include "terminal.h"
#include "string.h"
#include "vfs.h"
#include "rosc.h"
#include "log.h"

#ifdef GUI_MODE
#include "multiboot.h"
#include "gui/gui_init.h"
#include "gui/desktop.h"
#endif

extern os_mode_t current_mode;
extern void os_set_mode(os_mode_t mode);

int cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;
    extern const builtin_cmd_t builtins[];
    
    puts_color("\nRabinOS Shell (rosh) - Available Commands\n", COLOR_LIGHT_CYAN);
    puts_color("=========================================\n", COLOR_DARK_GREY);

    for (int i = 0; builtins[i].name != (void *)0; i++) {
        puts_color("  ", COLOR_WHITE);
        char padded[16];
        memset(padded, ' ', sizeof(padded));
        strncpy(padded, builtins[i].name, strlen(builtins[i].name));
        padded[10] = '\0';
        puts_color(padded, COLOR_LIGHT_GREEN);
        puts_color(builtins[i].description, COLOR_LIGHT_GREY);
        putchar('\n');
    }

    puts_color("\n  External commands in /bin/*.rox are also available.\n\n", COLOR_DARK_GREY);
    return 0;
}

struct ls_ctx {
    int count;
    int show_all;
};

static int ls_callback(const vfs_dirent_t *d, void *userdata)
{
    struct ls_ctx *ctx = (struct ls_ctx *)userdata;

    if (!ctx->show_all) {
        if (strcmp(d->name, ".") == 0 || strcmp(d->name, "..") == 0)
            return 0;
    }

    if (d->attributes & 0x10) {
        puts_color(d->name, COLOR_LIGHT_BLUE);
        puts_color("/", COLOR_LIGHT_BLUE);
    } else {
        unsigned int nlen = strlen(d->name);
        if (nlen > 4 && strcmp(d->name + nlen - 4, ".rox") == 0) {
            puts_color(d->name, COLOR_LIGHT_GREEN);
        } else {
            puts((char *)d->name);
        }
    }
    puts("  ");
    ctx->count++;

    if (ctx->count % 5 == 0) putchar('\n');
    return 0;
}

int cmd_ls(int argc, char **argv)
{
    char path[256];
    struct ls_ctx ctx = { 0, 0 };

    const char *target = (void *)0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0) {
            ctx.show_all = 1;
        } else {
            target = argv[i];
        }
    }

    if (target) {
        resolve_path(target, path, sizeof(path));
    } else {
        strcpy(path, shell_get_cwd());
    }

    int ret = vfs_readdir(path, ls_callback, &ctx);
    if (ret < 0) {
        puts_color("ls: cannot access '", COLOR_LIGHT_RED);
        puts(path);
        puts("'\n");
        return -1;
    }

    if (ctx.count > 0 && ctx.count % 5 != 0) putchar('\n');
    if (ctx.count == 0) puts_color("(empty)\n", COLOR_DARK_GREY);
    return 0;
}

int cmd_cd(int argc, char **argv)
{
    if (argc < 2) {
        shell_set_cwd("/");
        return 0;
    }

    const char *target = argv[1];

    if (strcmp(target, "..") == 0) {
        const char *cwd = shell_get_cwd();
        unsigned int len = strlen(cwd);
        if (len <= 1) return 0;
        
        char new_cwd[256];
        strcpy(new_cwd, cwd);
        int last_slash = -1;
        for (int i = (int)len - 1; i >= 0; i--) {
            if (new_cwd[i] == '/') { last_slash = i; break; }
        }
        if (last_slash <= 0) {
            shell_set_cwd("/");
        } else {
            new_cwd[last_slash] = '\0';
            shell_set_cwd(new_cwd);
        }
        return 0;
    }

    char path[256];
    resolve_path(target, path, sizeof(path));

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

    shell_set_cwd(path);
    return 0;
}

int cmd_cat(int argc, char **argv)
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
    putchar('\n');
    return 0;
}

int cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1) putchar(' ');
        puts(argv[i]);
    }
    putchar('\n');
    return 0;
}

int cmd_clear(int argc, char **argv)
{
    (void)argc; (void)argv;
    terminal_t *t = term_active();
    if (t && t->clear)
        t->clear();
    else
        fb_clear();
    cursor_move_home();
    return 0;
}

int cmd_mkdir(int argc, char **argv)
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

int cmd_touch(int argc, char **argv)
{
    if (argc < 2) {
        puts("Usage: touch <file>\n");
        return -1;
    }

    char path[256];
    resolve_path(argv[1], path, sizeof(path));

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

int cmd_rm(int argc, char **argv)
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

int cmd_pwd(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts((char *)shell_get_cwd());
    putchar('\n');
    return 0;
}

int cmd_mode(int argc, char **argv)
{
    if (argc < 2) {
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

int cmd_stat(int argc, char **argv)
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

int cmd_write(int argc, char **argv)
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

    for (int i = 2; i < argc; i++) {
        if (i > 2) vfs_write(fd, " ", 1);
        vfs_write(fd, argv[i], strlen(argv[i]));
    }
    vfs_write(fd, "\n", 1);

    vfs_close(fd);
    return 0;
}

int cmd_uname(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts_color("RabinOS v0.1.0 i386 (rosh)\n", COLOR_LIGHT_CYAN);
    return 0;
}

int cmd_whoami(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts("rabin\n");
    return 0;
}

int cmd_rosc(int argc, char **argv)
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

int cmd_desktop(int argc, char **argv)
{
    if (argc < 3 || (strcmp(argv[1], "add") != 0 &&
                      strcmp(argv[1], "delete") != 0 &&
                      strcmp(argv[1], "del") != 0)) {
        puts("Usage:\n"
             "  desktop add <name|path.rox>  add icon (name → /bin/<name>.rox)\n"
             "  desktop delete <label>       remove icon by label\n");
        return -1;
    }

#ifdef GUI_MODE
    if (strcmp(argv[1], "delete") == 0 || strcmp(argv[1], "del") == 0) {
        const char *label = argv[2];
        int ret = desktop_remove_icon(label);
        if (ret == 0) {
            puts_color("[desktop] icon '", COLOR_LIGHT_CYAN);
            puts_color(label, COLOR_WHITE);
            puts_color("' removed.\n", COLOR_LIGHT_CYAN);
        } else {
            puts_color("[desktop] icon not found: ", COLOR_LIGHT_RED);
            puts((char *)label);
            putchar('\n');
        }
        return ret;
    }

    const char *arg = argv[2];
    char path[256];

    if (arg[0] == '.' || arg[0] == '/') {
        resolve_path(arg, path, sizeof(path));
    } else {
        sprintf(path, "/bin/%s", arg);
        if (!ends_with(path, ".rox")) {
            strcat(path, ".rox");
        }
    }

    char label[32];
    int plen = strlen(path);
    int li = plen - 1;
    while (li >= 0 && path[li] != '/') li--;
    li++;
    int lw = 0;
    while (path[li + lw] && path[li + lw] != '.' && lw < 31) {
        label[lw] = path[li + lw];
        lw++;
    }
    label[lw] = '\0';
    if (lw == 0) {
        for (lw = 0; arg[lw] && lw < 31; lw++) label[lw] = arg[lw];
        label[lw] = '\0';
    }

    desktop_add_icon_path(label, path);
    desktop_save_config();

    puts_color("[desktop] icon '", COLOR_LIGHT_CYAN);
    puts_color(label, COLOR_WHITE);
    puts_color("' added.\n", COLOR_LIGHT_CYAN);
    return 0;
#else
    puts("desktop command requires GUI mode\n");
    return -1;
#endif
}
