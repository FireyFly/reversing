#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

size_t decompress(char *out, char *in, size_t compressed_size, size_t decompressed_size) {
  #define CONTEXT 0x1000
  #define BIT(n, b) (((n) >> (b)) & 1)

  u8 history[CONTEXT];
  size_t i       = 0,
         read    = 0,
         written = 0;

  #define READ() (read++, *in--)
  #define WRITE(x) {             \
    history[i] = (x);            \
    *out-- = history[i];         \
    written++;                   \
    i = (i + 1) & (CONTEXT - 1); \
  }

  while (written < decompressed_size) {
    u8 flags = READ();
    for (int b = 7; b >= 0 && written < decompressed_size; b--) {
      if (BIT(flags, b)) {
        // Copy from history
        u8 x = READ();
        size_t count = (x >> 4) + 3;
        size_t offset = (((x & 0x0F) << 8) | (u8) READ()) + 3;

        for (int j = 0; j < count; j++) {
          WRITE(history[(i - offset) & (CONTEXT - 1)]);
        }

      } else {
        // Copy input to output
        WRITE(READ());
      }
    }
  }

  return written;

  #undef CONTEXT
  #undef BIT
  #undef READ
  #undef WRITE
}

struct header {
  char magic[4];
  u32 compressed_size;
  u32 header_size;
  u32 unk1;
  u32 decompressed_size;
  u32 unk2;
  u32 unk3;
  u32 unk4;
} __attribute__((packed));

struct trailer {
  u32 compressed_size : 24;
  u32 trailer_size    : 8;
  u32 decompressed_increase;
} __attribute__((packed));

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <file>  -- decompress <file> to stdout\n", argv[0]);
    return 1;
  }

  FILE *f = fopen(argv[1], "r");
  if (f == NULL) {
    fprintf(stderr, "Couldn't open '%s' for reading.\n", argv[1]);
    return 1;
  }

  // Read header, verify magic & static fields
  struct header hd;
  fread(&hd, sizeof(struct header), 1, f);
  if (strncmp(hd.magic, "ACMP", 4) != 0) {
    fprintf(stderr, "Bad magic number.\n");
    return 2;
  }
  assert(hd.unk1 == 0x00000000);
  assert(hd.unk2 == 0x01234567);
  assert(hd.unk3 == 0x01234567);
  assert(hd.unk4 == 0x01234567);
  fseek(f, sizeof(struct header), SEEK_SET);

  // Read rest of file & verify trailer
  char *data = malloc(hd.decompressed_size);
  size_t n = fread(data, hd.compressed_size, 1, f);
  if (n == 0 && ferror(f)) perror("fread"), exit(2);
  struct trailer tr = *((struct trailer *) (data + hd.compressed_size - sizeof(struct trailer)));
  assert(8 <= tr.trailer_size && tr.trailer_size < 12);

  // Decompress
  decompress(data + hd.decompressed_size - 1,
             data + hd.compressed_size - tr.trailer_size - 1,
             tr.compressed_size,
             tr.compressed_size + tr.decompressed_increase);

  // Write
  fwrite(data, sizeof(char), hd.decompressed_size, stdout);
  return 0;
}
