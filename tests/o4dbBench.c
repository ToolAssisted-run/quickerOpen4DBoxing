// A machine's state saved after a movie's rows, or loaded and run for some frames (a benchmark, a profile's run):
//   o4dbBench GAMEDIR save MOVIE OUT     the movie played, its machine saved to OUT
//   o4dbBench GAMEDIR run STATE FRAMES   STATE loaded, FRAMES frames run with nothing pressed (the time printed)
//   (the environment's SKIP: o4db_test_hooks's parts skipped; CYCLES: the machine's speed from the state on)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "machine.h"
#include "movie.h"

int main(int argc, char **argv)
{
  if (argc < 5) { fprintf(stderr, "usage: o4dbBench GAMEDIR save MOVIE OUT | run STATE FRAMES\n"); return 2; }
  if (!o4db_setup(argv[1], "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  if (!strcmp(argv[2], "save"))
  {
    size_t n;
    MovieInput *rows = movie_load(argv[3], &n);
    O4dbInput *in = malloc(n * sizeof *in);
    for (size_t k = 0; k < n; k++) in[k] = (O4dbInput){ rows[k].buttons, rows[k].mouseX, rows[k].mouseY };
    o4db_new(&in[0]);
    o4db_run(in + 1, n - 1);
    uint8_t *s = malloc(o4db_state_size());
    o4db_save(s);
    FILE *f = fopen(argv[4], "wb");
    fwrite(s, 1, o4db_state_size(), f);
    fclose(f);
    return 0;
  }
  uint8_t *s = malloc(o4db_state_size());
  FILE *f = fopen(argv[3], "rb");
  if (!f || fread(s, 1, o4db_state_size(), f) != o4db_state_size()) { fprintf(stderr, "o4dbBench: no state in %s\n", argv[3]); return 1; }
  fclose(f);
  o4db_load(s);
  if (getenv("SKIP")) o4db_test_hooks(NULL, (unsigned)strtoul(getenv("SKIP"), NULL, 0));
  if (getenv("CYCLES")) o4db_test_set_cycles((uint32_t)strtoul(getenv("CYCLES"), NULL, 10));
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
