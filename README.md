# Generate a UEFI Bootable image from a Linux kernel image

No external dependencies other than zlib (for crc32)

No need to be root (losetup, etc)

Kernel must be compiled with `CONFIG_EFI=y` and `CONFIG_EFI_STUB=y`

Build this tool: `make`

Usage: `./efimaker disk.img path/to/bzImage`

The resuling image can be booted by a UEFI BIOS, qemu or Azure hyper-V v2.

