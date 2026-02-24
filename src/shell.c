#include "shell.h"
#include "vga.h"
#include "debug/print.h"
#include "debug/panic.h"
#include "drivers/timer.h"
#include "arch/i386/pic.h"

#include "fs/vfs.h"
#include "lib/string.h"
#include "drivers/ata.h"
#include "disk/mbr.h"
#include "apps/donut.h"


#include "disk/partition.h"
#include "fs/fat16.h"

typedef void (*cmd_fn)(const char* args);

static const char* skip_spaces(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static int starts_with(const char* s, const char* pfx) {
    while (*pfx) {
        if (*s++ != *pfx++) return 0;
    }
    return 1;
}

static void cmd_help(const char* args) {
    (void)args;
    vga_puts(
        "Commands:\n"
        "  parts\n"
        "  mount <0-3>\n"
        "  fatls\n"
        "  fatcat <file>\n"
        "  donut\n"
        "  mbr\n"
        "  cat <file>\n"
        "  clear\n"
        "  diskinfo\n"
        "  div0\n"
        "  echo <text>\n"
        "  help\n"
        "  hexdump\n"
        "  int3\n"
        "  ls\n"
        "  masks\n"
        "  panic\n"
        "  ticks\n" 
        "  uptime\n"
    );
}

static void cmd_clear(const char* args) {
    (void)args;
    vga_clear();
}

static void cmd_echo(const char* args) {
    args = skip_spaces(args);
    vga_puts(args);
    vga_putc('\n');
}

static void cmd_ticks(const char* args) {
    (void)args;
    vga_puts("ticks=");
    kprint_dec(timer_ticks());
    vga_putc('\n');
}

static void cmd_uptime(const char* args) {
    (void)args;
    vga_puts("uptime=");
    kprint_dec(timer_seconds());
    vga_puts("s\n");
}

static void cmd_masks(const char* args) {
    (void)args;
    vga_puts("PIC masks=");
    kprint_hex32(pic_get_masks());
    vga_putc('\n');
}

static void cmd_ls(const char* args) {
    (void)args;
    vfs_list();
}

static void cmd_cat(const char* args) {
    args = skip_spaces(args);
    if (!*args) {
        vga_puts("usage: cat <file>\n");
        return;
    }

    char name[32];
    unsigned i = 0;
    while (*args && *args != ' ' && *args != '\t' && i < (sizeof(name) - 1)) {
        name[i++] = *args++;
    }
    name[i] = '\0';

    vfs_node_t* n = vfs_open(name);
    if (!n) {
        vga_puts("cat: not found: ");
        vga_puts(name);
        vga_putc('\n');
        return;
    }

    uint8_t buf[256];
    size_t off = 0;
    while (off < n->size) {
        size_t got = vfs_read(n, off, sizeof(buf), buf);
        if (got == 0) break;
        for (size_t j = 0; j < got; j++) vga_putc((char)buf[j]);
        off += got;
    }
    if (n->size == 0) vga_puts("(empty)");
    vga_putc('\n');
}

static void cmd_int3(const char* args) {
    (void)args;
    __asm__ volatile ("int $0x03");
}

static void cmd_div0(const char* args) {
    (void)args;
    volatile int x = 1;
    volatile int y = 0;
    x = x / y;
    (void)x;
}

static void cmd_panic(const char* args) {
    (void)args;
    PANIC("Panic Command");
}

static void cmd_diskinfo(const char* args) {
    (void)args;
    if (!ata_present()) {
        vga_puts("diskinfo: no disk detected\n");
        return;
    }
    vga_puts("disk0 (primary master): ");
    vga_puts(ata_model());
    vga_putc('\n');
}

static uint32_t parse_u32(const char* s, int* ok) {
    *ok = 0;
    s = skip_spaces(s);
    if (!*s) return 0;
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = (v * 10u) + (uint32_t)(*s - '0');
        s++;
        *ok = 1;
    }
    return v;
}

static void hexdump_line(uint32_t base_off, const uint8_t* p, size_t n) {
    kprint_hex32(base_off);
    vga_puts(": ");
    for (size_t i = 0; i < 16; i++) {
        if (i < n) {
            kprint_hex8(p[i]);
            vga_putc(' ');
        } else {
            vga_puts("   ");
        }
    }
    vga_puts(" |");
    for (size_t i = 0; i < n; i++) {
        char c = (char)p[i];
        if (c < 32 || c > 126) c = '.';
        vga_putc(c);
    }
    vga_puts("|\n");
}

static void cmd_hexdump(const char* args) {
    if (!ata_present()) {
        vga_puts("hexdump: no disk detected\n");
        return;
    }

    int ok1 = 0;
    uint32_t lba = parse_u32(args, &ok1);
    if (!ok1) {
        vga_puts("usage: hexdump <lba> [count]\n");
        return;
    }

    args = skip_spaces(args);
    while (*args >= '0' && *args <= '9') args++;

    int ok2 = 0;
    uint32_t count32 = parse_u32(args, &ok2);
    uint8_t count = (ok2 && count32 > 0 && count32 <= 8) ? (uint8_t)count32 : 1;

    uint8_t sector[512 * 8];
    if (ata_read28(lba, count, sector) != 0) {
        vga_puts("hexdump: read failed\n");
        return;
    }

    size_t total = (size_t)count * 512;
    for (size_t off = 0; off < total; off += 16) {
        size_t n = (total - off >= 16) ? 16 : (total - off);
        hexdump_line((uint32_t)(lba * 512u + (uint32_t)off), &sector[off], n);
    }
}

