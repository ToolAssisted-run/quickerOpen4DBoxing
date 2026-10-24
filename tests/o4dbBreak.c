// The break between two rounds, its fastest way through: a movie played to a frame (at its speed), the machine set
// to CYCLES (default 220,000), its steps given the input HOLD (default "D", the keypad's back) until the round is
// over (DS:CF0A) and saved there; then for each candidate (a key, the mouse's left button at a place) and each frame
// it may start at (0 to LATEST after the round's end), that input held a few frames and let go, the game run until
// the next round has begun (DS:9302 more) or 40 s: the frames each took, the best of each candidate.
//   o4dbBreak GAMEDIR MOVIE FROM [LATEST]   (the environment's POKES, CYCLES, HOLD, HELD (frames held, 2), STEP (the
//   start frames tried: every 25, latest first, then one by one before the best), ONLY (the candidates whose names
//   hold it)). Each next round's start printed with the boxers' health and stamina (blue's, red's) there.
// The environment's STATE: a saved machine (a verified round's end: o4dbSteps2Rows's SAVE_END; saved at CYCLES, the
// machine set up at that speed, which a state does not hold) instead of the movie played to FROM (the movie's first row
// still the machine's first input); SAVE: the fastest candidate played again and the machine saved where the next
// round has begun, its rows from the start (STATE's or FROM's: the round's rest at HOLD, then the candidate's; a row a
// frame end passed, Chimera's) written to ROWS.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "movie.h"
#include "pokes.h"

static const uint8_t *ds(void) { return o4db_memory() + ((uint32_t)o4db_data_segment() << 4); }
static uint16_t dw(uint16_t off) { const uint8_t *d = ds(); return (uint16_t)(d[off] | d[off + 1] << 8); }
static void frame_hook(uint64_t frame, void *ctx) { (void)ctx; pokes_apply(frame); }
static const O4dbInput none = { 0, 160, 100 };

// a candidate pressed at a frame after the round's end (held some frames, let go), printed: the frames to the next
// round's start and the boxers' health and stamina there, or "-" if it has not begun sooner than the best so far (40 s
// at most), which it replaces if sooner
static void trial(const void *state, uint16_t round, const O4dbInput *press, const O4dbInput *rest, int start, int held, int *best, int *bestStart)
{
  o4db_load(state);
  const uint64_t f0 = o4db_frames();
  const int limit = *best < 0 ? 2800 : *best + 1;
  for (int k = 0; k < limit && dw(0x9302) == round && !o4db_exited(); k++) o4db_run(k < start ? &none : k < start + held ? press : rest, 1);
  const int took = dw(0x9302) != round ? (int)(o4db_frames() - f0) : -1;
  if (took >= 0 && (*best < 0 || took < *best)) *best = took, *bestStart = start;
  printf(" %d:", start);
  if (took < 0) printf("-");
  else printf("%d (%d/%d %d/%d)", took, (int16_t)dw(0xBA12 + 0x10), (int16_t)dw(0xBA12 + 0xE), (int16_t)dw(0xBB06 + 0x10), (int16_t)dw(0xBB06 + 0xE));
  fflush(stdout);
}

// a run's rows: its input once a frame end it passed (a run that stops a little later passes more, its input held)
static void put_rows(FILE *out, const O4dbInput *row, uint64_t frames)
{
  for (uint64_t k = 0; out && k < frames; k++) movie_write_row(out, row->buttons, row->mouseX, row->mouseY);
}

