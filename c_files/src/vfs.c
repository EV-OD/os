/* =========================================================================
 * vfs.c – Virtual Filesystem Layer (backed by FAT32)
 *
 * Implements POSIX-like file operations: open, read, write, close, seek,
 * stat, mkdir, unlink, readdir.
 *
 * Initialisation flow (fs_init):
 *   1. Probe ATA primary master with ata_init().
 *   2. Read LBA 0 (MBR).  If it has the 0xAA55 signature and the first
 *      partition entry has a FAT32 type byte (0x0B or 0x0C), use that
 *      partition's start LBA.
 *   3. Otherwise try mounting LBA 0 directly (super-floppy layout).
 *   4. Call fat32_init() and log the result.
 *
 * The file descriptor table is a simple fixed-size array.  No locking is
 * performed – this is a uniprocessor kernel with cooperative/preemptive
 * scheduling where FS calls are only made from kernel context.
 * ========================================================================= */

#include "vfs.h"
#include "ata.h"
#include "fat32.h"
#include "kheap.h"
#include "log.h"
#include "string.h"

/* =========================================================================
 * MBR partition entry (16 bytes, packed)
 * ========================================================================= */
typedef struct {
    unsigned char   boot_indicator;
    unsigned char   start_chs[3];
    unsigned char   partition_type;
    unsigned char   end_chs[3];
    unsigned int    start_lba;
    unsigned int    size_sectors;
} __attribute__((packed)) mbr_partition_entry_t;

/* =========================================================================
 * File descriptor entry
 * ========================================================================= */
typedef struct {
    int             in_use;             /**< 1 if slot is occupied              */
    int             flags;              /**< VFS_O_* flags from open()          */
    unsigned int    position;           /**< Current read/write byte offset     */
    fat_dir_entry_t entry;              /**< Copy of the FAT directory entry    */
    fat_dir_loc_t   loc;                /**< On-disk location (for writeback)   */
    int             dirty;              /**< 1 if entry metadata changed        */
    char            path[256];          /**< Path used to open (for debug)      */
} vfs_fd_t;

/* =========================================================================
 * Module-level state
 * ========================================================================= */
static fat32_context_t g_fs_ctx;
static int             g_fs_mounted = 0;
static vfs_fd_t        g_fd_table[VFS_MAX_OPEN_FILES];

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/**
 * Split an absolute path into parent-dir path and the final component name.
 * e.g. "/foo/bar/baz.txt" → parent="/foo/bar", name="baz.txt"
 *      "/hello.txt"       → parent="/",        name="hello.txt"
 */
static int split_path(const char *path,
                      char *parent_buf, unsigned int parent_max,
                      char *name_buf,   unsigned int name_max) {
    if (!path || path[0] != '/') return -1;

    unsigned int len = strlen(path);
    /* Find last '/' */
    int last_slash = -1;
    for (int i = (int)len - 1; i >= 0; i--) {
        if (path[i] == '/') { last_slash = i; break; }
    }

    if (last_slash < 0) return -1;

    /* Parent */
    if (last_slash == 0) {
        strncpy(parent_buf, "/", parent_max);
    } else {
        unsigned int plen = (unsigned int)last_slash;
        if (plen >= parent_max) plen = parent_max - 1;
        strncpy(parent_buf, path, plen);
        parent_buf[plen] = '\0';
    }

    /* Name */
    const char *namestart = path + last_slash + 1;
    strncpy(name_buf, namestart, name_max);
    name_buf[name_max - 1] = '\0';

    return 0;
}

/**
 * Split a filename like "README.TXT" into name part and extension part
 * for 8.3 formatting.
 */