static void cmd_mbr(const char* args) {
    (void)args;

    if (!ata_present()) {
        vga_puts("mbr: no disk detected\n");
        return;
    }

    uint8_t sector[512];
    if (ata_read28(0, 1, sector) != 0) {
        vga_puts("mbr: read failed\n");
        return;
    }

    const mbr_t* mbr = (const mbr_t*)sector;

    vga_puts("MBR signature: ");
    if (!mbr_is_valid(mbr)) {
        vga_puts("INVALID (expected 0x55AA)\n");
        return;
    }
    vga_puts("OK (0x55AA)\n");

    for (int i = 0; i < MBR_PARTITION_COUNT; i++) {
        const mbr_partition_t* p = &mbr->part[i];

        if (p->type == 0 || p->lba_count == 0) {
            vga_puts("Partition ");
            kprint_dec(i);
            vga_puts(": <empty>\n");
            continue;
        }

        vga_puts("Partition ");
        kprint_dec(i);
        vga_puts(": ");

        vga_puts("boot=");
        vga_puts((p->status == 0x80) ? "yes" : "no");

        vga_puts(" type=0x");
        kprint_hex8(p->type);

        vga_puts(" start=");
        kprint_dec(p->lba_start);

        vga_puts(" count=");
        kprint_dec(p->lba_count);

        vga_putc('\n');
    }
}

static void cmd_donut(const char* args) {
    (void)args;
    vga_puts("[donut] starting...\n");
    donut_run(800);
    vga_puts("[donut] returned\n");
}

static void cmd_parts(const char* args) {
    (void)args;

    if (!ata_present()) {
        vga_puts("parts: no disk detected\n");
        return;
    }

    part_info_t p[4];
    if (part_read_table(p) != 0) {
        vga_puts("parts: failed to read MBR\n");
        return;
    }

    for (int i = 0; i < 4; i++) {
        vga_puts("part ");
        kprint_dec(i);
        vga_puts(": ");

        if (!p[i].present) {
            vga_puts("<empty>\n");
            continue;
        }

        vga_puts("boot=");
        vga_puts(p[i].bootable ? "yes" : "no");
        vga_puts(" type=0x");
        kprint_hex8(p[i].type);
        vga_puts(" start=");
        kprint_dec(p[i].lba_start);
        vga_puts(" count=");
        kprint_dec(p[i].lba_count);
        vga_putc('\n');
    }
}

static void cmd_mount(const char* args) {
    int ok = 0;
    uint32_t idx = parse_u32(args, &ok);
    if (!ok || idx > 3) {
        vga_puts("usage: mount <0-3>\n");
        return;
    }

    part_info_t p;
    if (part_get((int)idx, &p) != 0) {
        vga_puts("mount: partition not present\n");
        return;
    }

    if (fat16_mount(p.lba_start) != 0) {
        vga_puts("mount: not FAT16 (or mount failed)\n");
        return;
    }

    vga_puts("mount: FAT16 mounted on part ");
    kprint_dec((uint32_t)idx);
    vga_putc('\n');
}

static void cmd_fatls(const char* args) {
    (void)args;
    fat16_ls_root();
}

static void cmd_fatcat(const char* args) {
    args = skip_spaces(args);
    if (!*args) {
        vga_puts("usage: fatcat <FILE.TXT>\n");
        return;
    }
    fat16_cat(args);
}

struct command {
    const char* name;
    cmd_fn fn;
};

static const struct command commands[] = {
    {"parts", cmd_parts},
{"mount", cmd_mount},
{"fatls", cmd_fatls},
{"fatcat", cmd_fatcat},
    {"donut", cmd_donut},
    {"mbr", cmd_mbr},
    {"help",  cmd_help},
    {"clear", cmd_clear},
    {"echo",  cmd_echo},
    {"ticks", cmd_ticks},
    {"uptime",cmd_uptime},
    {"masks", cmd_masks},
    {"ls",    cmd_ls},
    {"cat",   cmd_cat},
    {"int3",  cmd_int3},
    {"div0",  cmd_div0},
    {"panic", cmd_panic},
    {"diskinfo", cmd_diskinfo},
    {"hexdump",  cmd_hexdump},
};

void shell_execute(const char* line) {
    if (!line) return;

    line = skip_spaces(line);
    if (*line == '\0') return;

    for (unsigned i = 0; i < (sizeof(commands)/sizeof(commands[0])); i++) {
        const char* name = commands[i].name;

        if (kstrcmp(line, name) == 0) {
            commands[i].fn("");
            return;
        }

        if (starts_with(line, name)) {
            const char* after = line;
            while (*after && *after != ' ' && *after != '\t') after++;
            after = skip_spaces(after);
            commands[i].fn(after);
            return;
        }
    }

    vga_puts("Unknown command: ");
    vga_puts(line);
    vga_putc('\n');
}