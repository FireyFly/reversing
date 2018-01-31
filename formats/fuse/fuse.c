#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#include <util/hexdump.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define FMT_END "\x1B[m"

int decompress_file(char *in, char *out, size_t filesize) {
  u32 compressed_size = 0;
  compressed_size = compressed_size << 4 | *in++;
  compressed_size = compressed_size << 4 | *in++;
  compressed_size = compressed_size << 4 | *in++;
  compressed_size = compressed_size << 4 | *in++;
  compressed_size = htonl(compressed_size);

  if (compressed_size >> 28 != 0x4) return -1;
  compressed_size &= 0x3FFFFFFF;

  #define SIZE 0x10000

  u8 *buf = malloc(SIZE);
  int buf_i = 0,
      written = 0;

  #define READ() ((unsigned char) (*in++))
  #define WRITE(X) {             \
    buf[buf_i] = (X);            \
    *out++ = buf[buf_i];         \
    buf_i = (buf_i + 1) % SIZE;  \
    written++;                   \
  }
  #define BUF(X) (buf[(buf_i - (X)) & (SIZE - 1)])

  int ch;
  while (ch = READ(), written < filesize) {
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
    for (int i = 0; i < extra_bytes; i++) v = (v << 8) | READ();

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
        if (v < 0xFD) to_copy = 4 * ((v & 0x1F) + 1);
        else to_copy = v - 0xFD + 1;
        then_copy = 0;
        offset    = 0;
        break;
    }

    printf("[%08x] [x=%2d y=%2d z=%2d]", v, to_copy, then_copy, offset);
    for (int i = 0; i < to_copy; i++) printf(" %s%02x%s", hexdump_format_of(in[i]), (unsigned char) in[i], FMT_END);

    // Copy
    for (int i = 0; i < to_copy; i++) WRITE(READ());

    // Copy from history
    for (int i = 0; i < then_copy; i++) WRITE(BUF(offset));

    printf(" │");
    for (int i = 0; i < then_copy; i++) {
      unsigned char v = BUF(offset - then_copy + i);
      printf(" %s%02x%s", hexdump_format_of(v), v, FMT_END);
    }
    printf("\n");
  }

  free(buf);
  return written;

  #undef BUF
  #undef WRITE
  #undef SIZE
}

//-- Decompression --------------------------------------------------
int copy(FILE *in, FILE *out, size_t nbytes) {
  // FIXME: Super inefficient, but it works for the time being...
  for (size_t i = 0; i < nbytes; i++) fputc(fgetc(in), out);
  return nbytes;
}

int decompress(FILE *in, FILE *out, size_t filesize) {
  uint32_t compressed_size;
  fread(&compressed_size, sizeof(u32), 1, in);
  if (compressed_size >> 28 != 0x4) {
    fseek(in, -sizeof(u32), SEEK_CUR);
    return copy(in, out, filesize);
  }
  compressed_size &= 0x3FFFFFFF;

  #define SIZE 0x10000

  u8 *buf = malloc(SIZE);
  int buf_i = 0,
      written = 0;

  #define WRITE(X) {             \
    buf[buf_i] = (X);            \
    fputc(buf[buf_i], out);      \
    buf_i = (buf_i + 1) % SIZE;  \
    written++;                   \
  }
  #define BUF(X) (buf[(buf_i - (X)) & (SIZE - 1)])

  long start = ftell(in);

  int ch;
  while (ch = fgetc(in), ftell(in) - start < compressed_size) {
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
    for (int i = 0; i < extra_bytes; i++) v = (v << 8) | fgetc(in);

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
        if (v < 0xFD) to_copy = 4 * ((v & 0x1F) + 1);
        else to_copy = v - 0xFD + 1;
        then_copy = 0;
        offset    = 0;
        break;
    }

 // printf("\x1B[1;7m[%08lx %08x - %d %d %d]\x1B[m", ftell(in), v,
 //        to_copy, then_copy, offset);

    // Copy
    for (int i = 0; i < to_copy; i++) WRITE(fgetc(in));

    // Copy from history
    for (int i = 0; i < then_copy; i++) WRITE(BUF(offset));
  }

  free(buf);
  return 0;

  #undef BUF
  #undef WRITE
  #undef SIZE
}


//-- File format ----------------------------------------------------
struct fuse_header {
  char magic[8];
  u32 count;
  u32 zero1;
  u32 filesize;
} __attribute__((packed));

struct csv_entry {
  char *path;
  u32 offset;
  u32 filesize;
  u32 checksum;
};

typedef void (*file_entry_callback_t)(struct csv_entry *entry, void *any);

int for_each_file(FILE *f, file_entry_callback_t callback, void *any) {
  char buf[BUFSIZ];

  struct csv_entry entry;

  bool new_mode = false;

  while (fgets(buf, BUFSIZ, f) != NULL) {
    // Skip first line in newer CSV's
    printf("'%s'\n", buf);

    char *fields[10];
    int nfields = 1;
    fields[0] = buf;
    char *p = buf - 1;
    while (p = strchr(p + 1, ','), p != NULL) {
      fields[nfields++] = p + 1;
      *p = '\0';
    }

    if (nfields != 4 || fields[1] - fields[0] != 9) {
      fprintf(stderr, "Error reading CSV file.\n");
      return 3;
    }

    sscanf(fields[0], "%08X", &entry.checksum);
    sscanf(fields[2], "%d",   &entry.filesize);
    sscanf(fields[3], "%08x", &entry.offset);
    entry.path = fields[1];

    callback(&entry, any);
  }

  return 0;
}


