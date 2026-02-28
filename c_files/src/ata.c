/* =========================================================================
 * ata.c – ATA PIO (Programmed I/O) Driver
 *
 * Implements LBA28 sector reads from the primary ATA bus using polled I/O.
 * No DMA, no interrupts.  Suitable for read-only FAT32 access during boot.
 *
 * Reference: https://wiki.osdev.org/ATA_PIO_Mode
 * ========================================================================= */

#include "ata.h"
#include "stdio.h"
#include "string.h"
#include "log.h"
#include "isr.h"
#include "pic.h"

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

/**
 * ata_400ns_delay – perform four reads of the alternate-status register.
 *
 * Each inb takes ≥ 100 ns on a real bus, giving ≥ 400 ns total delay.
 * Required after some ATA commands before reading the status register.
 */
static void ata_400ns_delay(void)
{
    inb(ATA_PRIMARY_CONTROL);
    inb(ATA_PRIMARY_CONTROL);
    inb(ATA_PRIMARY_CONTROL);
    inb(ATA_PRIMARY_CONTROL);
}

/**
 * ata_wait_bsy – spin until the BSY bit in the status register clears.
 *
 * @return 0 once BSY is clear, -1 if a basic error condition is detected.
 */
static int ata_wait_bsy(void)
{
    unsigned int timeout = 0x100000;
    while (--timeout) {
        unsigned char status = inb(ATA_PRIMARY_STATUS);
        if (!(status & ATA_SR_BSY)) {
            return 0;
        }
    }
    log_error("[ata] timeout waiting for BSY to clear");
    return -1;
}

/**
 * ata_wait_drq – spin until either DRQ is set or an error is flagged.
 *
 * @return 0 when DRQ is set, -1 on error or timeout.
 */
static int ata_wait_drq(void)
{
    unsigned int timeout = 0x100000;
    while (--timeout) {
        unsigned char status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_SR_ERR) {
            log_error("[ata] drive signalled an error (status=0x%x)", (unsigned int)status);
            return -1;
        }
        if (status & ATA_SR_DRQ) {
            return 0;
        }
    }
    log_error("[ata] timeout waiting for DRQ");
    return -1;
}

/* =========================================================================
 * Module state
 * ========================================================================= */
static unsigned int g_ata_total_sectors = 0;

/**
 * IRQ 14 handler – acknowledge and discard.  PIO mode doesn't use interrupts
 * but the drive still asserts IRQ14 after each sector transfer.
 */
static void ata_irq_handler(struct cpu_state *cpu,
                            struct stack_state *stack,
                            unsigned int interrupt)
{
    (void)cpu; (void)stack; (void)interrupt;
    /* Reading the status register clears the IRQ condition on the drive. */
    inb(ATA_PRIMARY_STATUS);
}

/* =========================================================================
 * Public API
 * ========================================================================= */

unsigned int ata_get_total_sectors(void)
{
    return g_ata_total_sectors;
}

int ata_init(void)
{
    /* Select master drive and send IDENTIFY */
    outb(ATA_PRIMARY_DRIVE_HEAD, 0xA0);   /* select master, CHS mode for IDENTIFY */
    outb(ATA_PRIMARY_SECTOR_COUNT, 0);
    outb(ATA_PRIMARY_LBA_LO,  0);
    outb(ATA_PRIMARY_LBA_MID, 0);
    outb(ATA_PRIMARY_LBA_HI,  0);
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENTIFY);

    unsigned char status = inb(ATA_PRIMARY_STATUS);
    if (status == 0) {
        log_warning("[ata] no drive on primary master");
        return -1;
    }

    if (ata_wait_bsy() < 0) {
        return -1;
    }

    /* If LBA mid/hi are non-zero this is not a standard ATA drive (e.g. ATAPI) */
    if (inb(ATA_PRIMARY_LBA_MID) != 0 || inb(ATA_PRIMARY_LBA_HI) != 0) {
        log_warning("[ata] primary master is not an ATA drive (ATAPI?)");
        return -1;
    }

    if (ata_wait_drq() < 0) {
        return -1;
    }

    /* Read the 256 identify words; extract the model string (words 27-46) */
    unsigned short identify[256];
    unsigned int i;
    for (i = 0; i < 256; i++) {
        identify[i] = inw(ATA_PRIMARY_DATA);
    }

    /* Model string: bytes at words 27-46, each word is two ASCII characters,
     * stored with bytes swapped (big-endian within the word). */
    char model[41];
    unsigned int m;
    for (m = 0; m < 20; m++) {
        model[m * 2]     = (char)(identify[27 + m] >> 8);
        model[m * 2 + 1] = (char)(identify[27 + m] & 0xFF);
    }
    model[40] = '\0';

    /* Words 60-61: total addressable LBA28 sectors (32-bit value) */
    g_ata_total_sectors = ((unsigned int)identify[61] << 16) | identify[60];

    /* Trim trailing spaces from the model string */
    {
        int end = 39;
        while (end >= 0 && (model[end] == ' ' || model[end] == '\0')) end--;
        model[end + 1] = '\0';
    }

    log_info("[ata] primary master identified: %s", model);
    log_info("[ata] total LBA28 sectors: %u (%u MiB)",
             g_ata_total_sectors, g_ata_total_sectors / 2048);

    /* Register a no-op handler for IRQ 14 (primary ATA, vector 46) so that
     * disk I/O doesn't flood the log with "Unhandled interrupt: 46".  The
     * PIO driver uses polling; the IRQ is just acknowledged and discarded. */
    register_interrupt_handler(46, ata_irq_handler);

    return 0;
}

