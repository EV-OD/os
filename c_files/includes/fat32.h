#ifndef FAT32_H
#define FAT32_H

/* =========================================================================
 * fat32.h – FAT12/FAT16/FAT32 Filesystem Driver
 *
 * Provides read/write access to FAT12, FAT16 and FAT32 partitions.
 * Supports 8.3 short filenames and VFAT long file names (LFN).
 *
 * Usage
 * -----
 *   fat32_context_t fs;
 *   fat32_init(&fs, partition_lba);          // parse boot sector
 *   fat32_list_dir(&fs, fs.root_cluster, 0); // list root directory
 *
 *   fat_dir_entry_t entry;
 *   if (fat32_find_path(&fs, "/kernel/init", &entry) == 0) {
 *       void *buf = kmalloc(entry.file_size);
 *       fat32_read_file(&fs, &entry, buf, entry.file_size);
 *   }
 *
 * References
 * ----------
 *   https://wiki.osdev.org/FAT
 *   Microsoft FAT32 File System Specification 1.03
 * ========================================================================= */

/* =========================================================================
 * FAT type identifiers
 * ========================================================================= */
#define FAT_TYPE_FAT12  12
#define FAT_TYPE_FAT16  16
#define FAT_TYPE_FAT32  32

/* =========================================================================
 * Directory entry attribute flags
 * ========================================================================= */
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        (FAT_ATTR_READ_ONLY | FAT_ATTR_HIDDEN | \
                             FAT_ATTR_SYSTEM    | FAT_ATTR_VOLUME_ID)

/* =========================================================================
 * FAT32 cluster chain sentinels
 * ========================================================================= */
#define FAT32_EOC_MIN   0x0FFFFFF8u   /* End-of-chain (minimum value) */
#define FAT32_BAD       0x0FFFFFF7u   /* Bad cluster                  */
#define FAT32_FREE      0x00000000u   /* Free cluster                 */

#define FAT16_EOC_MIN   0xFFF8u
#define FAT16_BAD       0xFFF7u
#define FAT12_EOC_MIN   0xFF8u
#define FAT12_BAD       0xFF7u

/* =========================================================================
 * On-disk structures (all packed, little-endian)
 * ========================================================================= */

/**
 * fat_extBS_32_t – Extended BIOS Parameter Block for FAT32.
 * Begins at byte offset 36 of the Volume Boot Record.
 */
typedef struct fat_extBS_32 {
    unsigned int    table_size_32;      /**< FAT size in sectors                  */
    unsigned short  extended_flags;     /**< Mirror / active FAT flags            */
    unsigned short  fat_version;        /**< High byte = major, low = minor       */
    unsigned int    root_cluster;       /**< First cluster of the root directory  */
    unsigned short  fat_info;           /**< Sector number of FSInfo structure    */
    unsigned short  backup_BS_sector;   /**< Sector of backup boot record         */
    unsigned char   reserved_0[12];     /**< Reserved (must be zero)              */
    unsigned char   drive_number;       /**< BIOS drive number (0x00/0x80)        */
    unsigned char   reserved_1;         /**< Reserved (Windows NT flags)          */
    unsigned char   boot_signature;     /**< 0x28 or 0x29                         */
    unsigned int    volume_id;          /**< Partition serial number              */
    unsigned char   volume_label[11];   /**< Volume label, padded with spaces     */
    unsigned char   fat_type_label[8];  /**< Always "FAT32   " (informational)    */
} __attribute__((packed)) fat_extBS_32_t;

/**
 * fat_extBS_16_t – Extended BIOS Parameter Block for FAT12/FAT16.
 * Begins at byte offset 36 of the Volume Boot Record.
 */
typedef struct fat_extBS_16 {
    unsigned char   bios_drive_num;     /**< BIOS drive number                    */
    unsigned char   reserved1;          /**< Reserved (Windows NT flags)          */
    unsigned char   boot_signature;     /**< 0x28 or 0x29                         */
    unsigned int    volume_id;          /**< Partition serial number              */
    unsigned char   volume_label[11];   /**< Volume label, padded with spaces     */
    unsigned char   fat_type_label[8];  /**< "FAT     ", "FAT12   ", "FAT16   "   */
} __attribute__((packed)) fat_extBS_16_t;

