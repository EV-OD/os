/* =========================================================================
 * kmain.c – Kernel entry point (called from loader.s after paging is up)
 *
 * loader.s pushes two arguments before calling kmain:
 *   eax – Multiboot magic value  (0x2BADB002 if GRUB is the loader).
 *   ebx – Virtual address of the Multiboot information structure
 *         (loader.s adds KERNEL_VIRTUAL_BASE so C code can dereference it).
 *
 * Boot sequence
 * -------------
 *   kernel_init()    – hardware + memory subsystems (GDT…TSS…PFA…kheap…PIT)
 *   self-tests       – PFA + kheap unit tests
 *   shell_init()     – create /bin, /etc, /home, /tmp directories
 *   boot_animation() – text-based loading screen
 *   shell_run()      – enter interactive shell (nerd mode)
 *
 * See docs/kernel/boot_sequence.md for the full rationale.
 * ========================================================================= */

#include "kernel_init.h"
#include "multiboot.h"
#include "display.h"
#include "stdio.h"
#include "module.h"
#include "kheap.h"
#include "pfa.h"
#include "ktest.h"
#include "log.h"
#include "sched.h"
#include "process.h"
#include "tasks.h"
#include "interrupts.h"
#include "vfs.h"
#include "string.h"
#include "shell.h"
#include "boot_anim.h"
/* GUI mode (compiled in when GUI_MODE is defined) */
#ifdef GUI_MODE
#include "gui/fb.h"
#include "gui/gui_init.h"
#include "gui/gui_term.h"   /* gui_term_create – also used by cmd_mode */
#endif

/* Global multiboot pointer – stored early in kmain so that shell's
 * 'mode gui' command can call gui_init() any time after boot.       */
multiboot_info_t *g_multiboot_info = (multiboot_info_t *)0;

#ifdef GUI_MODE
#include "gui/desktop.h"
#include "terminal.h"
#include "gui/wm.h"
#include "gui/gui_term.h"
#include "terminal.h"
#include "keyboard.h"   /* keyboard_flush() – clear PS/2 init residue */

/*
 * Two kernel tasks for GUI mode:
 *   compositor  (nice=0)  – runs desktop_run() which handles mouse,
 *                            keyboard dispatch, and repaints every frame.
 *   shell       (nice=5)  – runs shell_run() which reads from the GUI
 *                            terminal's key buffer via term_active()->get_char().
 * The PIT at 10 ms preempts between them via the CFS scheduler.
 */
static void gui_compositor_task(void) { desktop_run(); }
static void gui_shell_task(void)
{
    /* Bind the currently active terminal to this process so that output
     * from this shell always goes to terminal-1 even after new terminals
     * are spawned and term_active() is redirected. */
    process_t *me = sched_current();
    if (me) me->bound_term = (void *)term_active();
    shell_run();
}
#endif

/* -------------------------------------------------------------------------
 * Helpers for filesystem test
 * ------------------------------------------------------------------------- */
static int fs_list_cb(const vfs_dirent_t *d, void *userdata)
{
    (void)userdata;
    if (d->attributes & 0x10) {  /* FAT_ATTR_DIRECTORY */
        log_info("[fs_test]   <DIR>  %s", d->name);
    } else {
        log_info("[fs_test]   %u bytes  %s", d->size, d->name);
    }
    return 0;
}

void kmain(unsigned int eax, unsigned int ebx)
{
    /* Save multiboot pointer globally so shell's 'mode gui' can use it. */
    g_multiboot_info = (multiboot_info_t *)ebx;

    /*
     * Forward the Multiboot magic and info pointer to kernel_init().
     * ebx is the virtual address forwarded by loader.s
     * (physical address + KERNEL_VIRTUAL_BASE).
     */
    kernel_init(eax, (multiboot_info_t *)ebx);

    /* ------------------------------------------------------------------
     * Heap smoke test – silent except on serial log.
     * ------------------------------------------------------------------ */
    {
        void *a = kmalloc(64);
        void *b = kmalloc(128);
        void *c = kmalloc(32);
        log_info("[kmain] heap smoke: a=0x%x b=0x%x c=0x%x",
                 (unsigned int)a, (unsigned int)b, (unsigned int)c);
        kfree(b); kfree(a); kfree(c);
        log_info("[kmain] heap smoke passed");
    }

    /* ------------------------------------------------------------------
     * Kernel self-tests – results go to serial log.
     * ------------------------------------------------------------------ */
    ktest_init();
    pfa_run_tests();
    kheap_run_tests();
    ktest_report();

    /* ------------------------------------------------------------------
     * Filesystem smoke test – silent on screen, logged via serial.
     * ------------------------------------------------------------------ */
    {
        fat32_context_t *fsctx = vfs_get_context();
        if (fsctx) {
            log_info("[fs_test] === Filesystem smoke test ===");
            vfs_readdir("/", fs_list_cb, (void *)0);

            int fd = vfs_open("/hello.txt", VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
            if (fd >= 0) {
                const char *msg = "Hello from RabinOS!\n";
                vfs_write(fd, msg, strlen(msg));
                vfs_close(fd);
            }

            int mkret = vfs_mkdir("/testdir");
            if (mkret == 0 || mkret == VFS_ERR_EXIST) {
                fd = vfs_open("/testdir/info.txt", VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
                if (fd >= 0) {
                    const char *data = "Nested file test\n";
                    vfs_write(fd, data, strlen(data));
                    vfs_close(fd);
                }
            }
            log_info("[fs_test] === Filesystem smoke test complete ===");
        }
    }

    /* ------------------------------------------------------------------
     * Create OS directory structure:  /bin  /etc  /home  /tmp  /var
     * ------------------------------------------------------------------ */
    shell_init();

    /* ------------------------------------------------------------------
     * Boot animation – text-based loading screen.
     * ------------------------------------------------------------------ */
    boot_animation();

    /* ------------------------------------------------------------------
     * GUI mode – try to initialise VESA framebuffer.
     * If GRUB provided an RGB linear framebuffer, launch the desktop.
     * Otherwise fall back to the classic text-mode nerd-mode shell.
     * ------------------------------------------------------------------ */
#ifdef GUI_MODE
    {
        multiboot_info_t *mb_ptr = (multiboot_info_t *)ebx;
        if (gui_launch(mb_ptr) == 0) {
            /* gui_launch() handles: framebuffer init, terminal creation,
             * desktop_init(), built-in icon registration, and
             * desktop_load_config() for persisted icons. */

            /*
             * Launch two preemptive kernel tasks under the CFS scheduler:
             *   compositor – desktop_run() handles mouse, keyboard dispatch,
             *                and repaints the screen every frame.
             *   shell      – shell_run() reads from the GUI terminal key
             *                buffer via term_active()->get_char().
             * sched_start() never returns; the PIT at 10 ms drives switching.
             */
            {
                process_t *comp  = process_create("compositor",
                                                  gui_compositor_task, 0);
                // process_t *shell = process_create("shell",
                //                                   gui_shell_task, 5);
                if (comp)  sched_add(comp);
                // if (shell) sched_add(shell);
            }
            log_info("[kmain] entering GUI mode (CFS scheduler)");
            /* Flush any PS/2 init residue from the keyboard ring buffer
             * so that it cannot trigger phantom commands in the shell. */
            keyboard_flush();
            sched_start();   /* NEVER RETURNS */
        } else {
            log_info("[kmain] GUI unavailable – entering nerd mode shell");
            shell_run();
        }
    }
#else
    log_info("[kmain] entering shell (nerd mode)");
    shell_run();
#endif

    /* Unreachable */
    while (1) {}
}
