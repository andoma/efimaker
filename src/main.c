#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "gpt.h"
#include "fat32.h"


static void
usage(const char *argv0)
{
  fprintf(stderr, "Usage %s ...\n", argv0);
  fprintf(stderr, "  -o   DISKIMAGE     Path to output image\n");
  fprintf(stderr, "  -k   KERNEL        Path to kernel\n");
  fprintf(stderr, "  -s   SIZE          EFI partition size in MB (Default 1024)\n");
  fprintf(stderr, "  -a   ARCHITECTURE  Architecture to build the image for (e.g. AA64, x64). Default X64. \n");
}


int
main(int argc, char **argv)
{
  int opt;
  const char *disk_image_path = NULL;
  const char *kernel_path = NULL;
  const char *arch = "x64";
  uint32_t image_size_in_sectors = 1024 * 1024 * 1024 / 512;

  while ((opt = getopt(argc, argv, "o:k:s:a:h")) != -1) {
    switch (opt) {
    case 'o':
      disk_image_path = optarg;
      break;
    case 'k':
      kernel_path = optarg;
      break;
    case 's':
      image_size_in_sectors = atoi(optarg) * 2048;
      break;
    case 'a':
      arch = optarg;
      break;
    case 'h':
      usage(argv[0]);
      exit(0);
    }
  }

  if(disk_image_path == NULL) {
    fprintf(stderr, "No image path (-o) given\n");
    usage(argv[0]);
    exit(1);
  }


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

  if(kernel_path) {
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

    fat32_make_efi(arch, image_fd, fat32_first_sector,
                   fat32_last_sector - fat32_first_sector + 1,
                   kmem, st.st_size);
  }

  gpt_write_efi(image_fd, image_size_in_sectors,
                fat32_first_sector, fat32_last_sector);

  return 0;
}
