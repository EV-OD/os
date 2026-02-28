# FAT32 Filesystem Driver

## Overview

Full read/write FAT32 driver for the kernel, supporting:

- FAT12, FAT16, and FAT32 type auto-detection
- VFAT long filename (LFN) support
- Cluster chain walking, allocation, and freeing
- Directory listing with callback interface
- Path resolution (absolute paths like `/foo/bar/baz.txt`)
- File read, write, create, and delete
- Directory creation (`mkdir`)

## Architecture

The driver operates on top of the ATA PIO block driver (`ata.c`) and is consumed by the VFS layer (`vfs.c`).

```
  VFS layer (vfs.c)
       │
       ▼
  FAT32 driver (fat32.c)
       │
       ▼
  ATA PIO driver (ata.c)
       │
       ▼
  Hardware (port I/O)
```

## Initialisation

```c
int fat32_init(fat32_context_t *ctx, unsigned int partition_lba);
```

Reads the BIOS Parameter Block (BPB) from the first sector of the partition,
determines FAT type (12/16/32) using the Microsoft-defined cluster count
thresholds, reads the FSInfo sector (FAT32 only), and populates the context
structure.

## Key Structures

### `fat32_context_t`

Holds all mounted-volume state:

| Field | Description |
|-------|-------------|
| `partition_lba` | Absolute LBA of partition start |
| `bytes_per_sector` | Typically 512 |
| `sectors_per_cluster` | 1–128 |
| `bytes_per_cluster` | Derived: BPS × SPC |
| `fat_size` | Sectors per FAT |
| `root_cluster` | First cluster of root directory (FAT32) |
| `fat_type` | 12, 16, or 32 |
| `free_count` / `next_free` | From FSInfo sector |

### `fat_dir_entry_t`

Standard 32-byte FAT directory entry (8.3 name, attributes, cluster, size, timestamps).

### `fat_dir_loc_t`

On-disk location of a directory entry, used for writeback:

| Field | Description |
|-------|-------------|
| `cluster` | Cluster number containing the entry |
| `sector` | Absolute LBA of the sector |
| `offset` | Byte offset within the sector |

## Read API

```c
int fat32_read_cluster(fat32_context_t *ctx, unsigned int cluster, void *buf);
unsigned int fat32_next_cluster(fat32_context_t *ctx, unsigned int cluster);
int fat32_list_dir(fat32_context_t *ctx, unsigned int cluster,
                   fat32_dir_callback_t callback, void *userdata);
int fat32_find_in_dir(fat32_context_t *ctx, unsigned int dir_cluster,
                      const char *name, fat_dir_entry_t *out, fat_dir_loc_t *out_loc);
int fat32_find_path(fat32_context_t *ctx, const char *path,
                    fat_dir_entry_t *out, fat_dir_loc_t *out_loc);
int fat32_read_file(fat32_context_t *ctx, fat_dir_entry_t *entry,
                    unsigned int offset, void *buf, unsigned int size);
```

## Write API

```c
int fat32_write_cluster(fat32_context_t *ctx, unsigned int cluster, const void *buf);
int fat32_set_fat_entry(fat32_context_t *ctx, unsigned int cluster, unsigned int value);
unsigned int fat32_alloc_cluster(fat32_context_t *ctx);
int fat32_update_dir_entry(fat32_context_t *ctx, const fat_dir_loc_t *loc,
                           const fat_dir_entry_t *entry);
int fat32_write_file(fat32_context_t *ctx, fat_dir_entry_t *entry,
                     const fat_dir_loc_t *loc, unsigned int offset,
                     const void *data, unsigned int size);
int fat32_create_file(fat32_context_t *ctx, unsigned int dir_cluster,
                      const char *filename, const char *ext,
                      unsigned char attributes,
                      fat_dir_entry_t *out_entry, fat_dir_loc_t *out_loc);
int fat32_mkdir(fat32_context_t *ctx, unsigned int parent_cluster,
                const char *dirname,
                fat_dir_entry_t *out_entry, fat_dir_loc_t *out_loc);
int fat32_delete_file(fat32_context_t *ctx, fat_dir_entry_t *entry,
                      const fat_dir_loc_t *loc);
```

## Cluster Allocation

`fat32_alloc_cluster()` performs a two-pass search:

1. **First pass**: Scan from `next_free` to `total_clusters + 2`.
2. **Second pass**: Wrap around from cluster 2 to `next_free`.

The allocated cluster is zeroed and marked as EOC (`0x0FFFFFFF`) in the FAT.
`free_count` and `next_free` in the context are updated accordingly.

## Limitations

- Only 8.3 short filenames are created (LFN read is supported).
- No FSInfo writeback (free count updated only in memory).
- Single-sector FAT entry updates (no cross-sector boundary handling for FAT12).
- No timestamp generation (entries created with zero timestamps).