static void split_filename(const char *filename,
                           char *name8, unsigned int n8max,
                           char *ext3,  unsigned int e3max) {
    /* Find the last dot */
    int dot_pos = -1;
    for (int i = 0; filename[i]; i++) {
        if (filename[i] == '.') dot_pos = i;
    }

    if (dot_pos >= 0) {
        unsigned int nlen = (unsigned int)dot_pos;
        if (nlen >= n8max) nlen = n8max - 1;
        strncpy(name8, filename, nlen);
        name8[nlen] = '\0';

        const char *e = filename + dot_pos + 1;
        strncpy(ext3, e, e3max);
        ext3[e3max - 1] = '\0';
    } else {
        strncpy(name8, filename, n8max);
        name8[n8max - 1] = '\0';
        ext3[0] = '\0';
    }
}

/**
 * Allocate a free fd slot.  Returns index or -1.
 */
static int alloc_fd(void) {
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!g_fd_table[i].in_use) return i;
    }
    return -1;
}

/**
 * Validate an fd.
 */
static int valid_fd(int fd) {
    return (fd >= 0 && fd < VFS_MAX_OPEN_FILES && g_fd_table[fd].in_use);
}

/* =========================================================================
 * fs_init – probe disk, find FAT partition, and mount
 * ========================================================================= */

int fs_init(void) {
    /* Clear the fd table */
    memset(g_fd_table, 0, sizeof(g_fd_table));
    g_fs_mounted = 0;

    /* Probe ATA drive */
    log_info("[vfs] Probing ATA primary master...");
    if (ata_init() < 0) {
        log_error("[vfs] No ATA drive found – filesystem unavailable");
        return -1;
    }

    /* Read LBA 0 (potential MBR) */
    unsigned char mbr[512];
    if (ata_read_sectors(0, 1, mbr) < 0) {
        log_error("[vfs] Failed to read LBA 0");
        return -1;
    }

    unsigned int partition_lba = 0;

    /* Check for MBR signature */
    if (mbr[510] == 0x55 && mbr[511] == 0xAA) {
        /* Parse partition table (4 entries starting at offset 446) */
        mbr_partition_entry_t *ptable = (mbr_partition_entry_t *)&mbr[446];

        for (int i = 0; i < 4; i++) {
            unsigned char ptype = ptable[i].partition_type;
            /* FAT32 types: 0x0B (FAT32 CHS), 0x0C (FAT32 LBA),
               also 0x01 (FAT12), 0x04/0x06/0x0E (FAT16) */
            if (ptype == 0x0B || ptype == 0x0C ||
                ptype == 0x01 || ptype == 0x04 ||
                ptype == 0x06 || ptype == 0x0E) {
                partition_lba = ptable[i].start_lba;
                log_info("[vfs] Found FAT partition %d at LBA %u (type=0x%x)",
                         i, partition_lba, (unsigned int)ptype);
                break;
            }
        }

        if (partition_lba == 0) {
            /* No FAT entry in partition table – try super-floppy */
            log_info("[vfs] No FAT partition in MBR, trying super-floppy layout (LBA 0)");
            partition_lba = 0;
        }
    } else {
        /* No MBR signature – super-floppy */
        log_info("[vfs] No MBR signature, trying super-floppy layout (LBA 0)");
        partition_lba = 0;
    }

    /* Mount the FAT partition */
    if (fat32_init(&g_fs_ctx, partition_lba) < 0) {
        log_error("[vfs] fat32_init failed at LBA %u", partition_lba);
        return -1;
    }

    g_fs_mounted = 1;
    fat32_dump_info(&g_fs_ctx);
    log_info("[vfs] Filesystem mounted successfully (FAT%d)", g_fs_ctx.fat_type);
    return 0;
}

/* =========================================================================
 * vfs_open
 * ========================================================================= */

