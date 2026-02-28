/* =========================================================================
 * rox.c – Rabin OS eXecutable loader
 *
 * Loads .rox files from the VFS, validates the header, copies the code
 * into a buffer, and spawns a user-mode process to execute it.
 *
 * The loader creates a ring-3 process via process_create_user(), adds it
 * to the CFS scheduler, and waits for it to complete via process_wait().
 * ========================================================================= */

#include "rox.h"
#include "vfs.h"
#include "kheap.h"
#include "log.h"
#include "string.h"
#include "process.h"
#include "sched.h"

/* -------------------------------------------------------------------------
 * rox_validate_header
 * ------------------------------------------------------------------------- */
int rox_validate_header(const rox_header_t *hdr)
{
    if (!hdr)
        return 0;
    if (hdr->magic != ROX_MAGIC)
        return 0;
    if (hdr->version == 0 || hdr->version > ROX_VERSION)
        return 0;
    if (hdr->code_size == 0)
        return 0;
    return 1;
}

/* -------------------------------------------------------------------------
 * rox_load_and_run – load a .rox and execute it as a user-mode process.
 * ------------------------------------------------------------------------- */
int rox_load_and_run(const char *path, int argc, char **argv)
{
    (void)argc;
    (void)argv;

    log_info("[rox] loading %s", path);

    /* Open the file */
    int fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        log_error("[rox] cannot open %s", path);
        return -1;
    }

    /* Read the header */
    rox_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    int n = vfs_read(fd, &hdr, sizeof(hdr));
    if (n < (int)sizeof(hdr)) {
        log_error("[rox] short read on header (%d bytes)", n);
        vfs_close(fd);
        return -2;
    }

    /* Validate */
    if (!rox_validate_header(&hdr)) {
        log_error("[rox] invalid header: magic=0x%x ver=%d",
                  hdr.magic, (int)hdr.version);
        vfs_close(fd);
        return -2;
    }

    log_info("[rox] name=%s  code_size=%u  entry_offset=%u",
             hdr.name, hdr.code_size, hdr.entry_offset);

    /* Allocate buffer for the code section */
    unsigned char *code = (unsigned char *)kmalloc(hdr.code_size);
    if (!code) {
        log_error("[rox] kmalloc(%u) failed", hdr.code_size);
        vfs_close(fd);
        return -3;
    }

    /* Seek to code section (right after header) and read */
    vfs_seek(fd, ROX_HEADER_SIZE, VFS_SEEK_SET);
    int total_read = 0;
    while ((unsigned int)total_read < hdr.code_size) {
        int r = vfs_read(fd, code + total_read, hdr.code_size - (unsigned int)total_read);
        if (r <= 0) break;
        total_read += r;
    }
    vfs_close(fd);

    if ((unsigned int)total_read < hdr.code_size) {
        log_error("[rox] incomplete code: read %d of %u", total_read, hdr.code_size);
        kfree(code);
        return -2;
    }

    /* --- Spawn a user-mode process ------------------------------------ */

    /* hdr.name is stack-local; allocate a persistent copy for the proc. */
    char *proc_name = (char *)kmalloc(12);
    if (proc_name) {
        memcpy(proc_name, hdr.name, 12);
        proc_name[11] = '\0';
    }

    process_t *proc = process_create_user(
        proc_name ? proc_name : "rox",  /* process name      */
        code,                            /* code buffer       */
        hdr.code_size,                   /* code size         */
        hdr.entry_offset,                /* entry offset      */
        0                                /* nice = 0          */
    );

    /* Free the code buffer – process_create_user copies it into user pages */
    kfree(code);

    if (!proc) {
        log_error("[rox] failed to create user process for %s", path);
        if (proc_name) kfree(proc_name);
        return -4;
    }

    /* Add to scheduler and wait for completion */
    sched_add(proc);

    log_info("[rox] spawned '%s' as pid=%d, waiting...", proc->name, (int)proc->pid);

    int exit_status = process_wait(proc->pid);

    log_info("[rox] '%s' (pid=%d) exited with status %d",
             proc->name, (int)proc->pid, exit_status);

    /* Clean up the dead process (also frees proc_name via proc->name). */
    /* Note: process_destroy frees kstack and page_dir but not name. */
    process_destroy(proc);
    if (proc_name) kfree(proc_name);

    return exit_status;
}
