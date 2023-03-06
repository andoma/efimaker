#pragma once

#include <stdint.h>

#define GPT_SIZE_IN_SECTORS 33

void gpt_write_efi(int image_fd, uint32_t image_size_in_sectors,
                   uint32_t fat32_first_sector,
                   uint32_t fat32_last_sector);
