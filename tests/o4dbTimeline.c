// A movie played with memory changes (pokes) at frame ends, the fight's values printed when they change:
// o4dbTimeline GAMEDIR MOVIE POKES FROM   POKES: lines "FRAME OFFSET HEXWORD" (a word of the data segment, given at
// that frame's end before its input), FROM the first frame printed
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "movie.h"

typedef struct { uint64_t frame; uint16_t off, value; } Poke;
static Poke *pokes;
static size_t nPokes, nextPoke;
static uint64_t from;
static char last[512];

static uint16_t dw(uint16_t off) { const uint8_t *ds = o4db_memory() + ((uint32_t)o4db_data_segment() << 4); return (uint16_t)(ds[off] | ds[off + 1] << 8); }
static uint32_t dd(uint16_t off) { return dw(off) | (uint32_t)dw(off + 2) << 16; }

static void frame_hook(uint64_t frame, void *ctx)
{
  (void)ctx;
  uint8_t *ds = o4db_memory() + ((uint32_t)o4db_data_segment() << 4);
  while (nextPoke < nPokes && pokes[nextPoke].frame <= frame)
  {
    ds[pokes[nextPoke].off] = (uint8_t)pokes[nextPoke].value;
    ds[pokes[nextPoke].off + 1] = (uint8_t)(pokes[nextPoke].value >> 8);
    nextPoke++;
  }
  if (frame < from) return;
  if (getenv("POSITIONS"))
  {
    if (frame % 10 == 0)
      printf("%7llu clk %6u blue (%5d,%5d) step %3u anim %5u | red (%5d,%5d) step %3u\n", (unsigned long long)frame, dd(0xCF02), (int16_t)dw(0xBA18),
             (int16_t)dw(0xBA1C), dw(0xBA62), dw(0xBA6A), (int16_t)dw(0xBB0C), (int16_t)dw(0xBB10), dw(0xBB56));
    return;
  }
  char line[512];
  snprintf(line, sizeof line, "rnd %u clk %6u count %2u warn %u | red down %u st %2u hp %6d sta %6d end %6d kd %u | blue down %u hp %6d kd %u | tko %u/%u ko %u/%u over %u/%u res %d win %d",
           dw(0x9302), dd(0xCF02), dw(0xCF12), dw(0xCF14), dw(0xBB06 + 0xC), dw(0xBB06 + 0x2C), (int16_t)dw(0xBB06 + 0x10), (int16_t)dw(0xBB06 + 0xE),
           (int16_t)dw(0xBB06 + 0x12), dw(0xA4FC + 0xA), dw(0xBA12 + 0xC), (int16_t)dw(0xBA12 + 0x10), dw(0xA488 + 0xA), dw(0xA4F8), dw(0xA56C), dw(0xA4FA),
           dw(0xA56E), dw(0xCF0A), dw(0xCF0C), (int16_t)dw(0xB0D0), (int16_t)dw(0xD208));
  // (print when anything but the clock and the vitals changed, and every 70 frames)
  char key[512];
  snprintf(key, sizeof key, "%u %u %u %u %u %u %u %u %u %u %u %u", dw(0xCF12), dw(0xCF14), dw(0xBB06 + 0xC), dw(0xBB06 + 0x2C), dw(0xA4FC + 0xA), dw(0xBA12 + 0xC),
           dw(0xA4F8), dw(0xA56C), dw(0xA4FA), dw(0xA56E), dw(0xCF0A), dw(0xCF0C));
  if (strcmp(key, last) || frame % 70 == 0) printf("%7llu %s\n", (unsigned long long)frame, line);
  strcpy(last, key);
}

int main(int argc, char **argv)
{
  if (argc < 5) { fprintf(stderr, "usage: o4dbTimeline GAMEDIR MOVIE POKES FROM\n"); return 2; }
  size_t n;
  MovieInput *rows = movie_load(argv[2], &n);
  FILE *f = fopen(argv[3], "r");
  size_t cap = 1024;
  pokes = malloc(cap * sizeof *pokes);
  unsigned long long fr; unsigned off, val;
  while (f && fscanf(f, "%llu %x %x", &fr, &off, &val) == 3)
  {
    if (nPokes == cap) pokes = realloc(pokes, (cap *= 2) * sizeof *pokes);
    pokes[nPokes++] = (Poke){ fr, (uint16_t)off, (uint16_t)val };
  }
  if (f) fclose(f);
  from = strtoull(argv[4], NULL, 10);
  if (!o4db_setup(argv[1], "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  o4db_set_frame_hook(frame_hook, NULL);
  O4dbInput *in = malloc(n * sizeof *in);
  for (size_t k = 0; k < n; k++) in[k] = (O4dbInput){ rows[k].buttons, rows[k].mouseX, rows[k].mouseY };
  o4db_new(&in[0]);
  o4db_run(in + 1, n - 1);
  return 0;
}
