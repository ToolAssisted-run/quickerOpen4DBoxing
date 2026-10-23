// The machine plays a movie and, from a frame on, counts the frame ends at which each byte of the memory differs from
// the frame end before, and the 256-byte pages that ever differ from the first one compared: o4dbChanges GAMEDIR
// MOVIE FROM [SOUND] [CLOCK]   (the environment's UNSAVED: "START-END,..." ranges left out of the pages' counts, as the
// emulator's unsaved memory)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "movie.h"

#define SIZE 0x110000
static uint64_t from;
static uint8_t *prev, *ref;
static uint32_t *count;
static uint8_t pageDirty[SIZE / 256];
static uint32_t maxDirty, frames;
static uint8_t skip[SIZE / 256];

static void frame_hook(uint64_t frame, void *ctx)
{
  (void)ctx;
  if (frame < from) return;
  const uint8_t *m = o4db_memory();
  if (frame == from) { memcpy(prev, m, SIZE); memcpy(ref, m, SIZE); return; }
  for (size_t i = 0; i < SIZE; i++)
    if (m[i] != prev[i]) count[i]++;
  uint32_t dirty = 0;
  for (size_t p = 0; p < SIZE / 256; p++)
    if (!skip[p] && memcmp(m + p * 256, ref + p * 256, 256)) { pageDirty[p] = 1; dirty++; }
  if (dirty > maxDirty) maxDirty = dirty;
  memcpy(prev, m, SIZE);
  frames++;
}

int main(int argc, char **argv)
{
  if (argc < 4) { fprintf(stderr, "usage: o4dbChanges GAMEDIR MOVIE FROM [SOUND] [CLOCK]\n"); return 2; }
  size_t n;
  MovieInput *rows = movie_load(argv[2], &n);
  if (!rows) return 1;
  from = strtoull(argv[3], NULL, 10);
  for (const char *u = getenv("UNSAVED"); u && *u;)
  {
    unsigned a, b;
    if (sscanf(u, "%x-%x", &a, &b) != 2) break;
    for (unsigned p = a / 256; p <= b / 256 && p < SIZE / 256; p++) skip[p] = 1;
    u = strchr(u, ',');
    if (u) u++;
  }
  prev = malloc(SIZE); ref = malloc(SIZE); count = calloc(SIZE, sizeof *count);
  if (!o4db_setup(argv[1], argc > 4 ? argv[4] : "mt32", argc > 5 ? argv[5] : "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  o4db_set_frame_hook(frame_hook, NULL);
  O4dbInput *in = malloc(n * sizeof *in);
  for (size_t k = 0; k < n; k++) in[k] = (O4dbInput){ rows[k].buttons, rows[k].mouseX, rows[k].mouseY };
  o4db_new(&in[0]);
  o4db_run(in + 1, n - 1);
  // regions of changing bytes (gaps of up to 16 merged), with the most changes in each
  printf("frames compared %u; pages ever different from the first frame %u (at most %u at once)\n", frames,
         (unsigned)({ uint32_t t = 0; for (size_t p = 0; p < SIZE / 256; p++) t += pageDirty[p]; t; }), maxDirty);
  long s = -1, e = -1;
  uint32_t mx = 0;
  for (long i = 0; i <= SIZE; i++)
  {
    const int c = i < SIZE && count[i];
    if (c && s >= 0 && i - e <= 16) { e = i; if (count[i] > mx) mx = count[i]; continue; }
    if (s >= 0 && (!c || i - e > 16)) { printf("%05lX-%05lX %7ld bytes, at most %u changes\n", s, e, e - s + 1, mx); s = -1; }
    if (c) { s = e = i; mx = count[i]; }
  }
  return 0;
}