/**
 * fat_BS_t – Common BIOS Parameter Block (BPB) for all FAT variants.
 * The first 36 bytes are shared; bytes 36-89 differ by FAT type and are
 * cast to fat_extBS_32_t or fat_extBS_16_t as appropriate.
 */
typedef struct fat_BS {
    unsigned char   bootjmp[3];             /**< JMP SHORT … NOP boot jump         */
    unsigned char   oem_name[8];            /**< OEM identifier string             */
    unsigned short  bytes_per_sector;       /**< Bytes per sector (usually 512)    */
    unsigned char   sectors_per_cluster;    /**< Sectors per cluster (power of 2)  */
    unsigned short  reserved_sector_count;  /**< Reserved sectors (includes VBR)   */
    unsigned char   table_count;            /**< Number of FAT copies (usually 2)  */
    unsigned short  root_entry_count;       /**< Max root dir entries (0 = FAT32)  */
    unsigned short  total_sectors_16;       /**< Total sectors (0 if > 65535)      */
    unsigned char   media_type;             /**< Media descriptor byte             */
    unsigned short  table_size_16;          /**< FAT size in sectors (FAT12/16)    */
    unsigned short  sectors_per_track;      /**< Sectors per track (geometry)      */
    unsigned short  head_side_count;        /**< Number of heads (geometry)        */
    unsigned int    hidden_sector_count;    /**< Hidden sectors (LBA of partition) */
    unsigned int    total_sectors_32;       /**< Total sectors if > 65535          */
    /* Bytes 36-89: extended boot record – cast to the appropriate union member */
    unsigned char   extended_section[54];
} __attribute__((packed)) fat_BS_t;

/**
 * fat_fsinfo_t – FAT32 FSInfo structure (usually at sector 1).
 */
typedef struct fat_fsinfo {
    unsigned int    lead_sig;           /**< Must be 0x41615252                   */
    unsigned char   reserved1[480];     /**< Reserved                             */
    unsigned int    struc_sig;          /**< Must be 0x61417272                   */
    unsigned int    free_count;         /**< Last known free cluster count        */
    unsigned int    nxt_free;           /**< Hint for next free cluster search    */
    unsigned char   reserved2[12];      /**< Reserved                             */
    unsigned int    trail_sig;          /**< Must be 0xAA550000                   */
} __attribute__((packed)) fat_fsinfo_t;

/**
 * fat_dir_entry_t – Standard 8.3 directory entry (32 bytes).
 */
typedef struct fat_dir_entry {
    unsigned char   name[8];            /**< Filename (space-padded, no dot)      */
    unsigned char   ext[3];             /**< Extension (space-padded)             */
    unsigned char   attributes;         /**< FAT_ATTR_* flags                     */
    unsigned char   reserved;           /**< Reserved (Windows NT case flags)     */
    unsigned char   creation_time_cs;   /**< Creation time, centiseconds (0-199)  */
    unsigned short  creation_time;      /**< Creation time  H:M:S packed          */
    unsigned short  creation_date;      /**< Creation date  Y:M:D packed          */
    unsigned short  last_access_date;   /**< Last access date                     */
    unsigned short  first_cluster_high; /**< High 16 bits of first cluster        */
    unsigned short  last_mod_time;      /**< Last modification time               */
    unsigned short  last_mod_date;      /**< Last modification date               */
    unsigned short  first_cluster_low;  /**< Low 16 bits of first cluster         */
    unsigned int    file_size;          /**< File size in bytes (0 for dirs)      */
} __attribute__((packed)) fat_dir_entry_t;

/**
 * fat_lfn_entry_t – VFAT Long File Name directory entry (32 bytes).
 * LFN entries are placed immediately before the corresponding 8.3 entry,
 * in reverse order, and have the FAT_ATTR_LFN attribute.
 */
