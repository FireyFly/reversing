#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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


//-- LZSS decompression ---------------------------------------------
#define CONTEXT 0x1000
struct lzss_state {
  int i;
  int written;
  u8 history[CONTEXT];
};

void lzss_init(struct lzss_state *state) {
  state->i = 0;
  state->written = 0;
}

int lzss_decompress(struct lzss_state *state, FILE *in, FILE *out, int decompressed_size) {
  #define BIT(n, b) (((n) >> (b)) & 1)

  #define WRITE(x) {                                                    \
    state->history[state->i] = (x);            \
    fputc(state->history[state->i], out);      \
    state->written++;                          \
    state->i = (state->i + 1) & (CONTEXT - 1); \
  }
  #define HISTORY(X) (state->history[(state->i - ((X) + 1)) & (CONTEXT - 1)])

  while (state->written < decompressed_size && !feof(in)) {
    u8 flags = fgetc(in);
    for (int b = 7; b >= 0 && !feof(in) && state->written < decompressed_size; b--) {
      if (BIT(flags, b)) {
        // Copy from history
        u8 x = fgetc(in);
        u8 y = fgetc(in);

        int count = x >> 4;
        int offset = (x & 0x0F) << 8 | y;

        count += 3;

        for (int i = 0; i < count; i++) WRITE(HISTORY(offset));

      } else {
        // Copy input to output
        WRITE(fgetc(in));
      }
    }
  }

  return state->written == decompressed_size? 0 : -1;

  #undef CONTEXT
  #undef BIT
  #undef WRITE
}


//-- BPE decompression ----------------------------------------------
int bpe_decompress(FILE *in, FILE *out, int decompressed_size) {
  int written = 0;

  while (written < decompressed_size && !feof(in)) {
    printf("%d %d %d\n", state->written, decompressed_size, state->i);
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

    u8 stack[256];
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
        written++;
      } else {
        stack[stack_i++] = right[c];
        stack[stack_i++] = left[c];
      }
    }
  }

  return written == decompressed_size? 0 : -1;
}


//-- File format ----------------------------------------------------
struct gfac_header {
  char magic[4]; // GFAC
  u32 unk1;
  u32 unk2;
  u32 files_offset;
  u32 files_size;
  u32 blob_offset;
  u32 blob_size;
} __attribute__((packed));

struct file_entry {
  u32 checksum;
  u16 filename_offset;
  u16 unk1;
  u32 size;
  u32 offset;
} __attribute__((packed));

struct file {
  struct file_entry entry;
  const char *filename;
};

struct gfcp_header {
  char magic[4]; // GFCP
  u32 unk1;
  u32 compression_type;
  u32 decompressed_size;
  u32 compressed_size;
} __attribute__((packed));

enum operation {
  OP_INFO,
  OP_LIST,
  OP_EXTRACT,
};


void file_copy(FILE *to_f, FILE *from_f, size_t count) {
  #define SIZE 0x1000
  char buf[SIZE];
  size_t remaining = count;
  while (remaining > 0) {
    size_t n = fread(buf, 1, remaining < SIZE? remaining : SIZE, from_f);
    if (n == 0) break;
    fwrite(buf, 1, n, to_f);
    remaining -= n;
  }
  #undef SIZE
}


