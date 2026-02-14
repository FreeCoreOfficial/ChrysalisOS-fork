/* kernel/storage/ata.c */
#include "ata.h"
#include <stdint.h>
#include <stddef.h>
#include "../arch/i386/io.h"        /* inb/outb/inw/outw */

#ifndef ATA_LOG_SERIAL_ONLY
#define ATA_LOG_SERIAL_ONLY 0
#endif

#if ATA_LOG_SERIAL_ONLY
#include "../drivers/serial.h"
#else
#include "../terminal.h" /* terminal_writestring() for simple debug */
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline void ata_log_write(const char* s)
{
#if ATA_LOG_SERIAL_ONLY
    serial_write_string(s);
#else
    terminal_writestring(s);
#endif
}

/* Primary bus */
#define ATA_PRIMARY_IO   0x1F0
#define ATA_PRIMARY_CTRL 0x3F6

/* registers (offsets from IO base) */
#define ATA_REG_DATA       0x00
#define ATA_REG_ERROR      0x01
#define ATA_REG_FEATURES   0x01
#define ATA_REG_SECCOUNT0  0x02
#define ATA_REG_LBA0       0x03
#define ATA_REG_LBA1       0x04
#define ATA_REG_LBA2       0x05
#define ATA_REG_HDDEVSEL   0x06
#define ATA_REG_COMMAND    0x07
#define ATA_REG_STATUS     0x07

/* control registers (from CONTROL base) */
#define ATA_CTRL_ALTSTATUS 0x00
#define ATA_CTRL_DEVICECTL 0x02

/* commands */
#define ATA_CMD_IDENTIFY     0xEC
#define ATA_CMD_READ_PIO     0x20
#define ATA_CMD_WRITE_PIO    0x30
#define ATA_CMD_CACHE_FLUSH  0xE7

/* status bits */
#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

/* global MBR write protection flag (default disabled) */
static int g_allow_mbr_write = 0;
static int g_skip_cache_flush = 0;

/* small io wait (400ns) - 4 reads of alt status */
static inline void ata_io_wait(void)
{
    (void)inb(ATA_PRIMARY_CTRL + ATA_CTRL_ALTSTATUS);
    (void)inb(ATA_PRIMARY_CTRL + ATA_CTRL_ALTSTATUS);
    (void)inb(ATA_PRIMARY_CTRL + ATA_CTRL_ALTSTATUS);
    (void)inb(ATA_PRIMARY_CTRL + ATA_CTRL_ALTSTATUS);
}

/* helper: read status */
static inline uint8_t ata_read_status(void)
{
    return inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
}

/* select primary master (0xA0) or slave (0xB0). we always use master (0xA0). */
static inline void ata_select_master(void)
{
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xA0);
    ata_io_wait();
}

/* wait until BSY cleared, return last status */
static uint8_t ata_wait_bsy_clear(void)
{
    uint8_t status = ata_read_status();
    for (int i = 0; i < 100000; i++) {
        if (!(status & ATA_SR_BSY))
            return status;
        status = ata_read_status();
    }
    return status;
}

/* simple hex dumper for debug */
static void ata_dump_hex(const uint8_t* buf, int count)
{
    const char hex[] = "0123456789ABCDEF";
    char out[4];

    for (int i = 0; i < count; i++) {
        uint8_t b = buf[i];
        out[0] = hex[b >> 4];
        out[1] = hex[b & 0xF];
        out[2] = ' ';
        out[3] = 0;
        ata_log_write(out);

        if ((i + 1) % 16 == 0)
            ata_log_write("\n");
    }
}

/* wrapper to expose MBR write control */
void ata_set_allow_mbr_write(int enabled)
{
    g_allow_mbr_write = enabled ? 1 : 0;
}

void ata_set_skip_cache_flush(int enabled)
{
    g_skip_cache_flush = enabled ? 1 : 0;
}

static void ata_soft_reset(void)
{
    /* SRST bit in device control */
    outb(ATA_PRIMARY_CTRL + ATA_CTRL_DEVICECTL, 0x04);
    ata_io_wait();
    outb(ATA_PRIMARY_CTRL + ATA_CTRL_DEVICECTL, 0x00);
    ata_io_wait();
}

/* Convenience wrapper */
int ata_read_sector(uint32_t lba, uint8_t* buffer)
{
    return ata_pio_read28(lba, buffer);
}

int ata_read_sector_retry(uint32_t lba, uint8_t* buffer, int retries)
{
    if (retries < 1) retries = 1;
    for (int i = 0; i < retries; i++) {
        int r = ata_read_sector(lba, buffer);
        if (r == 0) return 0;
        ata_log_write("[ATA] read retry\n");
        ata_soft_reset();
    }
    return -1;
}

/* IDENTIFY DEVICE implementation */
int ata_identify(uint16_t* buffer)
{
    if (!buffer)
        return -4;

    ata_select_master();

    /* clear registers per spec */
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA0, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA1, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA2, 0);

    ata_io_wait();

    /* send IDENTIFY */
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    /* read status */
    uint8_t status = ata_read_status();
    if (status == 0)
        return -1; /* no device */

    /* wait BSY clear */
    for (int i = 0; i < 100000; i++) {
        if (!(status & ATA_SR_BSY)) break;
        status = ata_read_status();
    }
    if (status & ATA_SR_BSY)
        return -5;

    /* check for error */
    if (status & ATA_SR_ERR)
        return -2;

    /* wait DRQ set */
    for (int i = 0; i < 100000; i++) {
        if (status & ATA_SR_DRQ) break;
        status = ata_read_status();
        if (status & ATA_SR_ERR)
            return -3;
    }
    if (!(status & ATA_SR_DRQ))
        return -6;

    /* read 256 words (512 bytes) */
    for (int i = 0; i < 256; ++i) {
        buffer[i] = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
    }

    return 0;
}

