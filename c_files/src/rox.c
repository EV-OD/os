/* =========================================================================
 * rox.c – Rabin OS eXecutable loader
 *
 * Loads .rox files from the VFS, validates the header, copies the code
 * into a kernel buffer, and calls the entry point.
 *
 * Phase 1: Executables run as kernel-mode function calls (ring 0).
 *          The entry point signature is: void entry(int argc, char **argv)
 *
 * Phase 2 (future): Load into a separate address space and iret to ring 3.
 * ========================================================================= */

#include "rox.h"
#include "vfs.h"
#include "kheap.h"
#include "log.h"
#include "string.h"

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
 * rox_load_and_run
 * ------------------------------------------------------------------------- */
int rox_load_and_run(const char *path, int argc, char **argv)
{
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

    log_info("[rox] name=%.11s  code_size=%u  entry_offset=%u",
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

    /* Calculate entry point */
    void (*entry)(int, char **) =
        (void (*)(int, char **))(code + hdr.entry_offset);

    log_info("[rox] executing at 0x%x", (unsigned int)entry);

    /* Call the program */
    entry(argc, argv);

    /* Free the code buffer after return */
    kfree(code);

    log_info("[rox] %s exited", hdr.name);
    return 0;
}
