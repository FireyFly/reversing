//#pragma pack(1)

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

size_t decompress(char *out, char *in, size_t compressed_size, size_t decompressed_size) {
  #define CONTEXT 0x4000
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

  // Read trailer
  struct trailer tr;
  fseek(f, -sizeof(struct trailer), SEEK_END);
  fread(&tr, sizeof(struct trailer), 1, f);
  assert(8 <= tr.trailer_size && tr.trailer_size < 12);

  // Check filesize
  size_t filesize = ftell(f);
  fseek(f, 0L, SEEK_SET);

  // Read entire file
  char *data = malloc(filesize + tr.decompressed_increase);
  if (fread(data, filesize, 1, f) != 1) perror("fread"), exit(2);

  // Decompress
  decompress(data + filesize + tr.decompressed_increase - 1,
             data + filesize - tr.trailer_size - 1,
             tr.compressed_size,
             tr.compressed_size + tr.decompressed_increase);

  // Write
  fwrite(data, sizeof(char), filesize + tr.decompressed_increase, stdout);
  return 0;
}
