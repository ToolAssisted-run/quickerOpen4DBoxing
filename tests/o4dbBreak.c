// The break between two rounds, its fastest way through: a movie played to a frame (at its speed), the machine set
// to CYCLES (default 220,000), its steps given the input HOLD (default "D", the keypad's back) until the round is
// over (DS:CF0A) and saved there; then for each candidate (a key, the mouse's left button at a place) and each frame
// it may start at (0 to LATEST after the round's end), that input held a few frames and let go, the game run until
// the next round has begun (DS:9302 more) or 40 s: the frames each took, the best of each candidate.
//   o4dbBreak GAMEDIR MOVIE FROM [LATEST]   (the environment's POKES, CYCLES, HOLD, HELD (frames held, 2), STEP (the
//   start frames tried: every 25))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "movie.h"
#include "pokes.h"

static const uint8_t *ds(void) { return o4db_memory() + ((uint32_t)o4db_data_segment() << 4); }
static uint16_t dw(uint16_t off) { const uint8_t *d = ds(); return (uint16_t)(d[off] | d[off + 1] << 8); }
static void frame_hook(uint64_t frame, void *ctx) { (void)ctx; pokes_apply(frame); }

static uint64_t buttons_of(const char *keys)
{
  uint64_t b = 0;
  for (const char *c = keys; *c; c++)
  {
    const char *p = strchr(k_mnemonics, *c);
    if (p) b |= 1ull << (p - k_mnemonics);
  }
  return b;
}

int main(int argc, char **argv)
{
  if (argc < 4) { fprintf(stderr, "usage: o4dbBreak GAMEDIR MOVIE FROM [LATEST]\n"); return 2; }
  size_t n;
  MovieInput *rows = movie_load(argv[2], &n);
  if (!rows) return 1;
  const uint64_t from = strtoull(argv[3], NULL, 10);
  const int latest = argc > 4 ? atoi(argv[4]) : 400;
  const int held = getenv("HELD") ? atoi(getenv("HELD")) : 2;
  const int step = getenv("STEP") ? atoi(getenv("STEP")) : 25;  // (the start frames tried: every STEP)
  if (!o4db_setup(argv[1], "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  O4dbInput *in = malloc(n * sizeof *in);
  for (size_t k = 0; k < n; k++) in[k] = (O4dbInput){ rows[k].buttons, rows[k].mouseX, rows[k].mouseY };
  pokes_load();
  o4db_set_frame_hook(frame_hook, NULL);
  o4db_new(&in[0]);
  o4db_run_ahead(in + 1, from, n - 1);
  o4db_test_set_cycles(getenv("CYCLES") ? (uint32_t)strtoul(getenv("CYCLES"), NULL, 10) : 220000);
  // the round fought to its end with one input
  const O4dbInput hold = { buttons_of(getenv("HOLD") ? getenv("HOLD") : "D"), 135, 65 };
  for (int k = 0; k < 400000 && !dw(0xCF0A); k++) o4db_run(&hold, 1);
  const uint16_t round = dw(0x9302);
  printf("round %u over at frame %llu (blue down %u, knockdowns %u/%u, fight over %u)\n", round, (unsigned long long)o4db_frames(), dw(0xBA12 + 0xC),
         dw(0xA488 + 0xA), dw(0xA4FC + 0xA), dw(0xCF0C));
  void *state = malloc(o4db_state_size());
  o4db_save(state);
  const O4dbInput none = { 0, 160, 100 };
  static const struct { const char *name; const char *keys; int x, y; } cand[] = {
    { "nothing", "", 160, 100 },
    { "Enter", "N", 160, 100 },
    { "Space", "_", 160, 100 },
    { "Esc", "X", 160, 100 },
    { "keypad 5", "K", 160, 100 },
    { "the mouse's left at the box (150,185)", "<", 150, 185 },
    { "the mouse's left at (160,100)", "<", 160, 100 },
    { "F", "f", 160, 100 },
    { "1", "1", 160, 100 },
  };
  for (size_t c = 0; c < sizeof cand / sizeof *cand; c++)
  {
    int best = -1, bestStart = -1;
    printf("  %-40s", cand[c].name);
    for (int start = 0; start <= (c ? latest : 0); start += step)
    {
      o4db_load(state);
      const uint64_t f0 = o4db_frames();
      const O4dbInput press = { buttons_of(cand[c].keys), (int16_t)cand[c].x, (int16_t)cand[c].y };
      const O4dbInput rest = { 0, (int16_t)cand[c].x, (int16_t)cand[c].y };
      int k = 0;
      for (; k < 2800 && dw(0x9302) == round && !o4db_exited(); k++) o4db_run(k < start ? &none : k < start + held ? &press : &rest, 1);
      const int took = (int)(o4db_frames() - f0);
      if (dw(0x9302) != round && (best < 0 || took < best)) best = took, bestStart = start;
      printf(" %d:%s", start, dw(0x9302) != round ? "" : "-");
      if (dw(0x9302) != round) printf("%d", took);
      fflush(stdout);
    }
    if (best < 0) printf("\n    no next round within 40 s\n");
    else printf("\n    next round after %d frames (pressed from frame %d)\n", best, bestStart);
  }
  return 0;
}