//-- Operations -----------------------------------------------------
void print_entry_callback(struct csv_entry *entry, void *any) {
  printf("%08X  %08x %8d %s\n",
         entry->checksum, entry->offset, entry->filesize, entry->path);
}

void debug_print_callback(struct csv_entry *entry, void *any) {
  static int i = 0;
  static int offset = 0;

  printf("%5d (%08X) [$%08x] +$%8x %s", i++, entry->checksum, entry->offset,
                                             entry->filesize, entry->path);
  if (offset != entry->offset) {
    printf(" \x1B[31m[..$%08x Δ=%dB]\x1B[m", entry->offset, entry->offset - offset);
  }
  printf("\n");
  offset = entry->offset + entry->filesize;

  /*
  static int i = 0;
  FILE *fuse_f = any;
  printf("\x1B[2J\x1B[H");
  printf("[%4d] %08X  %08x %8d\n%s\n\n", i++,
         entry->checksum, entry->offset, entry->filesize, entry->path);

  #define min(x,y) ((x) < (y)? (x) : (y))
  fseek(fuse_f, entry->offset, SEEK_SET);
  char buf[0x100];
  size_t n = fread(buf, 1, min(0x100, entry->filesize), fuse_f);
  hexdump(buf, n);
  printf("\n");
  char buf2[0x100];
  decompress_file(buf, buf2, n);
  hexdump(buf2, n);
  */

//switch (getchar()) {
//  case 'q': exit(0); /* quit */
//  default: break; /* (pass) */
//}

//exit(0);
}

struct entry_data {
  FILE *f;
  char *path;
};

void read_entry_callback(struct csv_entry *entry, void *any) {
  struct entry_data *data = any;
  if (strcmp(data->path, entry->path) == 0) {
    fseek(data->f, entry->offset, SEEK_SET);
    if (decompress(data->f, stdout, entry->filesize) < 0) {
      fprintf(stderr, "Error while decompressing!\n");
      exit(2);
    }
  }
}

void extract_file_callback(struct csv_entry *entry, void *any) {
  FILE *fuse_f = any;

  static char buf[0x1000];
  strcpy(buf, ".");

  struct stat statbuf;

  char *p = strtok(entry->path, "/");
  while (p != NULL) {
    char *p2 = strtok(NULL, "/");

    strcat(buf, "/");
    strcat(buf, p);

    if (p2 == NULL) {
      // File--extract
      FILE *f = fopen(buf, "wb");
      fseek(fuse_f, entry->offset, SEEK_SET);
      int err;
      if (err = decompress(fuse_f, f, entry->filesize), err < 0) {
        fprintf(stderr, "Error while decompressing: %d %s\n", err, buf);
        return;
      }
      fclose(f);

    } else {
      // Directory--mkdir if not present
      if (stat(buf, &statbuf) != 0 || !S_ISDIR(statbuf.st_mode)) {
        if (mkdir(buf, 0755) != 0) {
          fprintf(stderr, "Error creating directory '%s'\n", buf);
          return;
        }
      }
    }

    p = p2;
  }

  /*
  printf("%08X  %08x %8d %s\n",
         entry->checksum, entry->offset, entry->filesize, entry->path);
  */
}


//-- Entry point ----------------------------------------------------
enum operation {
  OP_LIST,
  OP_READ,
  OP_EXTRACT,
  OP_DEBUG,
};

int read_operation(const char *s) {
  if (strcmp(s, "l") == 0 || strcmp(s, "list")    == 0) return OP_LIST;
  if (strcmp(s, "r") == 0 || strcmp(s, "read")    == 0) return OP_READ;
  if (strcmp(s, "x") == 0 || strcmp(s, "extract") == 0) return OP_EXTRACT;
  if (strcmp(s, "d") == 0 || strcmp(s, "debug")   == 0) return OP_DEBUG;
  return -1;
}

int main(int argc, char *argv[]) {
  if (argc < 4) {
    fprintf(stderr, "Usage: %s <op> <csv-file> <fib-file> [...]\n", argv[0]);
    return 1;
  }

  FILE *f = fopen(argv[2], "r");
  if (f == NULL) {
    fprintf(stderr, "Couldn't open '%s' for reading.\n", argv[2]);
    return 1;
  }

  FILE *fuse_f = fopen(argv[3], "r");
  if (fuse_f == NULL) {
    fprintf(stderr, "Couldn't open '%s' for reading.\n", argv[3]);
    return 1;
  }

  // Process input files
  struct fuse_header header;
  fread(&header, sizeof(struct fuse_header), 1, fuse_f);
  if (strncmp(header.magic, "FUSE1.00", 8) != 0) {
    fprintf(stderr, "Bad magic in FUSE file.\n");
    return 1;
  }

  int err;

  int operation = read_operation(argv[1]);
  switch (operation) {
    case OP_LIST:
      if (err = for_each_file(f, print_entry_callback, NULL), err != 0) {
        return err;
      }
      break;

    case OP_READ: {
      struct entry_data data = { fuse_f, argv[4] };
      if (err = for_each_file(f, read_entry_callback, &data), err != 0) {
        return err;
      }
    } break;

    case OP_EXTRACT:
      if (err = for_each_file(f, extract_file_callback, fuse_f), err != 0) {
        return err;
      }
      break;

    case OP_DEBUG:
      if (err = for_each_file(f, debug_print_callback, fuse_f), err != 0) {
        return err;
      }
      break;

    default:
      fprintf(stderr, "No such operation: '%s'\n", argv[1]);
      return 1;
  }

  return 0;
}