int ata_read_sectors(unsigned int lba, unsigned char count, void *buf)
{
    if (count == 0) {
        return 0;
    }

    /* Wait for the drive to be ready */
    if (ata_wait_bsy() < 0) {
        return -1;
    }

    /* Send LBA28 parameters ------------------------------------------------ */
    /* Drive/Head: LBA mode (bit 6), master (bit 4), LBA bits 24-27 */
    outb(ATA_PRIMARY_DRIVE_HEAD,
         (unsigned char)(ATA_DRIVE_MASTER | ((lba >> 24) & 0x0F)));

    outb(ATA_PRIMARY_SECTOR_COUNT, count);
    outb(ATA_PRIMARY_LBA_LO,  (unsigned char)( lba        & 0xFF));
    outb(ATA_PRIMARY_LBA_MID, (unsigned char)((lba >>  8) & 0xFF));
    outb(ATA_PRIMARY_LBA_HI,  (unsigned char)((lba >> 16) & 0xFF));

    /* Issue READ SECTORS command */
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ_SECTORS);

    /* 400 ns delay before first status read */
    ata_400ns_delay();

    /* Read each sector (512 bytes = 256 16-bit words) */
    unsigned short *dest = (unsigned short *)buf;
    unsigned char s;
    for (s = 0; s < count; s++) {
        if (ata_wait_bsy() < 0) {
            return -1;
        }
        if (ata_wait_drq() < 0) {
            return -1;
        }

        unsigned int w;
        for (w = 0; w < 256; w++) {
            dest[s * 256 + w] = inw(ATA_PRIMARY_DATA);
        }

        ata_400ns_delay();
    }

    return 0;
}

int ata_write_sectors(unsigned int lba, unsigned char count, const void *buf)
{
    if (count == 0) {
        return 0;
    }

    if (ata_wait_bsy() < 0) {
        return -1;
    }

    outb(ATA_PRIMARY_DRIVE_HEAD,
         (unsigned char)(ATA_DRIVE_MASTER | ((lba >> 24) & 0x0F)));

    outb(ATA_PRIMARY_SECTOR_COUNT, count);
    outb(ATA_PRIMARY_LBA_LO,  (unsigned char)( lba        & 0xFF));
    outb(ATA_PRIMARY_LBA_MID, (unsigned char)((lba >>  8) & 0xFF));
    outb(ATA_PRIMARY_LBA_HI,  (unsigned char)((lba >> 16) & 0xFF));

    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_SECTORS);
    ata_400ns_delay();

    const unsigned short *src = (const unsigned short *)buf;
    unsigned char s;
    for (s = 0; s < count; s++) {
        if (ata_wait_bsy() < 0) {
            return -1;
        }
        if (ata_wait_drq() < 0) {
            return -1;
        }

        unsigned int w;
        for (w = 0; w < 256; w++) {
            outw(ATA_PRIMARY_DATA, src[s * 256 + w]);
        }
        
        // Command finishes after transferring words, but standard mandates delay before BSY read
        ata_400ns_delay();
    }

    /* Important: flush drive cache to media */
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_bsy();

    return 0;
}