typedef struct fat_lfn_entry {
    unsigned char   order;              /**< Sequence number; 0x40 bit = last LFN */
    unsigned short  name1[5];           /**< UTF-16LE characters  1-5             */
    unsigned char   attribute;          /**< Always FAT_ATTR_LFN (0x0F)          */
    unsigned char   type;               /**< Always 0 for name entries            */
    unsigned char   checksum;           /**< 8.3 name checksum                    */
    unsigned short  name2[6];           /**< UTF-16LE characters  6-11            */
    unsigned short  zero;               /**< Always 0                             */
    unsigned short  name3[2];           /**< UTF-16LE characters 12-13            */
} __attribute__((packed)) fat_lfn_entry_t;

/**
 * fat_dir_loc_t – Information to locate a specific directory entry on disk.
 * Used for updating directory entries (like file size updates after writing).
 */
typedef struct {
    unsigned int cluster;
    unsigned int sector;
    unsigned int offset;
} fat_dir_loc_t;

/* =========================================================================
 * Derived filesystem context (computed at mount time)
 * ========================================================================= */

/** Maximum length of a file/directory name returned by this driver. */
#define FAT32_NAME_MAX  256

/**
 * fat32_context_t – All computed parameters needed to operate the filesystem.
 *
 * Populated by fat32_init(); callers should treat it as opaque and pass it
 * by pointer to every fat32_* function.
 */
typedef struct fat32_context {
    unsigned int    partition_lba;      /**< LBA of the Volume Boot Record        */
    unsigned int    bytes_per_sector;   /**< Bytes per logical sector             */
    unsigned int    sectors_per_cluster;/**< Sectors per allocation cluster       */
    unsigned int    bytes_per_cluster;  /**< Bytes per cluster                    */
    unsigned int    reserved_sectors;   /**< Reserved sectors count               */
    unsigned int    fat_count;          /**< Number of FAT copies                 */
    unsigned int    root_entry_count;   /**< 0 for FAT32                          */
    unsigned int    total_sectors;      /**< Total sectors in the volume          */
    unsigned int    fat_size;           /**< FAT size in sectors                  */
    unsigned int    root_dir_sectors;   /**< Sectors occupied by FAT12/16 root   */
    unsigned int    first_fat_sector;   /**< Absolute LBA of first FAT sector     */
    unsigned int    first_data_sector;  /**< Absolute LBA of first data sector    */
    unsigned int    total_clusters;     /**< Total data clusters                  */
    unsigned int    root_cluster;       /**< FAT32 root directory cluster #       */
    unsigned int    fat_info_sector;    /**< FAT32 FSInfo sector (relative)       */
    int             fat_type;           /**< FAT_TYPE_FAT{12,16,32}               */
    unsigned int    free_count;         /**< Hint from FSInfo (-1 if unknown)     */
    unsigned int    next_free;          /**< Hint from FSInfo (-1 if unknown)     */
} fat32_context_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * fat32_init – mount a FAT partition.
 *
 * Reads the VBR at partition_lba, parses the BPB and extended BPB, computes
 * all derived values, and stores them in ctx.  For FAT32, also reads the
 * FSInfo structure for free-cluster hints.
 *
 * @param ctx            Output context structure to fill.
 * @param partition_lba  LBA address of the first sector of the partition.
 * @return               0 on success, negative on error.
 */
int fat32_init(fat32_context_t *ctx, unsigned int partition_lba);

/**
 * fat32_cluster_to_lba – convert a cluster number to an absolute LBA.
 *
 * @param ctx      Filesystem context.
 * @param cluster  Cluster number (>= 2).
 * @return         Absolute LBA of the first sector of the cluster.
 */
unsigned int fat32_cluster_to_lba(const fat32_context_t *ctx,
                                   unsigned int cluster);

/**
 * fat32_next_cluster – follow the FAT chain.
 *
 * Looks up the FAT entry for the given cluster and returns the next one.
 *
 * @param ctx      Filesystem context.
 * @param cluster  Current cluster number.
 * @return         Next cluster number, or >= FAT32_EOC_MIN if end-of-chain,
 *                 or FAT32_FREE (0) on read error.
 */