int vfs_open(const char *path, int flags) {
    if (!g_fs_mounted) return -1;
    if (!path || path[0] != '/') return -1;

    int fd = alloc_fd();
    if (fd < 0) {
        log_error("[vfs] No free file descriptors");
        return -1;
    }

    vfs_fd_t *f = &g_fd_table[fd];
    fat_dir_entry_t entry;
    fat_dir_loc_t   loc;

    int found = fat32_find_path(&g_fs_ctx, path, &entry, &loc);

    if (found == 0) {
        /* File/dir exists */
        if ((flags & VFS_O_DIRECTORY) && !(entry.attributes & FAT_ATTR_DIRECTORY)) {
            return -1; /* wanted dir, got file */
        }

        /* VFS_O_TRUNC: zero out existing file */
        if ((flags & VFS_O_TRUNC) && !(entry.attributes & FAT_ATTR_DIRECTORY)) {
            /* Free all clusters */
            unsigned int cluster = ((unsigned int)entry.first_cluster_high << 16) |
                                    entry.first_cluster_low;
            while (cluster >= 2 && cluster < FAT32_EOC_MIN && cluster != FAT32_BAD) {
                unsigned int next = fat32_next_cluster(&g_fs_ctx, cluster);
                fat32_set_fat_entry(&g_fs_ctx, cluster, FAT32_FREE);
                cluster = next;
            }
            entry.file_size = 0;
            entry.first_cluster_low = 0;
            entry.first_cluster_high = 0;
            fat32_update_dir_entry(&g_fs_ctx, &loc, &entry);
        }

    } else if (flags & VFS_O_CREAT) {
        /* File does not exist – create it */
        char parent_path[256];
        char name_part[256];
        if (split_path(path, parent_path, sizeof(parent_path),
                        name_part, sizeof(name_part)) < 0) {
            return -1;
        }

        /* Resolve parent directory */
        fat_dir_entry_t parent_entry;
        if (fat32_find_path(&g_fs_ctx, parent_path, &parent_entry, (void*)0) < 0) {
            log_error("[vfs] Parent dir not found: %s", parent_path);
            return -1;
        }
        if (!(parent_entry.attributes & FAT_ATTR_DIRECTORY)) {
            return -1;
        }

        unsigned int parent_cluster = ((unsigned int)parent_entry.first_cluster_high << 16) |
                                       parent_entry.first_cluster_low;

        /* Split name into 8.3 components */
        char name8[9], ext3[4];
        split_filename(name_part, name8, sizeof(name8), ext3, sizeof(ext3));

        const char *ext_ptr = (ext3[0] != '\0') ? ext3 : (void*)0;

        if (fat32_create_file(&g_fs_ctx, parent_cluster,
                              name8, ext_ptr,
                              FAT_ATTR_ARCHIVE,
                              &entry, &loc) < 0) {
            log_error("[vfs] Failed to create file: %s", path);
            return -1;
        }
        log_info("[vfs] Created file: %s", path);

    } else {
        /* Not found and O_CREAT not specified */
        return -1;
    }

    /* Fill the fd entry */
    memset(f, 0, sizeof(vfs_fd_t));
    f->in_use = 1;
    f->flags = flags;
    f->position = (flags & VFS_O_APPEND) ? entry.file_size : 0;
    memcpy(&f->entry, &entry, sizeof(fat_dir_entry_t));
    memcpy(&f->loc, &loc, sizeof(fat_dir_loc_t));
    f->dirty = 0;
    strncpy(f->path, path, sizeof(f->path) - 1);
    f->path[sizeof(f->path) - 1] = '\0';

    return fd;
}

/* =========================================================================
 * vfs_close
 * ========================================================================= */

int vfs_close(int fd) {
    if (!valid_fd(fd)) return -1;

    vfs_fd_t *f = &g_fd_table[fd];

    /* Flush dirty metadata */
    if (f->dirty) {
        fat32_update_dir_entry(&g_fs_ctx, &f->loc, &f->entry);
        f->dirty = 0;
    }

    f->in_use = 0;
    return 0;
}

/* =========================================================================
 * vfs_read
 * ========================================================================= */

