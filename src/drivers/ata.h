#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
    int present;
    char model[41];
} ata_device_t;

void ata_init(void);

int ata_present(void);

const char* ata_model(void);

int ata_read28(uint32_t lba, uint8_t count, void* out);
