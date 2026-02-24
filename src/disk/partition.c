#include "partition.h"
#include "mbr.h"
#include "../drivers/ata.h"
#include "../lib/string.h"

static part_info_t g_parts[4];
static int g_loaded = 0;

int part_read_table(part_info_t out[4]) {
    if (!ata_present()) return -1;

    uint8_t sector[512];
    if (ata_read28(0, 1, sector) != 0) return -1;

    const mbr_t* mbr = (const mbr_t*)sector;
    if (!mbr_is_valid(mbr)) return -1;

    for (int i = 0; i < 4; i++) {
        const mbr_partition_t* p = &mbr->part[i];
        g_parts[i].present  = (p->type != 0 && p->lba_count != 0);
        g_parts[i].type     = p->type;
        g_parts[i].lba_start= p->lba_start;
        g_parts[i].lba_count= p->lba_count;
        g_parts[i].bootable = (p->status == 0x80) ? 1 : 0;

        if (out) out[i] = g_parts[i];
    }

    g_loaded = 1;
    return 0;
}

int part_get(int index, part_info_t* out) {
    if (index < 0 || index > 3) return -1;
    if (!g_loaded) {
        if (part_read_table(0) != 0) return -1;
    }
    if (out) *out = g_parts[index];
    return g_parts[index].present ? 0 : -1;
}