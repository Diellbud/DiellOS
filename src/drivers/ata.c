#include "ata.h"
#include "../arch/i386/io.h"
#include "../lib/string.h"

#define ATA_IO_BASE   0x1F0
#define ATA_IO_CTRL   0x3F6

#define ATA_REG_DATA      0x00
#define ATA_REG_ERROR     0x01
#define ATA_REG_FEATURES  0x01
#define ATA_REG_SECCOUNT0 0x02
#define ATA_REG_LBA0      0x03
#define ATA_REG_LBA1      0x04
#define ATA_REG_LBA2      0x05
#define ATA_REG_HDDEVSEL  0x06
#define ATA_REG_COMMAND   0x07
#define ATA_REG_STATUS    0x07

#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF   0x20
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

#define ATA_CMD_IDENTIFY  0xEC
#define ATA_CMD_READ_PIO  0x20

static ata_device_t g_dev;

static inline void ata_delay_400ns(void) {
    (void)inb(ATA_IO_CTRL);
    (void)inb(ATA_IO_CTRL);
    (void)inb(ATA_IO_CTRL);
    (void)inb(ATA_IO_CTRL);
}

static int ata_wait_not_busy(void) {
    for (int i = 0; i < 100000; i++) {
        uint8_t s = inb(ATA_IO_BASE + ATA_REG_STATUS);
        if ((s & ATA_SR_BSY) == 0) return 0;
    }
    return -1;
}

static int ata_wait_drq_or_err(void) {
    for (int i = 0; i < 100000; i++) {
        uint8_t s = inb(ATA_IO_BASE + ATA_REG_STATUS);
        if (s & ATA_SR_ERR) return -1;
        if (s & ATA_SR_DF)  return -1;
        if (s & ATA_SR_DRQ) return 0;
    }
    return -1;
}

static void ata_select_primary_master(uint32_t lba) {
    outb(ATA_IO_BASE + ATA_REG_HDDEVSEL, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_delay_400ns();
}

static void ata_extract_model(char out[41], const uint16_t* id_words) {
    int idx = 0;
    for (int w = 27; w <= 46; w++) {
        uint16_t v = id_words[w];
        char hi = (char)((v >> 8) & 0xFF);
        char lo = (char)(v & 0xFF);
        out[idx++] = hi;
        out[idx++] = lo;
    }
    out[40] = '\0';

    for (int i = 39; i >= 0; i--) {
        if (out[i] == ' ' || out[i] == '\0') out[i] = '\0';
        else break;
    }
}

void ata_init(void) {
    kmemset(&g_dev, 0, sizeof(g_dev));

    ata_select_primary_master(0);

    outb(ATA_IO_BASE + ATA_REG_SECCOUNT0, 0);
    outb(ATA_IO_BASE + ATA_REG_LBA0, 0);
    outb(ATA_IO_BASE + ATA_REG_LBA1, 0);
    outb(ATA_IO_BASE + ATA_REG_LBA2, 0);

    outb(ATA_IO_BASE + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay_400ns();

    uint8_t status = inb(ATA_IO_BASE + ATA_REG_STATUS);
    if (status == 0) {
        g_dev.present = 0;
        return;
    }

    if (ata_wait_not_busy() != 0) {
        g_dev.present = 0;
        return;
    }

    uint8_t lba1 = inb(ATA_IO_BASE + ATA_REG_LBA1);
    uint8_t lba2 = inb(ATA_IO_BASE + ATA_REG_LBA2);
    if (lba1 != 0 || lba2 != 0) {
        g_dev.present = 0;
        return;
    }

    if (ata_wait_drq_or_err() != 0) {
        g_dev.present = 0;
        return;
    }

    uint16_t id[256];
    for (int i = 0; i < 256; i++) {
        id[i] = inw(ATA_IO_BASE + ATA_REG_DATA);
    }

    g_dev.present = 1;
    ata_extract_model(g_dev.model, id);
}

int ata_present(void) {
    return g_dev.present;
}

const char* ata_model(void) {
    return g_dev.model;
}

int ata_read28(uint32_t lba, uint8_t count, void* out) {
    if (!g_dev.present) return -1;
    if (count == 0) return 0;
    if (lba > 0x0FFFFFFF) return -1;

    uint8_t* buf = (uint8_t*)out;

    if (ata_wait_not_busy() != 0) return -1;

    ata_select_primary_master(lba);

    outb(ATA_IO_BASE + ATA_REG_SECCOUNT0, count);
    outb(ATA_IO_BASE + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_IO_BASE + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_IO_BASE + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_IO_BASE + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    for (uint8_t s = 0; s < count; s++) {
        if (ata_wait_drq_or_err() != 0) return -1;

        for (int i = 0; i < 256; i++) {
            uint16_t w = inw(ATA_IO_BASE + ATA_REG_DATA);
            *buf++ = (uint8_t)(w & 0xFF);
            *buf++ = (uint8_t)((w >> 8) & 0xFF);
        }
    }

    return 0;
}
