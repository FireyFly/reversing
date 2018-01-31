#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <util/hexdump.h>
#define FMT_END "\x1B[m"

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

struct header {
  u8 magic[4];
  u32 decompressed_size;
  u32 filesize;
} __attribute__((packed));

void decompress(FILE *in, FILE *out, size_t compressed_size, size_t decompressed_size) {
  #define HISTSIZE 0x1000
  static u8 hist[HISTSIZE];
  size_t hist_i = 0;

  #define READ() fgetc(in)
  #define WRITE(x) { \
    hist[hist_i] = (x); \
    fputc(hist[hist_i], out); \
    hist_i = (hist_i + 1) & (HISTSIZE - 1); \
  }

  #define HIST(d) hist[(hist_i - (d)) & (HISTSIZE - 1)]

  size_t start = ftell(in);
  while (start + ftell(in) < compressed_size && !feof(in)) {
    u8 op = READ();
    u8 v;
    size_t count, dist;

    switch (op >> 5) {
      case 0: // Copy verbatim
        count = op;
        for (size_t i = 0; i < count; i++) WRITE(READ());
        break;

      case 1: // Copy verbatim
        count = (op & 0x1F) << 8 | READ();
        for (size_t i = 0; i < count; i++) WRITE(READ());
        break;

      case 2: // Repeat byte
        count = op & 0x0F;
        if (op & 0x10) count = count << 8 | READ();
        count += 4;
        v = READ();
        for (size_t i = 0; i < count; i++) WRITE(v);
        break;

      case 3: // Copy last byte
        count = op & 0x1F;
        for (size_t i = 0; i < count; i++) WRITE(HIST(1));
        break;

      case 4: case 5: case 6: case 7: // Copy from history
        count = ((op >> 5) & 0x3) + 4;
        dist = (op & 0x1F) << 8 | READ();
        for (size_t i = 0; i < count; i++) WRITE(HIST(dist));
        break;
    }
  }

}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
    return 1;
  }

  FILE *f = fopen(argv[1], "r");
  if (f == NULL) {
    fprintf(stderr, "Couldn't open '%s' for reading.", argv[1]);
    return 1;
  }

  struct header hd;
  fread(&hd, sizeof(struct header), 1, f);
  assert(hd.magic[0] == 0xFC && hd.magic[1] == 0xAA &&
         hd.magic[2] == 0x55 && hd.magic[3] == 0xA7);

  decompress(f, stdout, hd.filesize - sizeof(struct header), hd.decompressed_size);
  return 0;
}