int vfs_read(int fd, void *buf, unsigned int count) {
    if (!valid_fd(fd)) return -1;
    if (!buf || count == 0) return 0;

    vfs_fd_t *f = &g_fd_table[fd];

    if (f->entry.attributes & FAT_ATTR_DIRECTORY) return -1; /* can't read dirs this way */
    if (!(f->flags & VFS_O_RDONLY) && !(f->flags & VFS_O_RDWR)) return -1; /* no read access */

    /* Clamp to remaining file data */
    if (f->position >= f->entry.file_size) return 0; /* EOF */
    unsigned int remaining = f->entry.file_size - f->position;
    if (count > remaining) count = remaining;

    /* Walk the cluster chain to "position" and read */
    unsigned int first_cluster = ((unsigned int)f->entry.first_cluster_high << 16) |
                                  f->entry.first_cluster_low;
    if (first_cluster < 2) return 0;

    unsigned int cluster_idx = f->position / g_fs_ctx.bytes_per_cluster;
    unsigned int off_in_cluster = f->position % g_fs_ctx.bytes_per_cluster;

    unsigned int cur_cluster = first_cluster;
    for (unsigned int i = 0; i < cluster_idx; i++) {
        unsigned int next = fat32_next_cluster(&g_fs_ctx, cur_cluster);
        if (next >= FAT32_EOC_MIN || next == 0 || next == FAT32_BAD) return 0;
        cur_cluster = next;
    }

    void *cluster_buf = kmalloc(g_fs_ctx.bytes_per_cluster);
    if (!cluster_buf) return -1;

    char *dest = (char *)buf;
    unsigned int total_read = 0;

    while (total_read < count) {
        if (fat32_read_cluster(&g_fs_ctx, cur_cluster, cluster_buf) < 0) {
            kfree(cluster_buf);
            return (total_read > 0) ? (int)total_read : -1;
        }

        unsigned int avail = g_fs_ctx.bytes_per_cluster - off_in_cluster;
        unsigned int to_copy = count - total_read;
        if (to_copy > avail) to_copy = avail;

        memcpy(dest + total_read, (char *)cluster_buf + off_in_cluster, to_copy);
        total_read += to_copy;
        off_in_cluster = 0;

        if (total_read < count) {
            unsigned int next = fat32_next_cluster(&g_fs_ctx, cur_cluster);
            if (next >= FAT32_EOC_MIN || next == 0 || next == FAT32_BAD) break;
            cur_cluster = next;
        }
    }

    kfree(cluster_buf);
    f->position += total_read;
    return (int)total_read;
}

/* =========================================================================
 * vfs_write
 * ========================================================================= */

int vfs_write(int fd, const void *buf, unsigned int count) {
    if (!valid_fd(fd)) return -1;
    if (!buf || count == 0) return 0;

    vfs_fd_t *f = &g_fd_table[fd];

    if (f->entry.attributes & FAT_ATTR_DIRECTORY) return -1;
    if (!(f->flags & VFS_O_WRONLY) && !(f->flags & VFS_O_RDWR)) return -1;

    unsigned int write_pos = (f->flags & VFS_O_APPEND) ? f->entry.file_size : f->position;

    int written = fat32_write_file(&g_fs_ctx, &f->entry, &f->loc,
                                   write_pos, buf, count);
    if (written < 0) return -1;

    f->position = write_pos + (unsigned int)written;
    f->dirty = 1; /* entry was already written back by fat32_write_file, but mark anyway */
    return written;
}

/* =========================================================================
 * vfs_seek
 * ========================================================================= */

int vfs_seek(int fd, int offset, int whence) {
    if (!valid_fd(fd)) return -1;

    vfs_fd_t *f = &g_fd_table[fd];
    int new_pos;

    switch (whence) {
        case VFS_SEEK_SET:
            new_pos = offset;
            break;
        case VFS_SEEK_CUR:
            new_pos = (int)f->position + offset;
            break;
        case VFS_SEEK_END:
            new_pos = (int)f->entry.file_size + offset;
            break;
        default:
            return -1;
    }

    if (new_pos < 0) new_pos = 0;
    f->position = (unsigned int)new_pos;
    return new_pos;
}

/* =========================================================================
 * vfs_stat
 * ========================================================================= */