static O4dbButtons buttons_of(const char *keys)
{
  O4dbButtons b = 0;
  for (const char *c = keys; *c; c++)
  {
    const char *p = strchr(k_mnemonics, *c);
    if (p) b |= (O4dbButtons)1 << (p - k_mnemonics);
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
  const uint32_t cycles = getenv("CYCLES") ? (uint32_t)strtoul(getenv("CYCLES"), NULL, 10) : 220000;
  if (getenv("STATE")) o4db_set_speed(cycles);
  if (!o4db_setup(argv[1], "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  O4dbInput *in = malloc(n * sizeof *in);
  for (size_t k = 0; k < n; k++) in[k] = (O4dbInput){ rows[k].buttons, rows[k].mouseX, rows[k].mouseY };
  pokes_load();
  o4db_set_frame_hook(frame_hook, NULL);
  o4db_new(&in[0]);
  if (getenv("STATE"))
  {
    FILE *f = fopen(getenv("STATE"), "rb");
    void *st = malloc(o4db_state_size());
    if (!f || fread(st, 1, o4db_state_size(), f) != o4db_state_size()) { fprintf(stderr, "o4dbBreak: no state in %s\n", getenv("STATE")); return 1; }
    fclose(f);
    o4db_load(st);
    free(st);
  }
  else
  {
    o4db_run_ahead(in + 1, from, n - 1);
    o4db_test_set_cycles(cycles);
  }
  // the round fought to its end with one input
  void *first = malloc(o4db_state_size());
  o4db_save(first);
  const O4dbInput hold = { buttons_of(getenv("HOLD") ? getenv("HOLD") : "D"), 135, 65 };
  for (int k = 0; k < 400000 && !dw(0xCF0A); k++) o4db_run(&hold, 1);
  const uint16_t round = dw(0x9302);
  printf("round %u over at frame %llu (blue down %u, knockdowns %u/%u, fight over %u)\n", round, (unsigned long long)o4db_frames(), dw(0xBA12 + 0xC),
         dw(0xA488 + 0xA), dw(0xA4FC + 0xA), dw(0xCF0C));
  void *state = malloc(o4db_state_size());
  o4db_save(state);
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
  int overall = -1, overallCand = -1, overallStart = -1;
  for (size_t c = 0; c < sizeof cand / sizeof *cand; c++)
  {
    if (getenv("ONLY") && !strstr(cand[c].name, getenv("ONLY"))) continue;
    int best = -1, bestStart = -1;
    printf("  %-40s", cand[c].name);
    const O4dbInput press = { buttons_of(cand[c].keys), (int16_t)cand[c].x, (int16_t)cand[c].y };
    const O4dbInput rest = { 0, (int16_t)cand[c].x, (int16_t)cand[c].y };
    // (the latest first, so that a start that cannot beat the best gives up there; then the frames between the best
    // and the start tried after it, one by one)
    for (int start = c ? latest / step * step : 0; start >= 0; start -= step) trial(state, round, &press, &rest, start, held, &best, &bestStart);
    if (best >= 0 && step > 1)
      for (int start = bestStart - step + 1 > 0 ? bestStart - step + 1 : 0, end = bestStart; start < end; start++)
        trial(state, round, &press, &rest, start, held, &best, &bestStart);
    if (best < 0) printf("\n    no next round within 40 s\n");
    else printf("\n    next round after %d frames (pressed from frame %d)\n", best, bestStart);
    if (best >= 0 && (overall < 0 || best < overall)) overall = best, overallCand = (int)c, overallStart = bestStart;
  }
  if (getenv("SAVE"))
  {
    if (overall < 0) { fprintf(stderr, "o4dbBreak: no candidate began the next round\n"); return 1; }
    // the fastest again from the start, its rows written, the machine saved where the next round has begun
    o4db_load(first);
    FILE *out = getenv("ROWS") ? fopen(getenv("ROWS"), "w") : NULL;
    for (int k = 0; k < 400000 && !dw(0xCF0A); k++) put_rows(out, &hold, o4db_run(&hold, 1));
    const O4dbInput press = { buttons_of(cand[overallCand].keys), (int16_t)cand[overallCand].x, (int16_t)cand[overallCand].y };
    const O4dbInput rest = { 0, (int16_t)cand[overallCand].x, (int16_t)cand[overallCand].y };
    for (int k = 0; k < 2800 && dw(0x9302) == round && !o4db_exited(); k++)
    {
      const O4dbInput *row = k < overallStart ? &none : k < overallStart + held ? &press : &rest;
      put_rows(out, row, o4db_run(row, 1));
    }
    if (out) fclose(out);
    FILE *f = fopen(getenv("SAVE"), "wb");
    if (!f) { fprintf(stderr, "o4dbBreak: cannot write %s\n", getenv("SAVE")); return 1; }
    void *next = malloc(o4db_state_size());
    o4db_save(next);
    fwrite(next, 1, o4db_state_size(), f);
    fclose(f);
    printf("saved: %s, round %u at frame %llu (%d frames, %s from frame %d)\n", getenv("SAVE"), dw(0x9302), (unsigned long long)o4db_frames(), overall,
           cand[overallCand].name, overallStart);
  }
  return 0;
}
