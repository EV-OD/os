#ifndef VFS_H
#define VFS_H

/* =========================================================================
 * vfs.h – Virtual Filesystem Layer
 *
 * Provides POSIX-like file system calls (open, read, write, close, seek,
 * stat, mkdir, unlink) backed by the FAT32 driver.
 *
 * File Descriptor Table
 * ---------------------
 *   The kernel maintains a fixed-size table of open file descriptors.
 *   Each descriptor tracks:
 *     - the FAT directory entry (name, size, cluster, attributes)
 *     - its on-disk location (for writeback)
 *     - the current seek position
 *     - open mode flags
 *
 * Initialisation
 * --------------
 *   fs_init() must be called during boot after ATA and kheap are ready.
 *   It probes the primary ATA drive, reads the MBR to find the first FAT
 *   partition, and mounts it.  If no partition table is found it tries
 *   treating the entire disk as a FAT volume (super-floppy layout).
 *
 * Usage
 * -----
 *   fs_init();
 *   int fd = vfs_open("/data/hello.txt", VFS_O_RDWR | VFS_O_CREAT);
 *   vfs_write(fd, "Hello!\n", 7);
 *   vfs_seek(fd, 0, VFS_SEEK_SET);
 *   char buf[64];
 *   int n = vfs_read(fd, buf, sizeof(buf));
 *   vfs_close(fd);
 *
 * References
 * ----------
 *   POSIX.1-2017 §2.5.1  File Descriptor
 *   docs/kernel/vfs.md   (overview diagram)
 * ========================================================================= */

#include "fat32.h"

/* =========================================================================
 * Open-mode flags (combinable with |)
 * ========================================================================= */
#define VFS_O_RDONLY    0x0001   /**< Open for reading only                */
#define VFS_O_WRONLY    0x0002   /**< Open for writing only                */
#define VFS_O_RDWR      0x0003   /**< Open for reading and writing         */
#define VFS_O_CREAT     0x0100   /**< Create file if it does not exist     */
#define VFS_O_TRUNC     0x0200   /**< Truncate existing file to zero size  */
#define VFS_O_APPEND    0x0400   /**< Writes always go to end of file      */
#define VFS_O_EXCL      0x0800   /**< With O_CREAT: fail if file exists    */
#define VFS_O_DIRECTORY 0x1000   /**< Fail if path is not a directory      */

/* =========================================================================
 * Error codes (returned as negative values)
 * ========================================================================= */
#define VFS_ERR_GENERIC     (-1)  /**< Unspecified error                    */
#define VFS_ERR_NOENT       (-2)  /**< File or directory not found           */
#define VFS_ERR_EXIST       (-3)  /**< File or directory already exists      */
#define VFS_ERR_NOTDIR      (-4)  /**< Expected directory, got file          */
#define VFS_ERR_ISDIR       (-5)  /**< Cannot perform file op on directory   */
#define VFS_ERR_NOSPC       (-6)  /**< No space left on device              */
#define VFS_ERR_NOFDS       (-7)  /**< File descriptor table full           */
#define VFS_ERR_BADF        (-8)  /**< Bad file descriptor                  */
#define VFS_ERR_IO          (-9)  /**< I/O error                            */
#define VFS_ERR_NOACCESS   (-10)  /**< Permission / mode mismatch           */

/* =========================================================================
 * Seek origin constants
 * ========================================================================= */
#define VFS_SEEK_SET  0   /**< Seek from beginning of file    */
#define VFS_SEEK_CUR  1   /**< Seek from current position     */
#define VFS_SEEK_END  2   /**< Seek from end of file          */

/* =========================================================================
 * File-descriptor table configuration
 * ========================================================================= */
#define VFS_MAX_OPEN_FILES  16  /**< Maximum simultaneously open files    */

/* =========================================================================
 * File stat structure
 * ========================================================================= */
typedef struct vfs_stat {
    unsigned int  size;         /**< File size in bytes                   */
    unsigned char attributes;   /**< FAT attribute flags                  */
    unsigned int  first_cluster;/**< First cluster number                 */
    unsigned short creation_date;
    unsigned short creation_time;
    unsigned short mod_date;
    unsigned short mod_time;
} vfs_stat_t;

/* =========================================================================
 * Directory listing entry (returned by vfs_readdir)
 * ========================================================================= */
