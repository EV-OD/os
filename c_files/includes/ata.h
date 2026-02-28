#ifndef ATA_H
#define ATA_H

/* =========================================================================
 * ata.h – ATA PIO (Programmed I/O) Driver
 *
 * Provides LBA28 sector reads from the primary ATA bus using polling
 * (no DMA, no IRQ).  Sufficient for reading FAT32 boot sectors,
 * allocation tables, and directory/file data during early boot.
 *
 * Primary ATA bus I/O ports
 * -------------------------
 *   0x1F0  Data register         (16-bit read/write for sector data)
 *   0x1F1  Error / Features
 *   0x1F2  Sector count
 *   0x1F3  LBA lo  (bits  0-7)
 *   0x1F4  LBA mid (bits  8-15)
 *   0x1F5  LBA hi  (bits 16-23)
 *   0x1F6  Drive / Head select   (0xE0 | drive<<4 | LBA bits 24-27)
 *   0x1F7  Status (read) / Command (write)
 *
 * Control register: 0x3F6 (Alternate Status / Device Control)
 *
 * Status register bits
 * --------------------
 *   Bit 7  BSY  – Controller busy
 *   Bit 6  DRDY – Drive ready
 *   Bit 3  DRQ  – Data request (sector buffer ready to transfer)
 *   Bit 0  ERR  – Error flag
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * I/O port addresses for the primary ATA bus
 * ------------------------------------------------------------------------- */
#define ATA_PRIMARY_DATA         0x1F0
#define ATA_PRIMARY_ERROR        0x1F1
#define ATA_PRIMARY_SECTOR_COUNT 0x1F2
#define ATA_PRIMARY_LBA_LO       0x1F3
#define ATA_PRIMARY_LBA_MID      0x1F4
#define ATA_PRIMARY_LBA_HI       0x1F5
#define ATA_PRIMARY_DRIVE_HEAD   0x1F6
#define ATA_PRIMARY_STATUS       0x1F7   /* read  */
#define ATA_PRIMARY_COMMAND      0x1F7   /* write */
#define ATA_PRIMARY_CONTROL      0x3F6

/* -------------------------------------------------------------------------
 * ATA commands
 * ------------------------------------------------------------------------- */
#define ATA_CMD_READ_SECTORS     0x20   /* Read Sectors (with retry) */
#define ATA_CMD_WRITE_SECTORS    0x30   /* Write Sectors (with retry) */
#define ATA_CMD_CACHE_FLUSH      0xE7   /* Flush Cache               */
#define ATA_CMD_IDENTIFY         0xEC   /* Identify Device           */

/* -------------------------------------------------------------------------
 * Status register bit masks
 * ------------------------------------------------------------------------- */
#define ATA_SR_BSY   0x80   /* Busy                  */
#define ATA_SR_DRDY  0x40   /* Drive Ready           */
#define ATA_SR_DRQ   0x08   /* Data Request Ready    */
#define ATA_SR_ERR   0x01   /* Error                 */

/* -------------------------------------------------------------------------
 * Drive select values for ATA_PRIMARY_DRIVE_HEAD
 * ------------------------------------------------------------------------- */
#define ATA_DRIVE_MASTER  0xE0   /* LBA28, master drive */
#define ATA_DRIVE_SLAVE   0xF0   /* LBA28, slave  drive */

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * ata_init – probe and initialise the primary ATA bus.
 *
 * Sends IDENTIFY to the master drive; logs the model string over serial.
 * Must be called before any ata_read_sectors() call.
 *
 * @return 0 on success, -1 if no drive detected.
 */
int ata_init(void);

/**
 * ata_get_total_sectors – return the LBA28 addressable sector count.
 *
 * The value comes from IDENTIFY words 60-61.  Only valid after a
 * successful ata_init() call.
 *
 * @return Total addressable sectors (0 if ata_init() has not been called).
 */
unsigned int ata_get_total_sectors(void);

/**
 * ata_read_sectors – read one or more 512-byte sectors into buf.
 *
 * Uses LBA28 PIO polling.  Each call to this function blocks until all
 * requested sectors have been transferred.
 *
 * @param lba    Logical block address of the first sector to read.
 * @param count  Number of sectors to read (1-255; subject to drive limit).
 * @param buf    Destination buffer; must be at least count * 512 bytes.
 * @return       0 on success, -1 on timeout or drive error.
 */
int ata_read_sectors(unsigned int lba, unsigned char count, void *buf);

/**
 * ata_write_sectors – write one or more 512-byte sectors from buf.
 *
 * Uses LBA28 PIO polling. Blocks until all sectors have been transferred
 * and issues a cache flush command at the end.
 *
 * @param lba    Logical block address of the first sector to write.
 * @param count  Number of sectors to write (1-255).
 * @param buf    Source buffer; must be at least count * 512 bytes.
 * @return       0 on success, -1 on timeout or drive error.
 */
int ata_write_sectors(unsigned int lba, unsigned char count, const void *buf);

#endif /* ATA_H */
