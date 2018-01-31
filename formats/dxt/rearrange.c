#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

//-- From https://fgiesen.wordpress.com/2009/12/13/decoding-morton-codes/
// "Insert" a 0 bit after each of the 16 low bits of x
u32 Part1By1(u32 x) {
  x &= 0x0000ffff;                  // x = ---- ---- ---- ---- fedc ba98 7654 3210
  x = (x ^ (x <<  8)) & 0x00ff00ff; // x = ---- ---- fedc ba98 ---- ---- 7654 3210
  x = (x ^ (x <<  4)) & 0x0f0f0f0f; // x = ---- fedc ---- ba98 ---- 7654 ---- 3210
  x = (x ^ (x <<  2)) & 0x33333333; // x = --fe --dc --ba --98 --76 --54 --32 --10
  x = (x ^ (x <<  1)) & 0x55555555; // x = -f-e -d-c -b-a -9-8 -7-6 -5-4 -3-2 -1-0
  return x;
}
u32 EncodeMorton2(u32 x, u32 y) {
  return (Part1By1(y) << 1) + Part1By1(x);
}

// Inverse of Part1By1 - "delete" all odd-indexed bits
u32 Compact1By1(u32 x) {
  x &= 0x55555555;                  // x = -f-e -d-c -b-a -9-8 -7-6 -5-4 -3-2 -1-0
  x = (x ^ (x >>  1)) & 0x33333333; // x = --fe --dc --ba --98 --76 --54 --32 --10
  x = (x ^ (x >>  2)) & 0x0f0f0f0f; // x = ---- fedc ---- ba98 ---- 7654 ---- 3210
  x = (x ^ (x >>  4)) & 0x00ff00ff; // x = ---- ---- fedc ba98 ---- ---- 7654 3210
  x = (x ^ (x >>  8)) & 0x0000ffff; // x = ---- ---- ---- ---- fedc ba98 7654 3210
  return x;
}
u32 DecodeMorton2X(u32 code) {
  return Compact1By1(code >> 0);
}
u32 DecodeMorton2Y(u32 code) {
  return Compact1By1(code >> 1);
}
//--

size_t swap(size_t v, size_t i, size_t j) {
  return (v & ~(1 << i) & ~(1 << j)) |
         ((v >> i) & 1) << j |
         ((v >> j) & 1) << i;
}

int main(int argc, char *argv[]) {
  FILE *f = fopen(argv[1], "r");

  char magic[3];
  fread(magic, 1, 3, f);
  assert(strncmp(magic, "P7\n", 3) == 0);

  size_t width, height, depth, maxval;
  char tupltype[0x100];
  fscanf(f, "WIDTH %zu\nHEIGHT %zu\nDEPTH %zu\nMAXVAL %zu\nTUPLTYPE %s\nENDHDR\n",
            &width, &height, &depth, &maxval, tupltype);
  assert(strncmp(tupltype, "RGB_ALPHA", strlen("RGB_ALPHA")) == 0);

  size_t max = width > height? width : height;
  fprintf(stderr, "%zu×%zu\n", width, height);

  u8 *data = malloc(4 * width * height);
  for (size_t i = 0; i < width * height; i++) {
    size_t min = width < height? width : height;
    size_t k = log2(min);

    size_t x, y;
    if (height < width) {
      // XXXyxyxyx → XXXxxxyyy
      size_t j = i >> (2*k) << (2*k)
               | (DecodeMorton2Y(i) & (min - 1)) << k
               | (DecodeMorton2X(i) & (min - 1)) << 0;
      x = j / height;
      y = j % height;
    } else {
      // YYYyxyxyx → YYYyyyxxx
      size_t j = i >> (2*k) << (2*k)
               | (DecodeMorton2X(i) & (min - 1)) << k
               | (DecodeMorton2Y(i) & (min - 1)) << 0;
      x = j % width;
      y = j / width;
    }

    u8 color[4];
    fread(color, 1, 4, f);
    memcpy(data + (y*width + x)*4, color, 4);
  }

  printf("P7\n");
  printf("WIDTH %zu\nHEIGHT %zu\nDEPTH %zu\nMAXVAL %zu\nTUPLTYPE %s\nENDHDR\n",
         width, height, depth, maxval, tupltype);
  fwrite(data, 1, width * height * 4, stdout);
  return 0;
}
