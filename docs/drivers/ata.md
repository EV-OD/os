# ATA PIO Driver

## Overview

Minimal ATA PIO (Programmed I/O) driver for the primary master drive on the
standard ISA I/O ports.  Supports identification, sector reading, and sector
writing using LBA28 addressing.

## Ports

| Port | Read | Write |
|------|------|-------|
| `0x1F0` | Data (16-bit) | Data (16-bit) |
| `0x1F1` | Error | Features |
| `0x1F2` | Sector Count | Sector Count |
| `0x1F3` | LBA Low | LBA Low |
| `0x1F4` | LBA Mid | LBA Mid |
| `0x1F5` | LBA High | LBA High |
| `0x1F6` | Drive/Head | Drive/Head |
| `0x1F7` | Status | Command |
| `0x3F6` | Alt Status | Device Control |

## API

```c
int ata_init(void);
```
Sends IDENTIFY DEVICE (0xEC) to the primary master.  Returns 0 on success, -1
if no drive is present.  Logs the drive model string via serial.

```c
int ata_read_sectors(unsigned int lba, unsigned int count, void *buf);
```
Reads `count` sectors (max 256) starting at `lba` into `buf` using PIO polling.

```c
int ata_write_sectors(unsigned int lba, unsigned int count, const void *buf);
```
Writes `count` sectors from `buf` starting at `lba`, followed by a cache flush
(command `0xE7`).

## Polling Strategy

All transfers use busy-wait polling:
1. Wait for BSY to clear.
2. Wait for DRQ to set.
3. Transfer 256 words (512 bytes) per sector via `inw`/`outw` on port `0x1F0`.

A 400 ns delay (reading the alternate status register 4 times) is inserted after
command issue to meet the ATA timing specification.

## Limitations

- LBA28 only (max 128 GiB).
- Primary master only (no slave, secondary channel).
- PIO mode (no DMA).
- No error recovery or retry logic.