//-- Entry point ----------------------------------------------------
int read_operation(const char *s) {
  if (strcmp(s, "i") == 0 || strcmp(s, "info")    == 0) return OP_INFO;
  if (strcmp(s, "l") == 0 || strcmp(s, "list")    == 0) return OP_LIST;
  if (strcmp(s, "x") == 0 || strcmp(s, "extract") == 0) return OP_EXTRACT;
  return -1;
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <op> <filename>\n", argv[0]);
    return 1;
  }

  FILE *f = fopen(argv[2], "r");
  if (f == NULL) {
    fprintf(stderr, "Couldn't open '%s' for reading.\n", argv[2]);
    return 1;
  }

  // Read header
  struct gfac_header hd_gfac;
  fread(&hd_gfac, sizeof(struct gfac_header), 1, f);
  assert(strncmp(hd_gfac.magic, "GFAC", 4) == 0);

  // Read files section
  fseek(f, hd_gfac.files_offset, SEEK_SET);
  u32 count;
  fread(&count, sizeof(u32), 1, f);
  struct file *files = malloc(sizeof(struct file) * count);
  for (int i = 0; i < count; i++) {
    fread(&files[i].entry, sizeof(struct file_entry), 1, f);
  }
  char buf[4096];
  for (int i = 0; i < count; i++) {
    struct file_entry *ent = &files[i].entry;
    fseek(f, ent->filename_offset, SEEK_SET);
    char *p = buf;
    int ch;
    while (ch = fgetc(f), ch != '\0' && ch != EOF) {
      *p++ = ch;
    }
    *p++ = '\0';
    files[i].filename = strdup(buf);
  }

  // Perform the requested operation
  int operation = read_operation(argv[1]);

  struct gfcp_header hd_gfcp;
  switch (operation) {
    case OP_INFO:
      // Read GFCP section
      fseek(f, hd_gfac.blob_offset, SEEK_SET);
      fread(&hd_gfcp, sizeof(struct gfcp_header), 1, f);
      assert(strncmp(hd_gfcp.magic, "GFCP", 4) == 0);

      printf("GFAC (header):\n");
      printf("  Files offset=%08x size=%08x (%d files)\n",
             hd_gfac.files_offset, hd_gfac.files_size, count);
      printf("  Blob  offset=%08x size=%08x\n",
             hd_gfac.blob_offset, hd_gfac.blob_size);
      printf("\n");
      printf("GFCP (blob):\n");
      printf("  Unknown1= %8d  Compr. type= %7d\n",
             hd_gfcp.unk1, hd_gfcp.compression_type);
      printf("  Size= %12d  Compressed= %8d\n",
             hd_gfcp.decompressed_size, hd_gfcp.compressed_size);
      break;

    case OP_LIST:
      // Report files
      for (int i = 0; i < count; i++) {
        struct file_entry *ent = &files[i].entry;
        printf("%3d: %-40s %08x %04x %8d %08x\n", i,
               files[i].filename, ent->checksum, ent->unk1, ent->size, ent->offset);
      }
      break;

    case OP_EXTRACT:
      // Read GFCP section
      fseek(f, hd_gfac.blob_offset, SEEK_SET);
      fread(&hd_gfcp, sizeof(struct gfcp_header), 1, f);
      assert(strncmp(hd_gfcp.magic, "GFCP", 4) == 0);
      assert(hd_gfcp.unk1 == 1);

      // Decompress blob
      FILE *tmp_f = tmpfile();

      int n;
      switch (hd_gfcp.compression_type) {
        case 1:
          n = bpe_decompress(f, tmp_f, hd_gfcp.decompressed_size);
          break;

        case 3: {
          struct lzss_state *lzss = malloc(sizeof(struct lzss_state));
          lzss_init(lzss);
          n = lzss_decompress(lzss, f, tmp_f, hd_gfcp.decompressed_size);
          free(lzss);
        } break;

        default:
          fprintf(stderr, "Unimplemented compression type! %d\n", hd_gfcp.compression_type);
          return 4;
      }
      if (n < 0) {
        fprintf(stderr, "Error while decompressing!\n");
        return 3;
      }
      fflush(tmp_f);

      // Cut up into files and extract
      fseek(tmp_f, 0L, SEEK_SET);
      for (int i = 0; i < count; i++) {
        struct file_entry *ent = &files[i].entry;
        printf("Extracting '%s'...\n", files[i].filename);
        fseek(tmp_f, ent->offset - hd_gfac.blob_offset, SEEK_SET);
        FILE *out_f = fopen(files[i].filename, "wb");
        file_copy(out_f, tmp_f, ent->size);
        fclose(out_f);
      }
      fclose(tmp_f);
      break;

    default:
      fprintf(stderr, "Unimplemented operation: '%s'\n", argv[1]);
      return 2;
  }

  return 0;
}
