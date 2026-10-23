// Pictures of a movie's frames: o4dbShots GAMEDIR MOVIE OUTDIR FRAME... (the screen at those frame ends, PPM files
// OUTDIR/FRAME.ppm, 320x200 through the DAC)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "movie.h"
#include "pokes.h"

static const char *outDir;
static uint64_t *want;
static int nWant;

static void frame_hook(uint64_t frame, void *ctx)
{
  (void)ctx;
  pokes_apply(frame);
  for (int k = 0; k < nWant; k++)
    if (want[k] == frame)
    {
      char path[1024];
      snprintf(path, sizeof path, "%s/%06llu.ppm", outDir, (unsigned long long)frame);
      FILE *f = fopen(path, "wb");
      fprintf(f, "P6\n320 200\n255\n");
      const uint8_t *s = o4db_screen();
      const uint8_t(*dac)[3] = o4db_palette();
      for (int i = 0; i < 320 * 200; i++)
        for (int c = 0; c < 3; c++) fputc((dac[s[i]][c] & 63) << 2 | (dac[s[i]][c] & 63) >> 4, f);
      fclose(f);
    }
}

int main(int argc, char **argv)
{
  if (argc < 5) { fprintf(stderr, "usage: o4dbShots GAMEDIR MOVIE OUTDIR FRAME...\n"); return 2; }
  size_t n;
  MovieInput *rows = movie_load(argv[2], &n);
  outDir = argv[3];
  nWant = argc - 4;
  want = malloc(sizeof *want * nWant);
  for (int k = 0; k < nWant; k++) want[k] = strtoull(argv[4 + k], NULL, 10);
  if (!o4db_setup(argv[1], "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  pokes_load();
  o4db_set_frame_hook(frame_hook, NULL);
  O4dbInput *in = malloc(n * sizeof *in);
  for (size_t k = 0; k < n; k++) in[k] = (O4dbInput){ rows[k].buttons, rows[k].mouseX, rows[k].mouseY };
  o4db_new(&in[0]);
  o4db_run(in + 1, n - 1);
  return 0;
}
