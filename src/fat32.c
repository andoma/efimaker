#include "fat32.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

// https://www.cs.fsu.edu/~cop4610t/assignments/project3/spec/fatspec.pdf

typedef struct {
  uint8_t   BS_jmpBoot[3];
  uint8_t   BS_OEMName[8];
  uint16_t  BPB_BytsPerSec;
  uint8_t   BPB_SecPerClus;
  uint16_t  BPB_RsvdSecCnt;
  uint8_t   BPB_NumFATs;
  uint16_t  BPB_RootEntCnt;
  uint16_t  BPB_TotSec16;
  uint8_t   BPB_Media;
  uint16_t  BPB_FATSz16;
  uint16_t  BPB_SecPerTrk;
  uint16_t  BPB_NumHeads;
  uint32_t  BPB_HiddSec;
  uint32_t  BPB_TotSec32;

  // FAT32 Structure Starting at OFfset 36
  uint32_t  BPB_FATSz32;
  uint16_t  BPB_ExtFlags;
  uint16_t  BPB_FSVer;
  uint32_t  BPB_RootClus;
  uint16_t  BPB_FSInfo;
  uint16_t  BPB_BkBootSec;
  uint8_t   BPB_Reserved[12];
  uint8_t   BS_DrvNum;
  uint8_t   BS_Reserved1;
  uint8_t   BS_BootSig;
  uint32_t  BS_VolID;
  uint8_t   BS_VolLab[11];
  uint8_t   BS_FilSysType[8];

  uint8_t boot_code[420];

  uint16_t  aa55;

} __attribute__ ((packed)) bpb_t;


typedef struct {
    uint8_t signature_1[4];
    uint8_t reserved_1[480];
    uint8_t signature_2[4];
    uint32_t last_known_free_data_clusters;
    uint32_t most_recent_known_allocated_data_cluster;
    uint8_t reserved_2[12];
    uint8_t signature_3[4];
} __attribute__ ((packed)) fs_info_t;

typedef struct {
    uint8_t file_name[8];
    uint8_t file_ext[3];
    uint8_t attributes;
    uint8_t windowsNT_reserved;
    uint8_t create_time_tenths_of_second;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_accessed_date;
    uint16_t first_cluster_hi;
    uint16_t last_modified_time;
    uint16_t last_modified_date;
    uint16_t first_cluster_low;
    uint32_t file_size_in_bytes;
} __attribute__ ((packed)) dirent_t;


typedef struct {
  int fd;
  uint32_t first_sector;
  uint32_t sectors_per_fat;
  uint32_t sectors_per_cluster;
} image_t;



static uint32_t
get_cluster_offset(const image_t *img, uint32_t cluster)
{
  return (img->first_sector + 32 + img->sectors_per_fat * 2 + img->sectors_per_cluster * (cluster - 2)) * 512;
}


static void
fat32_write_sectors(const image_t *img, uint32_t sector, uint32_t num_sectors, const void *data)
{
  if(pwrite(img->fd, data, num_sectors * 512, (img->first_sector + sector) * 512) != num_sectors * 512) {
    perror("pwrite");
    exit(1);
  }
}


static void
copyname(uint8_t *dst, const char *src, size_t maxlen)
{
  size_t len = strlen(src);
  if(len > maxlen)
    len = maxlen;
  memcpy(dst, src, len);
}

static void
fat32_write_dir_entry_dir(const image_t *img, uint32_t cluster, uint32_t index,
                          const char *name, uint32_t first_cluster)
{
  dirent_t entry = {
    .file_name = "        ",
    .file_ext = "   ",
    .attributes = 0x10,
    .first_cluster_hi = first_cluster >> 16,
    .first_cluster_low = first_cluster,
    .file_size_in_bytes = 0,
  };
  copyname(entry.file_name, name, 8);

  if(pwrite(img->fd, &entry, sizeof(entry),
            get_cluster_offset(img, cluster) + sizeof(entry) * index) != sizeof(entry)) {
    perror("pwrite");
    exit(1);
  }
}

static void
fat32_write_dir_entry_file(const image_t *img, uint32_t cluster, uint32_t index,
                           const char *name, const char *ext, uint32_t first_cluster,
                           uint32_t file_size)
{
  dirent_t entry = {
    .file_name = "        ",
    .file_ext = "   ",
    .first_cluster_hi = first_cluster >> 16,
    .first_cluster_low = first_cluster,
    .file_size_in_bytes = file_size,
  };
  copyname(entry.file_name, name, 8);
  copyname(entry.file_ext, ext, 3);

  if(pwrite(img->fd, &entry, sizeof(entry),
            get_cluster_offset(img, cluster) + sizeof(entry) * index) != sizeof(entry)) {
    perror("pwrite");
    exit(1);
  }
}


