#include "fat16.h"
#include "../drivers/ata.h"
#include "../debug/print.h"
#include "../vga.h"
#include "../lib/string.h"

typedef struct {
    int mounted;

    uint32_t part_lba;

    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t fat_size_sectors;   

    uint32_t fat_lba;            
    uint32_t root_dir_lba;
    uint32_t root_dir_sectors;
    uint32_t first_data_lba;
} fat16_t;

static fat16_t g_fat;

static uint16_t rd16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static int is_space(char c) { return (c == ' ' || c == '\t' || c == '\n' || c == '\r'); }
static char upc(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c; }

static void make_83_name(const char* in, char out11[11]) {
    
    for (int i = 0; i < 11; i++) out11[i] = ' ';

    
    while (*in && is_space(*in)) in++;

    int oi = 0;
    
    while (*in && *in != '.' && !is_space(*in) && oi < 8) {
        out11[oi++] = upc(*in++);
    }

    
    while (*in && *in != '.') {
        if (is_space(*in)) break;
        in++;
    }
    if (*in == '.') in++;

    int ej = 0;
    while (*in && !is_space(*in) && ej < 3) {
        out11[8 + ej] = upc(*in++);
        ej++;
    }
}

static int name11_eq(const uint8_t name[11], const char want[11]) {
    for (int i = 0; i < 11; i++) {
        if ((char)name[i] != want[i]) return 0;
    }
    return 1;
}

static uint32_t cluster_to_lba(uint16_t cluster) {
    return g_fat.first_data_lba + (uint32_t)(cluster - 2u) * (uint32_t)g_fat.sectors_per_cluster;
}

static uint16_t fat16_next_cluster(uint16_t cluster) {
    
    uint32_t fat_offset = (uint32_t)cluster * 2u;
    uint32_t fat_sector = fat_offset / 512u;
    uint32_t ent_offset = fat_offset % 512u;

    uint8_t sec[512];
    if (ata_read28(g_fat.fat_lba + fat_sector, 1, sec) != 0) return 0xFFFF;

    return rd16(&sec[ent_offset]);
}

int fat16_mount(uint32_t part_lba_start) {
    kmemset(&g_fat, 0, sizeof(g_fat));

    if (!ata_present()) return -1;

    uint8_t bs[512];
    if (ata_read28(part_lba_start, 1, bs) != 0) return -1;

    
    if (rd16(&bs[510]) != 0xAA55) return -1;

    g_fat.part_lba = part_lba_start;
    g_fat.bytes_per_sector     = rd16(&bs[11]);
    g_fat.sectors_per_cluster  = bs[13];
    g_fat.reserved_sectors     = rd16(&bs[14]);
    g_fat.num_fats             = bs[16];
    g_fat.root_entry_count     = rd16(&bs[17]);
    g_fat.fat_size_sectors     = rd16(&bs[22]);

    if (g_fat.bytes_per_sector != 512) return -1;
    if (g_fat.sectors_per_cluster == 0) return -1;
    if (g_fat.num_fats == 0) return -1;
    if (g_fat.fat_size_sectors == 0) return -1;

    g_fat.root_dir_sectors =
        ((uint32_t)g_fat.root_entry_count * 32u + (uint32_t)g_fat.bytes_per_sector - 1u) /
        (uint32_t)g_fat.bytes_per_sector;

    uint32_t first_fat = (uint32_t)g_fat.reserved_sectors;
    uint32_t first_root =
        first_fat + (uint32_t)g_fat.num_fats * (uint32_t)g_fat.fat_size_sectors;

    uint32_t first_data =
        first_root + g_fat.root_dir_sectors;

    g_fat.fat_lba       = g_fat.part_lba + first_fat;
    g_fat.root_dir_lba  = g_fat.part_lba + first_root;
    g_fat.first_data_lba= g_fat.part_lba + first_data;

    g_fat.mounted = 1;
    return 0;
}

