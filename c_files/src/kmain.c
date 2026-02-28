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
 *   kernel_init()  – hardware + memory subsystems (GDT…TSS…PFA…kheap…PIT)
 *   display setup  – clear framebuffer, show boot banner
 *   heap smoke test – quick kmalloc/kfree round-trip to validate the heap
 *   self-tests     – PFA + kheap unit tests
 *   sched_init()   – CFS scheduler + process subsystem init
 *   process_create – create 4 kernel tasks with different nice values
 *   sched_start()  – start preemptive scheduling (never returns)
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

    fb_clear();
    cursor_move_home();
    display_boot_info();

    /* ------------------------------------------------------------------
     * Heap smoke test – allocate and free a few blocks to confirm the
     * allocator and magic-number guard are working correctly.
     * ------------------------------------------------------------------ */
    {
        void *a = kmalloc(64);
        void *b = kmalloc(128);
        void *c = kmalloc(32);
        log_info("[kmain] heap smoke test: a=0x%x b=0x%x c=0x%x",
                 (unsigned int)a, (unsigned int)b, (unsigned int)c);
        kfree(b);               /* free middle → coalesce test            */
        kfree(a);               /* free left   → should coalesce with gap */
        kfree(c);               /* free right  → heap should be full again*/
        log_info("[kmain] heap smoke test passed");
    }

    /* ------------------------------------------------------------------
     * Kernel self-tests – run after all subsystems are initialised.
     * Results are visible on the serial monitor under the [ktest] tag.
     * ------------------------------------------------------------------ */
    ktest_init();
    pfa_run_tests();
    kheap_run_tests();
    ktest_report();

    /* ------------------------------------------------------------------
     * Filesystem smoke test – verify the VFS layer can create, write,
     * read back, and list files on the hard-disk FAT32 partition.
     * Run after kernel_init() has called fs_init().
     * ------------------------------------------------------------------ */
    {
        fat32_context_t *fsctx = vfs_get_context();
        if (fsctx) {
            log_info("[fs_test] === Filesystem smoke test ===");

            /* 1. List root directory (will be empty on first boot) */
            log_info("[fs_test] Root directory before test:");
            vfs_readdir("/", fs_list_cb, (void *)0);

            /* 2. Create & write a test file */
            int fd = vfs_open("/hello.txt", VFS_O_RDWR | VFS_O_CREAT);
            if (fd >= 0) {
                const char *msg = "Hello from MYOS kernel!\n";
                int written = vfs_write(fd, msg, strlen(msg));
                log_info("[fs_test] wrote %d bytes to /hello.txt", written);
                vfs_close(fd);
            } else {
                log_error("[fs_test] failed to create /hello.txt");
            }

            /* 3. Read the file back */
            fd = vfs_open("/hello.txt", VFS_O_RDONLY);
            if (fd >= 0) {
                char buf[64];
                memset(buf, 0, sizeof(buf));
                int nread = vfs_read(fd, buf, sizeof(buf) - 1);
                log_info("[fs_test] read back %d bytes: \"%s\"", nread, buf);
                vfs_close(fd);
            }

            /* 4. Create a subdirectory */
            if (vfs_mkdir("/testdir") == 0) {
                log_info("[fs_test] created /testdir");
            } else {
                log_info("[fs_test] /testdir already exists or mkdir failed");
            }

            /* 5. Create a file inside the subdirectory */
            fd = vfs_open("/testdir/info.txt", VFS_O_RDWR | VFS_O_CREAT);
            if (fd >= 0) {
                const char *data = "Nested file test\n";
                vfs_write(fd, data, strlen(data));
                vfs_close(fd);
                log_info("[fs_test] created /testdir/info.txt");
            }

            /* 6. List root directory after test */
            log_info("[fs_test] Root directory after test:");
            vfs_readdir("/", fs_list_cb, (void *)0);

            /* 7. List subdirectory */
            log_info("[fs_test] /testdir contents:");
            vfs_readdir("/testdir", fs_list_cb, (void *)0);

            /* 8. Stat the file */
            vfs_stat_t st;
            if (vfs_stat("/hello.txt", &st) == 0) {
                log_info("[fs_test] stat /hello.txt: size=%u cluster=%u",
                         st.size, st.first_cluster);
            }

            log_info("[fs_test] === Filesystem smoke test complete ===");
        } else {
            log_warning("[fs_test] No filesystem mounted – skipping test");
        }
    }

    /* ------------------------------------------------------------------
     * CFS Scheduler – initialise scheduler and process subsystems, then
     * create the test tasks and launch preemptive multitasking.
     *
     * Task layout (VGA rows 9-12 show live counters):
     *   task_a  nice=-5  weight=3121  → ~3× baseline CPU (fastest counter)
     *   task_b  nice= 0  weight=1024  → baseline
     *   task_c  nice=+5  weight= 335  → ~0.33× baseline CPU
     *   task_idle nice=+19 weight=15  → near-idle background task
     *
     * The vruntime delta per tick:
     *   delta = PIT_TICK_MS * NICE0_WEIGHT / weight
     *   task_a: 10 * 1024 / 3121 ≈  3 ms-units/tick  (slow increase → more CPU)
     *   task_b: 10 * 1024 / 1024 = 10 ms-units/tick  (baseline)
     *   task_c: 10 * 1024 /  335 ≈ 31 ms-units/tick  (fast increase → less CPU)
     * ------------------------------------------------------------------ */
    log_info("[kmain] initialising CFS scheduler");
    process_init();
    sched_init();

    {
        process_t *pa = process_create("task_a",    task_a,    -5);
        process_t *pb = process_create("task_b",    task_b,     0);
        process_t *pc = process_create("task_c",    task_c,     5);
        process_t *pi = process_create("task_idle", task_idle, 19);

        if (!pa || !pb || !pc || !pi) {
            log_error("[kmain] process creation failed – hanging");
            while (1) {}
        }

        sched_add(pa);
        sched_add(pb);
        sched_add(pc);
        sched_add(pi);
    }

    log_info("[kmain] launching scheduler – entering multitasking");

    /* Disable interrupts before sched_start(); iret re-enables them via
     * EFLAGS.IF=1 in the restored process frame. */
    interrupts_disable();

    /* NEVER RETURNS – enters the first process via iret */
    sched_start();

    /* Unreachable – but keep as a safety net */
    while (1) {}
}
