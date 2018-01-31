#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

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


int decompress(FILE *in, FILE *out) {
  while (!feof(in)) {
    u8 left[256], right[256];
    for (int i = 0; i < 256; i++) left[i] = i;

    // Read the table of pairs
    int count;
    int c = 0;
    while (count = fgetc(in), count != EOF) {
      if (count >= 0x80) {
        c += count - 0x80 + 1;
        count = 0;
      }
      if (c == 0x100) break;

      for (int i = 0; i <= count; i++) {
        left[c] = fgetc(in);
        if (c != left[c]) right[c] = fgetc(in);
        c++;
      }

      if (c == 0x100) break;
    }

    // Read size & decompress
    u16 size = fgetc(in);
    size = size << 8 | fgetc(in);

    u8 stack[30];
    int stack_i = 0;

    for (int i = 0; ; ) {
      if (stack_i > 0) {
        c = stack[--stack_i];
      } else {
        if (i++ == size) break;
        c = fgetc(in);
      }

      if (c == left[c]) {
        fputc(c, out);
      } else {
        stack[stack_i++] = right[c];
        stack[stack_i++] = left[c];
      }
    }
  }

  return 0;
}


int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <file>\n", argv[0]);
    return 1;
  }

  FILE *f = fopen(argv[1], "r");
  decompress(f, stdout);

  return 0;
}
