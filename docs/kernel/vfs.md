# Virtual Filesystem (VFS) Layer

## Overview

The VFS layer provides a **POSIX-like file API** for the kernel, backed by the
FAT32 filesystem driver.  It is the single point of entry for all file I/O in the
kernel – higher-level code never calls the FAT32 or ATA drivers directly.

```
 ┌──────────────┐
 │  Kernel code  │   vfs_open / vfs_read / vfs_write / …
 └──────┬───────┘
        │
 ┌──────▼───────┐
 │    VFS layer  │   c_files/src/vfs.c
 │  (fd table)   │
 └──────┬───────┘
        │
 ┌──────▼───────┐
 │ FAT32 driver  │   c_files/src/fat32.c
 └──────┬───────┘
        │
 ┌──────▼───────┐
 │  ATA PIO      │   c_files/src/ata.c
 └───────────────┘
```

## Boot Initialisation

`fs_init()` is called from `kernel_init()` after the heap is ready:

1. **ATA probe** – `ata_init()` sends IDENTIFY to the primary master drive.
2. **MBR parse** – LBA 0 is read; if it contains a valid MBR (`0xAA55`), the
   partition table is scanned for the first FAT32 entry (type `0x0B` / `0x0C`)
   or FAT12/16 entry (`0x01` / `0x04` / `0x06` / `0x0E`).
3. **Super-floppy fallback** – If no partition table is found, LBA 0 is treated
   as the start of a FAT volume directly.
4. **FAT mount** – `fat32_init()` reads the BPB, EBPB and FSInfo sector, then
   populates the `fat32_context_t` structure.

If any step fails, `fs_init()` returns `-1` and the kernel continues without a
mounted filesystem.

## File Descriptor Table

A fixed-size table (`VFS_MAX_OPEN_FILES = 16`) tracks open files.  Each slot
stores:

| Field      | Description                                  |
|------------|----------------------------------------------|
| `in_use`   | Whether the slot is occupied                 |
| `flags`    | `VFS_O_*` flags from the open call           |
| `position` | Current byte offset for read/write           |
| `entry`    | Copy of the FAT32 directory entry            |
| `loc`      | On-disk sector/offset of the dir entry       |
| `dirty`    | Metadata needs flush on close                |

## API Reference

### Initialisation

```c
int fs_init(void);            // Probe ATA, find FAT partition, mount
fat32_context_t *vfs_get_context(void);  // Raw access (debugging)
```

### File Operations

```c
int vfs_open(const char *path, int flags);   // Returns fd ≥ 0, or -1
int vfs_close(int fd);
int vfs_read(int fd, void *buf, unsigned int count);  // Returns bytes read
int vfs_write(int fd, const void *buf, unsigned int count);
int vfs_seek(int fd, int offset, int whence);  // Returns new position
```

### Metadata

```c
int vfs_stat(const char *path, vfs_stat_t *st);
int vfs_readdir(const char *path, vfs_readdir_cb_t cb, void *userdata);
```

### Directory / File Management

```c
int vfs_mkdir(const char *path);
int vfs_unlink(const char *path);   // Files only (not directories)
```

### Open Flags

| Flag            | Value    | Description                    |
|-----------------|----------|--------------------------------|
| `VFS_O_RDONLY`  | `0x0001` | Read-only                      |
| `VFS_O_WRONLY`  | `0x0002` | Write-only                     |
| `VFS_O_RDWR`   | `0x0003` | Read and write                 |
| `VFS_O_CREAT`  | `0x0100` | Create if not exists           |
| `VFS_O_TRUNC`  | `0x0200` | Truncate existing file to 0    |
| `VFS_O_APPEND` | `0x0400` | All writes go to end of file   |
| `VFS_O_DIRECTORY` | `0x1000` | Fail if not a directory      |

### Seek Whence

| Constant       | Value | Base                 |
|----------------|-------|----------------------|
| `VFS_SEEK_SET` | 0     | Beginning of file    |
| `VFS_SEEK_CUR` | 1     | Current position     |
| `VFS_SEEK_END` | 2     | End of file          |

## Usage Example

```c
#include "vfs.h"

void demo(void) {
    // Create and write
    int fd = vfs_open("/hello.txt", VFS_O_RDWR | VFS_O_CREAT);
    vfs_write(fd, "Hello, world!\n", 14);

    // Seek back and read
    vfs_seek(fd, 0, VFS_SEEK_SET);
    char buf[64];
    int n = vfs_read(fd, buf, sizeof(buf));
    vfs_close(fd);

    // List root directory
    vfs_readdir("/", my_callback, NULL);

    // Create a subdirectory
    vfs_mkdir("/mydir");

    // Delete a file
    vfs_unlink("/hello.txt");
}
```

## Source Files

| File | Purpose |
|------|---------|
| `c_files/includes/vfs.h` | Public header (types, flags, API) |
| `c_files/src/vfs.c` | Implementation (fd table, init, all ops) |
| `c_files/includes/fat32.h` | FAT32 driver header |
| `c_files/src/fat32.c` | FAT32 driver implementation |
| `c_files/includes/ata.h` | ATA PIO driver header |
| `c_files/src/ata.c` | ATA PIO driver implementation |