typedef struct vfs_dirent {
    char           name[256];   /**< File/directory name                  */
    unsigned int   size;        /**< File size (0 for directories)        */
    unsigned char  attributes;  /**< FAT attributes                       */
    unsigned int   first_cluster;
} vfs_dirent_t;

/* =========================================================================
 * Readdir callback prototype
 * ========================================================================= */

/**
 * Callback for vfs_readdir(). Called once per directory entry.
 *
 * @param dirent  The directory entry info.
 * @param userdata Opaque pointer passed through from vfs_readdir.
 * @return 0 to continue, non-zero to stop iteration early.
 */
typedef int (*vfs_readdir_cb_t)(const vfs_dirent_t *dirent, void *userdata);

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * fs_init – Initialise the filesystem subsystem.
 *
 * Probes ATA, reads the MBR or super-floppy boot sector, and mounts the
 * first FAT partition.  Logs progress and errors to serial.
 *
 * @return 0 on success, -1 on error (no disk / no FAT partition found).
 */
int fs_init(void);

/**
 * vfs_open – Open a file or directory by absolute path.
 *
 * @param path   Absolute path (must begin with '/').
 * @param flags  Combination of VFS_O_* flags.
 * @return       Non-negative file descriptor on success, -1 on error.
 */
int vfs_open(const char *path, int flags);

/**
 * vfs_close – Close an open file descriptor.
 *
 * Flushes dirty directory-entry metadata (file size) and frees the slot.
 *
 * @param fd  File descriptor returned by vfs_open().
 * @return    0 on success, -1 on bad fd.
 */
int vfs_close(int fd);

/**
 * vfs_read – Read bytes from an open file.
 *
 * Reads up to count bytes starting at the current seek position.
 * Advances the position by the number of bytes actually read.
 *
 * @param fd     Open file descriptor.
 * @param buf    Destination buffer.
 * @param count  Maximum bytes to read.
 * @return       Number of bytes read (0 at EOF), or -1 on error.
 */
int vfs_read(int fd, void *buf, unsigned int count);

/**
 * vfs_write – Write bytes to an open file.
 *
 * Writes count bytes starting at the current seek position (or at the
 * end of file if VFS_O_APPEND was specified).  Extends the file and
 * allocates new clusters as needed.  Advances the position.
 *
 * @param fd     Open file descriptor (must be opened with write access).
 * @param buf    Source data buffer.
 * @param count  Number of bytes to write.
 * @return       Number of bytes written, or -1 on error.
 */
int vfs_write(int fd, const void *buf, unsigned int count);

/**
 * vfs_seek – Reposition the read/write offset of a file descriptor.
 *
 * @param fd      Open file descriptor.
 * @param offset  Signed byte offset.
 * @param whence  VFS_SEEK_SET, VFS_SEEK_CUR, or VFS_SEEK_END.
 * @return        New absolute position, or -1 on error.
 */
int vfs_seek(int fd, int offset, int whence);

/**
 * vfs_stat – Get file metadata without opening it.
 *
 * @param path  Absolute path.
 * @param st    Output stat structure.
 * @return      0 on success, -1 if path does not exist.
 */
int vfs_stat(const char *path, vfs_stat_t *st);

/**
 * vfs_mkdir – Create a new directory.
 *
 * Parent directories must already exist.
 *
 * @param path  Absolute path of the directory to create.
 * @return      0 on success, -1 on error.
 */
int vfs_mkdir(const char *path);

/**
 * vfs_unlink – Delete a file.
 *
 * Does not delete directories (use vfs_rmdir for that).
 *
 * @param path  Absolute path of the file to delete.
 * @return      0 on success, -1 on error.
 */
int vfs_unlink(const char *path);

/**
 * vfs_readdir – Enumerate all entries in a directory.
 *
 * @param path      Absolute path of the directory.
 * @param callback  Called for each entry.
 * @param userdata  Opaque pointer forwarded to callback.
 * @return          0 on success, -1 on error.
 */
int vfs_readdir(const char *path, vfs_readdir_cb_t callback, void *userdata);

/**
 * vfs_get_context – Get a pointer to the mounted filesystem context.
 *
 * @return Pointer to the active fat32_context_t, or NULL if not mounted.
 */
fat32_context_t *vfs_get_context(void);

#endif /* VFS_H */
