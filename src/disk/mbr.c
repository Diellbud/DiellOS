#include "mbr.h"

int mbr_is_valid(const mbr_t* mbr) {
    if (!mbr) return 0;
    return (mbr->signature == MBR_SIGNATURE_VALUE) ? 1 : 0;
}