int vfs_stat(const char *path, vfs_stat_t *st) {
    if (!g_fs_mounted || !path || !st) return -1;

    fat_dir_entry_t entry;
    if (fat32_find_path(&g_fs_ctx, path, &entry, (void*)0) < 0) return -1;

    st->size = entry.file_size;
    st->attributes = entry.attributes;
    st->first_cluster = ((unsigned int)entry.first_cluster_high << 16) |
                         entry.first_cluster_low;
    st->creation_date = entry.creation_date;
    st->creation_time = entry.creation_time;
    st->mod_date = entry.last_mod_date;
    st->mod_time = entry.last_mod_time;
    return 0;
}

/* =========================================================================
 * vfs_mkdir
 * ========================================================================= */

int vfs_mkdir(const char *path) {
    if (!g_fs_mounted || !path || path[0] != '/') return -1;

    char parent_path[256];
    char name_part[256];
    if (split_path(path, parent_path, sizeof(parent_path),
                    name_part, sizeof(name_part)) < 0) {
        return -1;
    }

    fat_dir_entry_t parent_entry;
    if (fat32_find_path(&g_fs_ctx, parent_path, &parent_entry, (void*)0) < 0) {
        log_error("[vfs] Parent not found: %s", parent_path);
        return -1;
    }
    if (!(parent_entry.attributes & FAT_ATTR_DIRECTORY)) return -1;

    unsigned int parent_cluster = ((unsigned int)parent_entry.first_cluster_high << 16) |
                                   parent_entry.first_cluster_low;

    return fat32_mkdir(&g_fs_ctx, parent_cluster, name_part,
                       (void*)0, (void*)0);
}

/* =========================================================================
 * vfs_unlink
 * ========================================================================= */

int vfs_unlink(const char *path) {
    if (!g_fs_mounted || !path || path[0] != '/') return -1;

    fat_dir_entry_t entry;
    fat_dir_loc_t   loc;
    if (fat32_find_path(&g_fs_ctx, path, &entry, &loc) < 0) return -1;

    /* Don't delete directories with unlink */
    if (entry.attributes & FAT_ATTR_DIRECTORY) return -1;

    return fat32_delete_file(&g_fs_ctx, &entry, &loc);
}

/* =========================================================================
 * vfs_readdir
 * ========================================================================= */

/* Internal callback context for bridging fat32_list_dir → vfs_readdir */
typedef struct {
    vfs_readdir_cb_t user_cb;
    void            *user_data;
} readdir_bridge_ctx_t;

static int readdir_bridge_cb(const fat_dir_entry_t *entry,
                             const fat_dir_loc_t *loc,
                             const char *lfn,
                             void *userdata) {
    (void)loc;
    readdir_bridge_ctx_t *bridge = (readdir_bridge_ctx_t *)userdata;

    vfs_dirent_t d;
    strncpy(d.name, lfn, sizeof(d.name) - 1);
    d.name[sizeof(d.name) - 1] = '\0';
    d.size = entry->file_size;
    d.attributes = entry->attributes;
    d.first_cluster = ((unsigned int)entry->first_cluster_high << 16) |
                       entry->first_cluster_low;

    return bridge->user_cb(&d, bridge->user_data);
}

int vfs_readdir(const char *path, vfs_readdir_cb_t callback, void *userdata) {
    if (!g_fs_mounted || !path || !callback) return -1;

    fat_dir_entry_t entry;
    if (fat32_find_path(&g_fs_ctx, path, &entry, (void*)0) < 0) return -1;
    if (!(entry.attributes & FAT_ATTR_DIRECTORY)) return -1;

    unsigned int dir_cluster = ((unsigned int)entry.first_cluster_high << 16) |
                                entry.first_cluster_low;

    readdir_bridge_ctx_t bridge;
    bridge.user_cb   = callback;
    bridge.user_data = userdata;

    return fat32_list_dir(&g_fs_ctx, dir_cluster, readdir_bridge_cb, &bridge);
}

/* =========================================================================
 * vfs_get_context
 * ========================================================================= */

fat32_context_t *vfs_get_context(void) {
    return g_fs_mounted ? &g_fs_ctx : (void*)0;
}
