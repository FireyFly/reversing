#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;


#define FMT_END "\x1B[m"

/** SGR string to format the byte `v`. */
const char *format_of(u32 v) {
  return v ==  0x00? "\x1B[38;5;238m"
       : v ==  0xFF? "\x1B[38;5;167m"
       : v <   0x20? "\x1B[38;5;150m"
       : v >=  0x7F? "\x1B[38;5;141m"
       :             FMT_END;
}

/** Hexdump `n` bytes at `p`. */
void hexdump(void *p_, int n) {
  u8 *p = p_;
  int cols = 16, i;

  for (i = 0; i < n; i += cols) {
    // Offset
    printf("    %04x ", i);

    // Read line
    int line[cols];
    for (int j = 0; j < cols; j++) {
      line[j] = i + j < n? *p++ : -1;
    }

    // Print hex area
    for (int j = 0; j < cols; j++) {
      int v = line[j];
      if (v >= 0) printf(" %s%02x" FMT_END, format_of(v), v);
      else printf("   ");
      if (j % 8 == 7) printf(" ");
    }
    printf(" ");

    // Print character area
    for (int j = 0; j < cols; j++) {
      int v = line[j];
      if (v >= 0) printf("%s%c" FMT_END, format_of(v), isprint(v)? v : '.');
      else printf(" ");
      if (j % 8 == 7) putchar(" \n"[j == cols - 1]);
    }
  }

  if (i % cols != 0) putchar('\n');
}


int decompress(FILE *in, FILE *out, size_t compressed_size) {
  #define SIZE 0x10000

  u8 *buf = malloc(SIZE);
  int i = 0,
      written = 0;

  #define WRITE(X) {        \
    buf[i] = (X);           \
    fputc(buf[i], stdout);  \
    i = (i + 1) % SIZE;     \
    written++;              \
  }
  #define BUF(X) (buf[(i - (X)) & (SIZE - 1)])

  long start = ftell(in);

  int ch;
  while (ch = fgetc(in), ch != EOF && ftell(in) - start < compressed_size) {
    // copy `to_copy` bytes from file, then `then_copy` bytes from history with offset `offset`
    int to_copy,
        then_copy,
        offset;

    int type = ch >> 5; // type of compression operation
    int extra_bytes = type < 4? 1
                    : type < 6? 2
                    : type < 7? 3
                    :           0;

    u32 v = ch;
    for (int j = 0; j < extra_bytes; j++) v = (v << 8) | fgetc(in);

    switch (ch >> 5) {
      case 0: case 1: case 2: case 3:  // 0yyyxxzz zzzzzzzz
        to_copy   = (v >> 10) & 0x3;
        then_copy = (v >> 12) + 3;
        offset    = (v & 0x3FF) + 1;
        break;

      case 4: case 5:                  // 10yyyyyy xxzzzzzz zzzzzzzz
        to_copy   = (v >> 14) & 0x3;
        then_copy = ((v >> 16) & 0x3F) + 4;
        offset    = (v & 0x3FFF) + 1;
        break;

      case 6:                          // 110xxyyy zzzzzzzz zzzzzzzz yyyyyyyy
        to_copy   = (v >> 27) & 0x3;
        then_copy = (((v >> 24) & 0x7) << 7) + (v & 0xFF) + 5;
        offset    = ((v >> 8) & 0xFFFF) + 1;
        break;

      case 7:                          // 111xxxxx
        to_copy   = 4 * ((v & 0x1F) + 1);
        then_copy = 0;
        offset    = 0;
        break;
    }

    /*/

    // $E0, $E1, $E2, $E3, ...
    if (ch >= 0xE0) {
      to_copy = 4 * (ch - 0xE0 + 1);
      then_copy = 0;
      offset = 0;

    // $80, $81, $82, $83 ...
    } else if (ch >= 0x80 && ch < 0xC0) {
      then_copy = ch - 0x80 + 4;
      int x = fgetc(in);
      x = (x << 8) | fgetc(in);
      to_copy = x >> 14;
      offset = (x & 0x3FFF) + 1;

    } else if (0xC0 <= ch) {
      to_copy = (ch - 0xC0) >> 3;
      int multiplier = ch & 0x07;
      int x = fgetc(in);
      x = (x << 8) | fgetc(in);
      offset = x + 1;
      then_copy = fgetc(in) + 0x80 * multiplier + 5;

    // $0C, $10, $14, ...
    } else if (ch < 0x80) {
      int x = (ch << 8) | fgetc(in);
      // xxxxyyzz zzzzzzzz
      to_copy   = (x >> 10) & 0x3;
      then_copy = (x >> 12) + 3;
      offset    = (x & 0x3FF) + 1;

    // ???
    } else {
      fseek(in, -1L, SEEK_CUR);
      goto error;
    }
    //*/

 // printf("\x1B[1;7m[%d %d %d]\x1B[m", to_copy, then_copy, offset);

    // Copy
    for (int j = 0; j < to_copy; j++) WRITE(fgetc(in));

    // Copy from history
    for (int j = 0; j < then_copy; j++) WRITE(BUF(offset));
  }

  free(buf);
  return 0;

  error:
  #define ERR_CONTEXT 0x10
  fprintf(stderr, "Error near $%04lx:\n ", ftell(in));
  fread(buf, 1, ERR_CONTEXT, in);
  for (int i = 0; i < ERR_CONTEXT; i++) {
    fprintf(stderr, " %s%02x%s", format_of(buf[i]), buf[i], FMT_END);
  }
  fprintf(stderr, "\n ");
  for (int i = 0; i < ERR_CONTEXT; i++) {
    fprintf(stderr, " %s%2c%s", format_of(buf[i]), isprint(buf[i])? buf[i] : '.', FMT_END);
  }
  fprintf(stderr, "\n");

  free(buf);
  return -1;

  #undef BUF
  #undef WRITE
  #undef SIZE
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
    return 1;
  }

  FILE *f = fopen(argv[1], "r");
  if (f == NULL) {
    fprintf(stderr, "Couldn't open '%s' for reading.\n", argv[1]);
    return 1;
  }

  uint32_t compressed_size;
  fread(&compressed_size, sizeof(uint32_t), 1, f);
  compressed_size &= 0x3FFFFFFF;
  if (decompress(f, stdout, compressed_size) < 0) {
    return 2;
  }

  return 0;
}
