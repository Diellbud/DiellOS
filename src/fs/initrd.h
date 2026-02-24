#pragma once
#include "../boot/multiboot.h"

int initrd_mount_from_multiboot(const multiboot_info_t* mb);
