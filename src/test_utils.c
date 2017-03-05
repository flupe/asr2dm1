#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "utils.h"

int main() {
  FILE *fd = fopen("./fat_cli", "r");

  printf("read_uint8: %x\n", read_uint8(fd));
  rewind(fd);

  printf("---\n");

  printf("read_uint16_littleendian: %x\n", read_uint16_littleendian(fd));
  rewind(fd);
  printf("read_uint16_bigendian: %x\n", read_uint16_bigendian(fd));
  rewind(fd);

  printf("---\n");

  printf("read_uint32_littleendian: %x\n", read_uint32_littleendian(fd));
  rewind(fd);
  printf("read_uint32_bigendian: %x\n", read_uint32_bigendian(fd));
  rewind(fd);

  return 0;
}