unsigned int fat32_next_cluster(fat32_context_t *ctx, unsigned int cluster);

/**
 * fat32_read_cluster – read an entire cluster into buf.
 *
 * @param ctx      Filesystem context.
 * @param cluster  Cluster number to read.
 * @param buf      Destination buffer (must be >= bytes_per_cluster bytes).
 * @return         0 on success, -1 on error.
 */
int fat32_read_cluster(fat32_context_t *ctx, unsigned int cluster, void *buf);

/**
 * fat32_list_dir – enumerate all entries in a directory.
 *
 * Iterates every cluster in the directory's chain.  For each valid 8.3 or
 * LFN entry, calls the user-provided callback.  The callback receives:
 *   - The 8.3 directory entry.
 *   - The location of the entry.
 *   - The long file name (empty string if no LFN was attached).
 *
 * @param ctx          Filesystem context.
 * @param first_cluster First cluster of the directory (use 0 for FAT12/16 root).
 * @param callback     Function called for each valid entry.
 *                     Return 0 to continue, non-zero to stop early.
 * @param userdata     Opaque pointer forwarded to callback.
 * @return             0 when enumeration is complete, or the non-zero value
 *                     returned by the callback if it requested early exit.
 */
int fat32_list_dir(fat32_context_t *ctx,
                   unsigned int first_cluster,
                   int (*callback)(const fat_dir_entry_t *entry,
                                   const fat_dir_loc_t *loc,
                                   const char *lfn,
                                   void *userdata),
                   void *userdata);

/**
 * fat32_find_in_dir – look up a name inside one directory cluster chain.
 *
 * Comparison is case-insensitive.  Matches both 8.3 and LFN names.
 *
 * @param ctx           Filesystem context.
 * @param first_cluster First cluster of the directory (0 = FAT12/16 root).
 * @param name          Null-terminated name to look for.
 * @param out           Receives the matching directory entry on success.
 * @param out_loc       Optional. Receives the disk location of the valid 8.3 entry.
 * @return              0 if found, -1 if not found.
 */
int fat32_find_in_dir(fat32_context_t *ctx,
                      unsigned int first_cluster,
                      const char *name,
                      fat_dir_entry_t *out,
                      fat_dir_loc_t *out_loc);

/**
 * fat32_find_path – resolve an absolute path to a directory entry.
 *
 * Splits path on '/' and descends into each directory component.
 * The path must start with '/'.  Example: "/boot/grub/grub.cfg"
 *
 * @param ctx   Filesystem context.
 * @param path  Absolute path string.
 * @param out   Receives the final directory entry on success.
 * @param out_loc Optional. Receives the disk location of the entry.
 * @return      0 on success, -1 if any component is not found.
 */
int fat32_find_path(fat32_context_t *ctx,
                    const char *path,
                    fat_dir_entry_t *out,
                    fat_dir_loc_t *out_loc);

/**
 * fat32_read_file – read up to size bytes of a file into buf.
 *
 * Follows the cluster chain of the file described by entry, copies data
 * into buf until min(size, entry->file_size) bytes have been read.
 *
 * @param ctx    Filesystem context.
 * @param entry  Directory entry obtained from fat32_find_path/fat32_find_in_dir.
 * @param buf    Destination buffer (must be at least size bytes).
 * @param size   Maximum number of bytes to read.
 * @return       Number of bytes actually copied, or -1 on error.
 */
int fat32_read_file(fat32_context_t *ctx,
                    const fat_dir_entry_t *entry,
                    void *buf,
                    unsigned int size);

/* =========================================================================
 * Write API Additions
 * ========================================================================= */

/**
 * fat32_write_cluster – overwrite an entire cluster with buf.
 */
int fat32_write_cluster(fat32_context_t *ctx, unsigned int cluster, const void *buf);

/**
 * fat32_set_fat_entry – update the FAT table chain for a given cluster.
 */
