#pragma once

#include <stdint.h>
#include <stddef.h>

void fat32_make_efi(uint32_t image_fd, uint32_t first_sector, uint32_t num_sectors,
                    const void *kernel, size_t kernel_size);
