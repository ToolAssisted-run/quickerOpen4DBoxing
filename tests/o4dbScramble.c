// Whether the game reads memory back: a movie played to a frame and saved; played on from there, the hash of the
// memory outside the given ranges at every frame end; the save taken back, the ranges filled with noise, played on
// again and compared: o4dbScramble GAMEDIR MOVIE FROM FRAMES RANGE...  (RANGE: START-END, hexadecimal linear
// addresses, END included). Prints the first frame end where the rest of the memory differs, and its bytes there.
// The environment's POKES: memory changes at frame ends (pokes.h)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "movie.h"
#include "pokes.h"

#define SIZE 0x110000
static uint32_t ranges[64][2];
static int nRanges;
static uint64_t f0, frames, mismatch = UINT64_MAX, snapFrame = UINT64_MAX;
static uint64_t *hashes;
static int pass;
static uint8_t *snap, *masked;

static uint64_t masked_hash(void)
{
  memcpy(masked, o4db_memory(), SIZE);
  for (int r = 0; r < nRanges; r++) memset(masked + ranges[r][0], 0, ranges[r][1] - ranges[r][0] + 1);
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < SIZE; i++) h = (h ^ masked[i]) * 1099511628211ull;
  return h;
}

static void frame_hook(uint64_t frame, void *ctx)
{
  (void)ctx;
  pokes_apply(frame);
  if (frame < f0 || frame >= f0 + frames) return;
  if (frame == snapFrame)
  {
    if (pass == 2) { masked_hash(); memcpy(snap, masked, SIZE); }
    if (pass == 3)
    {
      masked_hash();
      int shown = 0;
      for (size_t i = 0; i < SIZE && shown < 64; i++)
        if (masked[i] != snap[i]) { printf("  %05zX: %02X (noise: %02X)\n", i, snap[i], masked[i]); shown++; }
    }
    return;
  }
  if (pass == 0) hashes[frame - f0] = masked_hash();
  if (pass == 1 && mismatch == UINT64_MAX && hashes[frame - f0] != masked_hash()) mismatch = frame;
}

static void scramble(void)
{
  uint8_t *m = o4db_memory();
  uint64_t x = 88172645463325252ull;
  for (int r = 0; r < nRanges; r++)
    for (uint32_t a = ranges[r][0]; a <= ranges[r][1]; a++) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; m[a] = (uint8_t)x; }
}

int main(int argc, char **argv)
{
  if (argc < 6) { fprintf(stderr, "usage: o4dbScramble GAMEDIR MOVIE FROM FRAMES RANGE...\n"); return 2; }
  size_t n;
  MovieInput *rows = movie_load(argv[2], &n);
  if (!rows) return 1;
  const uint64_t from = strtoull(argv[3], NULL, 10);
  frames = strtoull(argv[4], NULL, 10);
  for (int a = 5; a < argc && nRanges < 64; a++, nRanges++)
    if (sscanf(argv[a], "%x-%x", &ranges[nRanges][0], &ranges[nRanges][1]) != 2) { fprintf(stderr, "bad range %s\n", argv[a]); return 2; }
  if (!o4db_setup(argv[1], "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  O4dbInput *in = malloc(n * sizeof *in);
  for (size_t k = 0; k < n; k++) in[k] = (O4dbInput){ rows[k].buttons, rows[k].mouseX, rows[k].mouseY };
  hashes = calloc(frames, sizeof *hashes);
  snap = malloc(SIZE);
  masked = malloc(SIZE);
  pokes_load();
  o4db_set_frame_hook(frame_hook, NULL);
  f0 = UINT64_MAX;
  o4db_new(&in[0]);
  o4db_run_ahead(in + 1, from, n - 1);
  f0 = o4db_frames() + 1;
  if (f0 + frames > n) frames = n - f0;
  void *state = malloc(o4db_state_size());
  o4db_save(state);
  for (pass = 0; pass < 2; pass++)
  {
    o4db_load(state);
    if (pass == 1) scramble();
    o4db_run_ahead(in + f0, frames, n - f0);
  }
  if (mismatch == UINT64_MAX) { printf("from frame %llu, %llu frames: the rest of the memory never differs\n", (unsigned long long)f0, (unsigned long long)frames); return 0; }
  printf("from frame %llu: the rest of the memory first differs at frame %llu (%llu frames on)\n", (unsigned long long)f0, (unsigned long long)mismatch,
         (unsigned long long)(mismatch - f0));
  snapFrame = mismatch;
  for (pass = 2; pass < 4; pass++)
  {
    o4db_load(state);
    if (pass == 3) scramble();
    o4db_run_ahead(in + f0, mismatch - f0 + 1, n - f0);
  }
  return 1;
}
