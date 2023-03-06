/* system/core/gpttool/gpttool.c
**
** Copyright 2011, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>


#include <sys/random.h>

#include "gpt.h"



const uint8_t partition_type_efi[16] = {
  0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
  0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b,
};

#define EFI_VERSION 0x00010000
#define EFI_MAGIC "EFI PART"
#define EFI_ENTRIES 128
#define EFI_NAMELEN 36

struct efi_header {
  uint8_t magic[8];

  uint32_t version;
  uint32_t header_sz;

  uint32_t crc32;
  uint32_t reserved;

  uint64_t header_lba;
  uint64_t backup_lba;
  uint64_t first_lba;
  uint64_t last_lba;

  uint8_t volume_uuid[16];

  uint64_t entries_lba;

  uint32_t entries_count;
  uint32_t entries_size;
  uint32_t entries_crc32;
} __attribute__((packed));

struct efi_entry {
  uint8_t type_uuid[16];
  uint8_t uniq_uuid[16];
  uint64_t first_lba;
  uint64_t last_lba;
  uint64_t attr;
  uint16_t name[EFI_NAMELEN];
};

struct ptable {
  uint8_t mbr[512];
  union {
    struct efi_header header;
    uint8_t block[512];
  };
  struct efi_entry entry[EFI_ENTRIES];	
};



static void
get_uuid(uint8_t *uuid)
{
  if(getrandom(uuid, 16, 0) < 0) {
    perror("getrandom");
    exit(1);
  }
}

static void
init_mbr(uint8_t *mbr, uint32_t blocks)
{
  mbr[0x1be] = 0x00; // nonbootable
  mbr[0x1bf] = 0x00; // bogus CHS
  mbr[0x1c0] = 0x01;
  mbr[0x1c1] = 0x00;

  mbr[0x1c2] = 0xEE; // GPT partition
  mbr[0x1c3] = 0xFE; // bogus CHS
  mbr[0x1c4] = 0xFF;
  mbr[0x1c5] = 0xFF;

  mbr[0x1c6] = 0x01; // start
  mbr[0x1c7] = 0x00;
  mbr[0x1c8] = 0x00;
  mbr[0x1c9] = 0x00;

  memcpy(mbr + 0x1ca, &blocks, sizeof(uint32_t));

  mbr[0x1fe] = 0x55;
  mbr[0x1ff] = 0xaa;
}


static int
add_ptn(struct ptable *ptbl, uint64_t first, uint64_t last,
        const char *name, const uint8_t *type)
{
  struct efi_header *hdr = &ptbl->header;
  struct efi_entry *entry = ptbl->entry;
  unsigned n;

  if (first < 34) {
    fprintf(stderr,"partition '%s' overlaps partition table\n", name);
    return -1;
  }

  if (last > hdr->last_lba) {
    fprintf(stderr,"partition '%s' does not fit on disk\n", name);
    return -1;
  }
  for (n = 0; n < EFI_ENTRIES; n++, entry++) {
    if (entry->type_uuid[0])
      continue;
    memcpy(entry->type_uuid, type, 16);
    get_uuid(entry->uniq_uuid);
    entry->first_lba = first;
    entry->last_lba = last;
    for (n = 0; (n < EFI_NAMELEN) && *name; n++)
      entry->name[n] = *name++;
    return 0;
  }
  fprintf(stderr,"out of partition table entries\n");
  return -1;
}

#define _crc32(ptr,len) crc32(crc32(0,Z_NULL,0),(void*)(ptr),len)

static void
update_crc32(struct ptable *ptbl)
{
  uint32_t n;
  n = _crc32((void*) ptbl->entry, sizeof(ptbl->entry));
  ptbl->header.entries_crc32 = n;

  ptbl->header.crc32 = 0;
  n = _crc32((void*) &ptbl->header, sizeof(ptbl->header));
  ptbl->header.crc32 = n;
}




void
gpt_write_efi(int image_fd, uint32_t image_size_in_sectors,
              uint32_t fat32_first_sector,
              uint32_t fat32_last_sector)
{
  struct ptable ptbl = {};
  struct efi_header *hdr = &ptbl.header;

  init_mbr(ptbl.mbr, image_size_in_sectors - 1);

  memcpy(hdr->magic, EFI_MAGIC, sizeof(hdr->magic));
  hdr->version = EFI_VERSION;
  hdr->header_sz = sizeof(struct efi_header);
  hdr->header_lba = 1;
  hdr->backup_lba = image_size_in_sectors - 1;
  hdr->first_lba = fat32_first_sector;
  hdr->last_lba = fat32_last_sector;
  get_uuid(hdr->volume_uuid);
  hdr->entries_lba = 2;
  hdr->entries_count = 128;
  hdr->entries_size = sizeof(struct efi_entry);

  add_ptn(&ptbl, fat32_first_sector, fat32_last_sector,
          "EFI SYSTEM PARTITION",
          partition_type_efi);

  update_crc32(&ptbl);

  if(pwrite(image_fd, &ptbl, sizeof(ptbl), 0) != sizeof(ptbl)) {
    fprintf(stderr, "Failed to write partition table -- %m\n");
    exit(1);
  }

  hdr->header_lba = image_size_in_sectors - 1;
  hdr->backup_lba = 1;
  update_crc32(&ptbl);

  if(pwrite(image_fd, &ptbl.header, sizeof(ptbl.header),
            (image_size_in_sectors - 1) * 512) != sizeof(ptbl.header)) {
    fprintf(stderr, "Failed to write backup header -- %m\n");
    exit(1);
  }

  if(pwrite(image_fd, &ptbl.entry, sizeof(ptbl) - 1024,
            (image_size_in_sectors - GPT_SIZE_IN_SECTORS) * 512) != sizeof(ptbl) - 1024) {
    fprintf(stderr, "Failed to write backup entries -- %m\n");
    exit(1);
  }


}