static void print_name_83(const uint8_t n[11]) {
    
    int i = 0;
    while (i < 8 && n[i] != ' ') { vga_putc((char)n[i]); i++; }

    
    int has_ext = 0;
    for (int j = 0; j < 3; j++) if (n[8+j] != ' ') has_ext = 1;
    if (has_ext) {
        vga_putc('.');
        for (int j = 0; j < 3; j++) {
            if (n[8+j] == ' ') break;
            vga_putc((char)n[8+j]);
        }
    }
}

void fat16_ls_root(void) {
    if (!g_fat.mounted) {
        vga_puts("fatls: not mounted (use: mount <0-3>)\n");
        return;
    }

    uint8_t sec[512];
    uint32_t total = g_fat.root_dir_sectors;

    for (uint32_t s = 0; s < total; s++) {
        if (ata_read28(g_fat.root_dir_lba + s, 1, sec) != 0) {
            vga_puts("fatls: read failed\n");
            return;
        }

        for (int off = 0; off < 512; off += 32) {
            const uint8_t* e = &sec[off];
            uint8_t first = e[0];

            if (first == 0x00) return;      
            if (first == 0xE5) continue;    
            uint8_t attr = e[11];
            if (attr == 0x0F) continue;     
            if (attr & 0x08) continue;      

            print_name_83(e);
            vga_puts("  ");
            uint32_t size = rd32(&e[28]);
            kprint_dec(size);
            vga_puts(" bytes\n");
        }
    }
}

static int find_root_entry(const char want11[11], uint8_t out_entry[32]) {
    uint8_t sec[512];
    uint32_t total = g_fat.root_dir_sectors;

    for (uint32_t s = 0; s < total; s++) {
        if (ata_read28(g_fat.root_dir_lba + s, 1, sec) != 0) return -1;

        for (int off = 0; off < 512; off += 32) {
            const uint8_t* e = &sec[off];
            uint8_t first = e[0];

            if (first == 0x00) return -1;      
            if (first == 0xE5) continue;
            uint8_t attr = e[11];
            if (attr == 0x0F) continue;
            if (attr & 0x08) continue;

            if (name11_eq(e, want11)) {
                kmemcpy(out_entry, e, 32);
                return 0;
            }
        }
    }
    return -1;
}

void fat16_cat(const char* user_name) {
    if (!g_fat.mounted) {
        vga_puts("fatcat: not mounted (use: mount <0-3>)\n");
        return;
    }
    if (!user_name || !*user_name) {
        vga_puts("usage: fatcat <FILE.TXT>\n");
        return;
    }

    char want11[11];
    make_83_name(user_name, want11);

    uint8_t ent[32];
    if (find_root_entry(want11, ent) != 0) {
        vga_puts("fatcat: not found: ");
        vga_puts(user_name);
        vga_putc('\n');
        return;
    }

    uint16_t first_cluster = rd16(&ent[26]);
    uint32_t size = rd32(&ent[28]);

    if (size == 0) { vga_puts("(empty)\n"); return; }
    if (first_cluster < 2) { vga_puts("fatcat: bad cluster\n"); return; }

    uint8_t sec[512];
    uint16_t cl = first_cluster;
    uint32_t remaining = size;

    while (remaining > 0) {
        uint32_t lba = cluster_to_lba(cl);

        for (uint8_t s = 0; s < g_fat.sectors_per_cluster; s++) {
            if (ata_read28(lba + s, 1, sec) != 0) {
                vga_puts("fatcat: read failed\n");
                return;
            }

            uint32_t to_print = (remaining > 512) ? 512 : remaining;
            for (uint32_t i = 0; i < to_print; i++) vga_putc((char)sec[i]);
            remaining -= to_print;

            if (remaining == 0) break;
        }

        uint16_t next = fat16_next_cluster(cl);
        if (next >= 0xFFF8) break;          
        if (next == 0xFFF7 || next == 0xFFFF || next < 2) {
            vga_puts("\nfatcat: bad FAT chain\n");
            return;
        }
        cl = next;
    }

    vga_putc('\n');
}