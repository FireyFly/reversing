#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

struct header {
  char magic[4];
  u16 unk1; // 0x10
  u16 nentries;
  u32 unk2; // 0
} __attribute__((packed));

struct entry {
  char path[64];
  u32 unk1;
  u32 size_compressed;
  u32 size_decompressed; // (x | 0x40000000)
  u32 offset;
} __attribute__((packed));

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

  // Header & entry table
  struct header hd;
  fread(&hd, sizeof(struct header), 1, f);
  assert(hd.unk1 == 0x10);
  assert(hd.unk2 == 0);

  struct entry *entries = malloc(sizeof(struct entry) * hd.nentries);
  fread(entries, sizeof(struct entry), hd.nentries, f);
//printf("\x1B[1m%3s %-64s %-8s %-8s %-8s %-8s\x1B[m\n",
//       "#", "path", "unk1", "comp", "decomp", "offset");
  for (size_t i = 0; i < hd.nentries; i++) {
    struct entry *ent = &entries[i];
 // printf("%3zu %-64s %08x %08x %08x %08x\n",
 //        i, ent->path, ent->unk1, ent->size_compressed, ent->size_decompressed, ent->offset);
    size_t size_decompressed = ent->size_decompressed & 0x3FFFFFFF;
 // if (size_decompressed < ent->size_compressed) printf("\x1B[7m");
    printf("%08x %3zu %-54s %8d %x:%8zu %08x %s\x1B[m\n",
           ent->unk1, i, ent->path, ent->size_compressed, ent->size_decompressed >> 28, size_decompressed, ent->offset, argv[1]);
  }

  return 0;
}
