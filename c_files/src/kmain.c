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
     * Enter shell – nerd mode (text-only).
     * The shell uses getchar() which spin-waits on the keyboard IRQ.
     * Interrupts must remain enabled for keyboard + PIT to work.
     *
     * NOTE: We run the shell directly from kmain rather than through
     * the CFS scheduler.  The scheduler + task demos are still available
     * and can be launched via "tasks" command in the future.
     * ------------------------------------------------------------------ */
    log_info("[kmain] entering shell (nerd mode)");
    shell_run();

    /* Unreachable */
    while (1) {}
}
