#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "gpt.h"
#include "fat32.h"

int
main(int argc, char **argv)
{
  if(argc < 1) {
    fprintf(stderr, "Usage %s <DISK.IMG> [<KERNEL>]\n", argv[0]);
    exit(1);
  }

  const uint32_t image_size_in_sectors = 1024 * 1024 * 1024 / 512;

  const char *disk_image_path = argv[1];
  int image_fd = open(disk_image_path, O_CREAT | O_RDWR, 0644);
  if(image_fd == -1) {
    fprintf(stderr, "Unable to open image %s -- %m\n", disk_image_path);
    exit(1);
  }

  if(ftruncate(image_fd, 0) < 0) {
    perror("ftruncate");
    exit(1);
  }

  if(ftruncate(image_fd, image_size_in_sectors * 512) < 0) {
    perror("ftruncate");
    exit(1);
  }

  const uint32_t fat32_first_sector = 2048;
  const uint32_t fat32_last_sector  = image_size_in_sectors - 2048 - 1;

  if(argc > 2) {
    const char *kernel_path = argv[2];
    int kfd = open(kernel_path, O_RDONLY);

    if(kfd == -1) {
      fprintf(stderr, "Unable to open kernel %s -- %m\n", kernel_path);
      exit(1);
    }

    struct stat st;
    if(fstat(kfd, &st) == -1) {
      perror("stat");
      exit(1);
    }

    void *kmem = malloc(st.st_size);

    if(read(kfd, kmem, st.st_size) != st.st_size) {
      fprintf(stderr, "Unable to read kernel %s\n", kernel_path);
      exit(1);
    }

    close(kfd);

    fat32_make_efi(image_fd, fat32_first_sector,
                   fat32_last_sector - fat32_first_sector + 1,
                   kmem, st.st_size);
  }

  gpt_write_efi(image_fd, image_size_in_sectors,
                fat32_first_sector, fat32_last_sector);

  return 0;
}
