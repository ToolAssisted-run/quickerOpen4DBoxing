// Words of the data segment at frame ends: o4dbPeek GAMEDIR MOVIE FRAMES OFFSETS (comma-separated; FRAMES "A-B/S" for
// every S-th frame from A to B)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "movie.h"
#include "pokes.h"

static uint64_t fa, fb, fs = 1;
static unsigned offs[64];
static int nOffs;

static void frame_hook(uint64_t frame, void *ctx)
{
  (void)ctx;
  pokes_apply(frame);
  if (frame < fa || frame > fb || (frame - fa) % fs) return;
  const uint8_t *ds = o4db_memory() + ((uint32_t)o4db_data_segment() << 4);
  printf("%7llu", (unsigned long long)frame);
  for (int k = 0; k < nOffs; k++) printf(" %04X:%04X", offs[k], ds[offs[k]] | ds[offs[k] + 1] << 8);
  printf("\n");
}

int main(int argc, char **argv)
{
  if (argc < 5) { fprintf(stderr, "usage: o4dbPeek GAMEDIR MOVIE A-B/S OFF,OFF,...\n"); return 2; }
  size_t n;
  MovieInput *rows = movie_load(argv[2], &n);
  if (sscanf(argv[3], "%llu-%llu/%llu", (unsigned long long *)&fa, (unsigned long long *)&fb, (unsigned long long *)&fs) < 2) return 2;
  for (char *t = strtok(argv[4], ","); t && nOffs < 64; t = strtok(NULL, ",")) offs[nOffs++] = (unsigned)strtoul(t, NULL, 16);
  if (!o4db_setup(argv[1], "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  pokes_load();
  o4db_set_frame_hook(frame_hook, NULL);
  O4dbInput *in = malloc(n * sizeof *in);
  for (size_t k = 0; k < n; k++) in[k] = (O4dbInput){ rows[k].buttons, rows[k].mouseX, rows[k].mouseY };
  o4db_new(&in[0]);
  o4db_run(in + 1, n - 1);
  return 0;
}
