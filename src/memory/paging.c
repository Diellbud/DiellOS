#include "paging.h"

#define PAGE_PRESENT 0x1
#define PAGE_RW      0x2
#define PAGE_TABLE_ENTRIES 1024
#define LOW_IDENTITY_TABLES 16
#define TOTAL_PAGE_TABLES 64

static uint32_t page_directory[PAGE_TABLE_ENTRIES] __attribute__((aligned(4096)));
static uint32_t page_tables[TOTAL_PAGE_TABLES][PAGE_TABLE_ENTRIES] __attribute__((aligned(4096)));
static uint32_t next_free_table = 0;

static uint32_t* alloc_page_table(void)
{
    if (next_free_table >= TOTAL_PAGE_TABLES) return 0;
    return page_tables[next_free_table++];
}

static uint32_t* ensure_page_table(uint32_t dir_index)
{
    uint32_t* table;

    if (page_directory[dir_index] & PAGE_PRESENT) {
        return (uint32_t*)(uintptr_t)(page_directory[dir_index] & 0xFFFFF000u);
    }

    table = alloc_page_table();
    if (!table) return 0;

    for (uint32_t i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        table[i] = 0;
    }

    page_directory[dir_index] = ((uint32_t)(uintptr_t)table) | PAGE_PRESENT | PAGE_RW;
    return table;
}

static void map_identity_range(uint32_t base, uint32_t size)
{
    uint32_t start = base & 0xFFFFF000u;
    uint32_t end = (base + size + 0xFFFu) & 0xFFFFF000u;

    if (size == 0) return;
    if (end < start) return;

    for (uint32_t addr = start; addr < end; addr += 0x1000u) {
        uint32_t dir_index = addr >> 22;
        uint32_t table_index = (addr >> 12) & 0x3FFu;
        uint32_t* table = ensure_page_table(dir_index);
        if (!table) return;
        table[table_index] = addr | PAGE_PRESENT | PAGE_RW;
    }
}

void paging_init(uint32_t extra_identity_base, uint32_t extra_identity_size)
{
    for (uint32_t i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        page_directory[i] = 0;
    }

    next_free_table = 0;

    for (uint32_t table = 0; table < LOW_IDENTITY_TABLES; table++) {
        uint32_t* pt = alloc_page_table();
        if (!pt) return;

        for (uint32_t i = 0; i < PAGE_TABLE_ENTRIES; i++) {
            uint32_t page = table * PAGE_TABLE_ENTRIES + i;
            pt[i] = (page * 0x1000u) | PAGE_PRESENT | PAGE_RW;
        }

        page_directory[table] = ((uint32_t)(uintptr_t)pt) | PAGE_PRESENT | PAGE_RW;
    }

    map_identity_range(extra_identity_base, extra_identity_size);

    __asm__ __volatile__("mov %0, %%cr3" :: "r"(page_directory));

    uint32_t cr0;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ __volatile__("mov %0, %%cr0" :: "r"(cr0));
}