void
fat32_make_efi(uint32_t image_fd, uint32_t first_sector, uint32_t num_sectors,
               const void *kernel, size_t kernel_size)
{
  if(kernel_size > 0xffffffff) {
    fprintf(stderr, "Kernel too big\n");
    exit(1);
  }
  image_t img = {
    .fd = image_fd,
    .first_sector = first_sector,
    // TODO: The sectors_per_cluster should probably be computed automatically to ensure
    // 1. There are at least 65525 clusters (to be considered fat32)
    // 2. A cluster has at most 32768 byte
    .sectors_per_cluster = 2,
  };

  const uint32_t volume_id = rand();

  const uint32_t num_clusters = num_sectors / img.sectors_per_cluster;
  if (num_clusters < 65525) {
    fprintf(stderr, "internal error: fat number of clusters < 65525 (%d)\n", num_clusters);
    exit(1);
  }

  num_sectors = num_clusters * img.sectors_per_cluster;

  const uint32_t bytes_per_cluster = img.sectors_per_cluster * 512;
  if (bytes_per_cluster > 32768) {
    fprintf(stderr, "internal error: fat cluster size > 32768 (%d)\n", bytes_per_cluster);
    exit(1);
  }

  img.sectors_per_fat = ((num_clusters + 127) / 128 + img.sectors_per_cluster - 1) & ~(img.sectors_per_cluster - 1);

  uint32_t kernel_clusters = (kernel_size + bytes_per_cluster - 1) / bytes_per_cluster;

  bpb_t bpb = {
    .BS_jmpBoot = {0xeb, 0xfe, 0x90},
    .BS_OEMName = "EFIBOOT ",
    .BPB_BytsPerSec = 512,
    .BPB_SecPerClus = img.sectors_per_cluster,
    .BPB_RsvdSecCnt = 32,
    .BPB_NumFATs = 2,
    .BPB_Media = 0xf8,
    .BPB_SecPerTrk = 63,
    .BPB_NumHeads = 64,
    .BPB_TotSec32 = num_sectors,
    .BPB_FATSz32 = img.sectors_per_fat,
    .BPB_RootClus = 2,
    .BPB_FSInfo = 1,
    .BPB_BkBootSec = 6,
    .BS_DrvNum = 0x80,
    .BS_BootSig = 0x29,
    .BS_VolID = volume_id,
    .BS_VolLab = "Volume 0   ",
    .BS_FilSysType = "FAT32   ",
    .aa55 = 0xaa55
  };

  fs_info_t fs_info = {
    .signature_1 = "RRaA",
    .signature_2 = "rrAa",
    .last_known_free_data_clusters = 0xffffffff,
    .most_recent_known_allocated_data_cluster = 0xffffffff,
    .signature_3 = {0x00, 0x00, 0x55, 0xAA},
  };

  fat32_write_sectors(&img, 0, 1, &bpb);
  fat32_write_sectors(&img, 1, 1, &fs_info);
  fat32_write_sectors(&img, 6, 1, &bpb);

  uint32_t *fat = calloc(img.sectors_per_fat, 512);
  fat[0] = 0x0ffffff8;
  fat[1] = 0x0fffffff;
  fat[2] = 0x0ffffff8;
  fat[3] = 0x0fffffff;
  fat[4] = 0x0fffffff;

  for(uint32_t i = 0; i < kernel_clusters - 1; i++) {
    fat[i + 5] = i + 6;
  }
  fat[5 + kernel_clusters - 1] = 0x0fffffff;

  fat32_write_sectors(&img, 32, img.sectors_per_fat, fat);
  fat32_write_sectors(&img, 32 + img.sectors_per_fat, img.sectors_per_fat, fat);

  // Write root directory
  fat32_write_dir_entry_dir(&img, 2, 0, "EFI", 3);

  // Write EFI directory
  fat32_write_dir_entry_dir(&img, 3, 0, ".", 3);
  fat32_write_dir_entry_dir(&img, 3, 1, "..", 0);
  fat32_write_dir_entry_dir(&img, 3, 2, "BOOT", 4);

  // Write BOOT directory
  fat32_write_dir_entry_dir(&img, 4, 0, ".", 4);
  fat32_write_dir_entry_dir(&img, 4, 1, "..", 3);
  fat32_write_dir_entry_file(&img, 4, 2, "BOOTX64", "EFI", 5, kernel_size);

  if(pwrite(image_fd, kernel, kernel_size,get_cluster_offset(&img, 5)) != kernel_size) {
    perror("pwrite");
    exit(1);
  }
}

