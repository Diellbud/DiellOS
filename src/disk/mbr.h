#pragma once
#include <stdint.h>

#define MBR_SIGNATURE_OFFSET 510
#define MBR_SIGNATURE_VALUE  0xAA55u
#define MBR_PARTITION_TABLE_OFFSET 446
#define MBR_PARTITION_COUNT 4

typedef struct {
    uint8_t  status;      
    uint8_t  chs_first[3]; 
    uint8_t  type;        
    uint8_t  chs_last[3];  
    uint32_t lba_start;   
    uint32_t lba_count;   
} __attribute__((packed)) mbr_partition_t;

typedef struct {
    uint8_t          bootstrap[446];
    mbr_partition_t  part[4];
    uint16_t         signature; 
} __attribute__((packed)) mbr_t;

int mbr_is_valid(const mbr_t* mbr);