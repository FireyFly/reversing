#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

struct header {
  u32 flags;
  u32 compressed_size;
  char magic[4];
  u32 decompressed_size;
} __attribute__((packed));

int decompress(FILE *in, FILE *out, int compressed_size, int decompressed_size) {
  #define SIZE 4096
  static char buf[SIZE];
  int buf_i = 0;
  int read = 0, written = 0;
  int ch;

  #define READ() (read++, fgetc(in))
  #define WRITE(x) { \
    int x_ = (x); \
    buf_i = (buf_i + 1) & (SIZE - 1); \
    buf[buf_i] = (x_); \
    fputc(x_, out); \
    written++; \
  }
  #define BUF(d) buf[(buf_i + (d)) & (SIZE - 1)]

  while (ch = READ(), ch != EOF /*&& read < compressed_size && written < decompressed_size*/) {
    int x = ch & 0x1F, y;
    if (x == 0) {
      x = READ();
      x = READ() << 8 | x;
    }
    switch ((ch >> 4) & 0xE) {
      case 0x0: // Copy literal
        for (int i = 0; i < x; i++) WRITE(READ());
        break;

      case 0x2: // Write nulls
        for (int i = 0; i < x; i++) WRITE(0x00);
        break;

      case 0x4: // Repeat next byte
        y = READ();
        for (int i = 0; i < x; i++) WRITE(y);
        break;

      case 0x6: // Copy at offset
        y = READ();
        for (int i = 0; i < x; i++) WRITE(BUF(-y + 1));
        break;

      case 0x8: // Copy at big offset
        y = READ();
        y = READ() << 8 | y;
        for (int i = 0; i < x; i++) WRITE(BUF(-y + 1));
        break;

      case 0xA: // Copy literal bytes interspersed with nulls
        for (int i = 0; i < x; i++) { WRITE(READ()); WRITE(0x00); }
        break;

      default:
        if (ch == 0xFF) return 0; // End-of-stream
        return -1;
    }
  }

  if (ftell(in) != compressed_size) return -2;
  return 0;
}

int cat(FILE *in, FILE *out) {
  static char buf[0x1000];
  int n;
  while (n = fread(buf, 1, 0x1000, in), n > 0) {
    fwrite(buf, 1, n, out);
  }
  return ferror(out);
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <file>\n", argv[0]);
    return 1;
  }

  FILE *f = fopen(argv[1], "r");
  if (f == NULL) {
    fprintf(stderr, "Couldn't open '%s' for reading.\n", argv[1]);
    return 1;
  }

  static char name[0x1000];

  // Loop over each chunk
  struct header hd;
  int i = 0;
  while (!feof(f)) {
    // Filename for this chunk
    i++;
    sprintf(name, "%s.%03d.%s.bin", argv[1], i, hd.magic);
    fprintf(stderr, "[%08lx] ", ftell(f));
    // Read chunk header
    int n = fread(&hd, sizeof(struct header), 1, f);

    fprintf(stderr, "magic=%c%c%c%c flags=%08x compressed=$%06x decompressed=%06x %s\n",
                    hd.magic[0], hd.magic[1], hd.magic[2], hd.magic[3],
                    hd.flags, hd.compressed_size, hd.decompressed_size, argv[1]);
    if (strcmp(hd.magic, "") == 0 || hd.compressed_size == 0) {
      // Bad chunk--this shouldn't happen!
      fprintf(stderr, "Bad chunk near $%04lx!\n", ftell(f));
      return 1;
    }
    if (hd.decompressed_size == 0) {
      // End-of-chunks marker
      if (strncmp(hd.magic, "END0", 4) == 0) {
        fprintf(stderr, "END0 found.\n");
        break;
      }

      // Uncompressed chunk
      fprintf(stderr, "Copying '%s' verbatim -- not compressed?\n", name);
      FILE *out_f = fopen(name, "w");
      if (out_f == NULL) {
        fprintf(stderr, "Couldn't open '%s' for writing.\n", name);
        return 1;
      }
      cat(f, out_f);
    } else {
      fprintf(stderr, "Decompressing '%s'...\n", name);

      // Decompress chunk
      FILE *out_f = fopen(name, "w");
      if (out_f == NULL) {
        fprintf(stderr, "Couldn't open '%s' for writing.\n", name);
        return 1;
      }
      int err = decompress(f, out_f, hd.compressed_size, hd.decompressed_size);
      if (err) {
        fprintf(stderr, "[%d] Error while decompressing @ $%04lx! (output @ $%04lx)\n",
                        err, ftell(f) - 1, ftell(out_f));
        return 2;
      }
      fclose(out_f);
    }

    // Seek to next block--round up to multiple of 0x20
    long offset = ftell(f);
    fseek(f, offset - (offset % 0x40) + 0x40, SEEK_SET);
  }

  return 0;
}