/* decode identify: model string + LBA28 sectors if present */
int ata_decode_identify(const uint16_t* id_buf, char* model, size_t model_len, uint32_t* lba28_sectors)
{
    if (!id_buf || !model || model_len == 0)
        return -1;

    /* model is words 27..46 (20 words -> 40 chars). Each word: high byte first then low byte. */
    const int MODEL_WORD = 27;
    size_t out_pos = 0;
    for (int w = 0; w < 20 && out_pos + 1 < model_len; ++w) {
        uint16_t word = id_buf[MODEL_WORD + w];
        char c1 = (char)(word >> 8);
        char c2 = (char)(word & 0xFF);

        if (out_pos < model_len - 1) model[out_pos++] = c1;
        if (out_pos < model_len - 1) model[out_pos++] = c2;
    }
    /* trim trailing spaces */
    while (out_pos > 0 && out_pos < model_len && (model[out_pos - 1] == ' ' || model[out_pos - 1] == '\0'))
        out_pos--;
    model[out_pos < model_len ? out_pos : model_len - 1] = '\0';

    if (lba28_sectors) {
        /* words 60..61 contain total number of user-addressable sectors for LBA28 */
        uint32_t low = (uint32_t)id_buf[60];
        uint32_t high = (uint32_t)id_buf[61];
        *lba28_sectors = (high << 16) | low;
    }

    return 0;
}

/* read single sector using LBA28 PIO */
int ata_pio_read28(uint32_t lba, uint8_t* buffer)
{
    if (!buffer)
        return -1;

    if (lba & 0xF0000000) /* LBA must be 28-bit */
        return -2;

    ata_select_master();

    /* set sector count = 1 */
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, 1);

    /* set LBA low/mid/high */
    outb(ATA_PRIMARY_IO + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));

    /* select drive and high 4 bits of LBA (0xE0 = 1110 0000 -> LBA mode + master) */
    uint8_t head = 0xE0 | (uint8_t)((lba >> 24) & 0x0F);
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, head);
    ata_io_wait();

    /* issue read command */
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    /* wait for BSY clear */
    uint8_t status = ata_wait_bsy_clear();

    /* check error */
    if (status & ATA_SR_ERR)
        return -3;

    /* wait DRQ */
    for (int i = 0; i < 100000; i++) {
        if (status & ATA_SR_DRQ) break;
        status = ata_read_status();
        if (status & ATA_SR_ERR)
            return -4;
    }
    if (!(status & ATA_SR_DRQ))
        return -5;

    /* read 256 words (512 bytes) */
    for (int i = 0; i < 256; ++i) {
        uint16_t w = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
        /* write little-endian into buffer */
        buffer[i * 2 + 0] = (uint8_t)(w & 0xFF);
        buffer[i * 2 + 1] = (uint8_t)(w >> 8);
    }

    /* final status check (optional) */
    ata_io_wait();
    return 0;
}

/* write single sector using LBA28 PIO */
int ata_write_sector(uint32_t lba, const uint8_t* buffer)
{
    if (!buffer)
        return -1;

    if (lba & 0xF0000000)
        return -2;

    /* protecție MBR */
    if (lba == 0 && !g_allow_mbr_write)
        return -100;

    ata_select_master();

    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, 1);

    outb(ATA_PRIMARY_IO + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));

    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL,
         0xE0 | ((lba >> 24) & 0x0F));

    ata_io_wait();

    /* issue write command */
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    uint8_t status = ata_wait_bsy_clear();
    if (status & ATA_SR_ERR) {
        ata_log_write("[ATA] write: ERR after BSY clear\n");
        return -3;
    }

    for (int i = 0; i < 100000; i++) {
        if (status & ATA_SR_DRQ) break;
        status = ata_read_status();
        if (status & ATA_SR_ERR) {
            ata_log_write("[ATA] write: ERR waiting DRQ\n");
            return -4;
        }
    }
    if (!(status & ATA_SR_DRQ)) {
        ata_log_write("[ATA] write: DRQ timeout\n");
        return -5;
    }

    /* write 256 words */
    for (int i = 0; i < 256; i++) {
        uint16_t w =
            ((uint16_t)buffer[i * 2 + 1] << 8) |
             (uint16_t)buffer[i * 2 + 0];

        outw(ATA_PRIMARY_IO + ATA_REG_DATA, w);
    }

    /* flush cache */
    if (!g_skip_cache_flush) {
        outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
        ata_wait_bsy_clear();
    }

    return 0;
}

int ata_write_sector_retry(uint32_t lba, const uint8_t* buffer, int retries)
{
    if (retries < 1) retries = 1;
    for (int i = 0; i < retries; i++) {
        int r = ata_write_sector(lba, buffer);
        if (r == 0) return 0;
        ata_log_write("[ATA] write retry\n");
        ata_soft_reset();
    }
    return -1;
}

/* init + test (reads sector 0 and dumps first 128 bytes) */
void ata_init(void)
{
    ata_log_write("[ATA] init\n");

    static uint8_t sector[512];

    if (ata_read_sector(0, sector) != 0) {
        ata_log_write("[ATA] read sector 0 FAILED\n");
        return;
    }

    ata_log_write("[ATA] sector 0 read OK\n");

    ata_dump_hex(sector, 128);

    /* semnătura MBR */
    if (sector[510] == 0x55 && sector[511] == 0xAA)
        ata_log_write("\n[ATA] MBR signature OK (55 AA)\n");
    else
        ata_log_write("\n[ATA] MBR signature MISSING\n");
}

#ifdef __cplusplus
}
#endif
