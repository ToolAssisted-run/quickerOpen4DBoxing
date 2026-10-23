// A machine's state saved after a movie's rows, or loaded and run for some frames (a benchmark, a profile's run):
//   o4dbBench GAMEDIR save MOVIE OUT     the movie played, its machine saved to OUT (the environment's ROWS: the rows
//                                        played written there, the movie's and its last again at each frame end the
//                                        stop after it passed: Chimera's rows to the state)
//   o4dbBench GAMEDIR same MOVIE STATE   the movie played, its machine compared with STATE's (a movie from the start,
//                                        Chimera's: does it reach the machine a pipeline saved?)
//   (save, same: the environment's POKES given during the movie; INITIAL_SPEED and INITIAL_ROWS, experiments: the
//   movie's first rows played at that speed, the machine switched to SPEED where they stop, the rows the stop passed
//   skipped, as o4dbSteps2Rows's and the emulator's "Initial Speed")
//   o4dbBench GAMEDIR run STATE FRAMES   STATE loaded, FRAMES frames run with nothing pressed (the time printed)
//   o4dbBench GAMEDIR steps STATE N      STATE loaded, N steps (the controls' reads) run with nothing pressed
//   (the environment's SPEED: the machine's speed, o4db_set_speed, as the state's; SKIP: o4db_test_hooks's parts
//   skipped; CYCLES: the machine's speed changed from the state on; FAST:
//   the fight's logic alone, o4db_set_fast; WARM: steps run before the timed ones)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "machine.h"
#include "movie.h"
#include "pokes.h"

static void frame_hook(uint64_t frame, void *ctx) { (void)ctx; pokes_apply(frame); }

int main(int argc, char **argv)
{
  if (argc < 5) { fprintf(stderr, "usage: o4dbBench GAMEDIR save MOVIE OUT | same MOVIE STATE | run STATE FRAMES | steps STATE N\n"); return 2; }
  const int playing = !strcmp(argv[2], "save") || !strcmp(argv[2], "same");
  const size_t initialRows = playing && getenv("INITIAL_SPEED") && getenv("INITIAL_ROWS") ? strtoull(getenv("INITIAL_ROWS"), NULL, 10) : 0;
  if (initialRows) o4db_set_speed((uint32_t)strtoul(getenv("INITIAL_SPEED"), NULL, 10));
  else if (getenv("SPEED")) o4db_set_speed((uint32_t)strtoul(getenv("SPEED"), NULL, 10));
  if (!o4db_setup(argv[1], "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  if (playing)
  {
    size_t n;
    MovieInput *rows = movie_load(argv[3], &n);
    if (!rows || !n) { fprintf(stderr, "o4dbBench: no movie in %s\n", argv[3]); return 1; }
    O4dbInput *in = malloc(n * sizeof *in);
    for (size_t k = 0; k < n; k++) in[k] = (O4dbInput){ rows[k].buttons, rows[k].mouseX, rows[k].mouseY };
    pokes_load();
    o4db_set_frame_hook(frame_hook, NULL);
    o4db_new(&in[0]);
    uint64_t frames = 0;
    if (initialRows > 1)
    {
      frames = o4db_run(in + 1, (initialRows < n ? initialRows : n) - 1);
      if (getenv("SPEED")) o4db_test_set_cycles((uint32_t)strtoul(getenv("SPEED"), NULL, 10));
    }
    if (frames + 1 < n) frames += o4db_run(in + 1 + frames, n - 1 - frames);
    o4db_set_frame_hook(NULL, NULL);
    printf("%zu rows, %llu frame ends after the first\n", n, (unsigned long long)frames);
    uint8_t *s = malloc(o4db_state_size());
    o4db_save(s);
    if (!strcmp(argv[2], "same"))
    {
      uint8_t *t = malloc(o4db_state_size());
      FILE *f = fopen(argv[4], "rb");
      if (!f || fread(t, 1, o4db_state_size(), f) != o4db_state_size()) { fprintf(stderr, "o4dbBench: no state in %s\n", argv[4]); return 1; }
      fclose(f);
      size_t differ = 0;
      for (size_t k = 0; k < o4db_state_size(); k++)
        if (s[k] != t[k] && differ++ < 8) printf("  state byte %zu: %02X played, %02X saved\n", k, s[k], t[k]);
      if (differ) printf("%zu bytes differ\n", differ);
      else printf("the same machine\n");
      return differ ? 1 : 0;
    }
    FILE *f = fopen(argv[4], "wb");
    fwrite(s, 1, o4db_state_size(), f);
    fclose(f);
    if (getenv("ROWS"))
    {
      FILE *out = fopen(getenv("ROWS"), "w");
      if (!out) { fprintf(stderr, "o4dbBench: cannot write %s\n", getenv("ROWS")); return 1; }
      for (size_t k = 0; k < n; k++) movie_write_row(out, in[k].buttons, in[k].mouseX, in[k].mouseY);
      for (uint64_t k = n - 1; k < frames; k++) movie_write_row(out, in[n - 1].buttons, in[n - 1].mouseX, in[n - 1].mouseY);  // (held to the stop)
      fclose(out);
    }
    return 0;
  }
  uint8_t *s = malloc(o4db_state_size());
  FILE *f = fopen(argv[3], "rb");
  if (!f || fread(s, 1, o4db_state_size(), f) != o4db_state_size()) { fprintf(stderr, "o4dbBench: no state in %s\n", argv[3]); return 1; }
  fclose(f);
  o4db_load(s);
  if (getenv("SKIP")) o4db_test_hooks(NULL, (unsigned)strtoul(getenv("SKIP"), NULL, 0));
  if (getenv("CYCLES")) o4db_test_set_cycles((uint32_t)strtoul(getenv("CYCLES"), NULL, 10));
  if (!strcmp(argv[2], "steps"))
  {
    o4db_set_step_function(0x25BB0202, 8);
    if (getenv("FAST")) o4db_set_fast(1);
    const O4dbInput none = { 0, 135, 65 };
    for (size_t k = 0, warm = getenv("WARM") ? strtoull(getenv("WARM"), NULL, 10) : 0; k < warm; k++) o4db_step(&none);
    const size_t steps = strtoull(argv[4], NULL, 10);
    // (DELTA: each step from its state saved and loaded as a delta, as JaffarPlus steps; the frames of each printed)
    uint8_t *delta = NULL;
    if (getenv("DELTA"))
    {
      o4db_set_unsaved(0xA0000, 0xAFFFF);
      o4db_set_reference();
      delta = malloc(o4db_delta_size(1024));
    }
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (size_t k = 0; k < steps; k++)
    {
      if (delta) { o4db_save_delta(delta, 1024); o4db_load_delta(delta, 1024); }
      const uint64_t f = o4db_step(&none);
      if (delta && k < 20) printf("step %zu: %llu frames, frame %llu\n", k, (unsigned long long)f, (unsigned long long)o4db_frames());
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double sec = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
    printf("%zu steps in %.3f s (%.3f ms a step), %llu replays\n", steps, sec, sec * 1000.0 / (double)steps, (unsigned long long)o4db_fast_replays());
    return 0;
  }
  const size_t frames = strtoull(argv[4], NULL, 10);
  O4dbInput *in = calloc(frames, sizeof *in);
  for (size_t k = 0; k < frames; k++) in[k].mouseX = 135, in[k].mouseY = 65;
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  o4db_run(in, frames);
  clock_gettime(CLOCK_MONOTONIC, &t1);
  printf("%zu frames in %.3f s (%.3f ms a frame)\n", frames, (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9,
         ((t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9) * 1000.0 / (double)frames);
  return 0;
}