int fat32_set_fat_entry(fat32_context_t *ctx, unsigned int cluster, unsigned int value);

/**
 * fat32_alloc_cluster – find a free cluster, mark it as EOC, and return it.
 */
unsigned int fat32_alloc_cluster(fat32_context_t *ctx);

/**
 * fat32_update_dir_entry – write a directory entry back to its original location.
 */
int fat32_update_dir_entry(fat32_context_t *ctx, const fat_dir_loc_t *loc, const fat_dir_entry_t *entry);

/**
 * fat32_write_file – write data to an existing file, extending clusters as needed.
 *
 * Follows the existing cluster chain and allocates new clusters when the file
 * grows beyond its current allocation.  Updates the directory entry file_size
 * on disk via the supplied location.
 *
 * @param ctx     Filesystem context.
 * @param entry   Directory entry (will be modified in-place with new size).
 * @param loc     Disk location of the directory entry (for writeback).
 * @param offset  Byte offset within the file to begin writing.
 * @param data    Source data buffer.
 * @param size    Number of bytes to write.
 * @return        Number of bytes written, or -1 on error.
 */
int fat32_write_file(fat32_context_t *ctx,
                     fat_dir_entry_t *entry,
                     const fat_dir_loc_t *loc,
                     unsigned int offset,
                     const void *data,
                     unsigned int size);

/**
 * fat32_create_file – create a new 8.3 file entry in a directory.
 *
 * Scans the directory for a free slot (0x00 or 0xE5 first byte), writes
 * an 8.3 entry with the given name, and optionally allocates an initial
 * cluster.  The name must already be in 8.3 format (up to 8+3 chars).
 *
 * @param ctx           Filesystem context.
 * @param dir_cluster   First cluster of the parent directory.
 * @param filename      Short name (e.g. "README"), up to 8 chars.
 * @param ext           Extension (e.g. "TXT"), up to 3 chars.  NULL for none.
 * @param attributes    FAT_ATTR_* flags for the new entry.
 * @param out_entry     Receives the created directory entry.
 * @param out_loc       Receives the disk location of the new entry.
 * @return              0 on success, -1 on error.
 */
int fat32_create_file(fat32_context_t *ctx,
                      unsigned int dir_cluster,
                      const char *filename,
                      const char *ext,
                      unsigned char attributes,
                      fat_dir_entry_t *out_entry,
                      fat_dir_loc_t *out_loc);

/**
 * fat32_mkdir – create a new subdirectory inside a parent directory.
 *
 * Allocates a cluster for the new directory, writes the '.' and '..' entries,
 * and inserts an 8.3 directory entry into the parent.
 *
 * @param ctx           Filesystem context.
 * @param parent_cluster First cluster of the parent directory.
 * @param name          Directory name (up to 8 chars, 8.3 short name).
 * @param out_entry     Receives the created directory entry.
 * @param out_loc       Receives the disk location of the new entry.
 * @return              0 on success, -1 on error.
 */
int fat32_mkdir(fat32_context_t *ctx,
                unsigned int parent_cluster,
                const char *name,
                fat_dir_entry_t *out_entry,
                fat_dir_loc_t *out_loc);

/**
 * fat32_delete_file – mark a file's directory entry as deleted and free its clusters.
 *
 * Sets the first byte of the 8.3 entry to 0xE5 and walks the cluster chain,
 * marking each cluster as FREE in the FAT.
 *
 * @param ctx   Filesystem context.
 * @param entry The directory entry to delete.
 * @param loc   Disk location of the directory entry.
 * @return      0 on success, -1 on error.
 */
int fat32_delete_file(fat32_context_t *ctx,
                      const fat_dir_entry_t *entry,
                      const fat_dir_loc_t *loc);

/**
 * fat32_dump_info – log the parsed filesystem parameters to the serial port.
 *
 * Useful during development to verify that the boot sector was read correctly.
 *
 * @param ctx  Filesystem context (must have been initialised).
 */
void fat32_dump_info(const fat32_context_t *ctx);

#endif /* FAT32_H */
