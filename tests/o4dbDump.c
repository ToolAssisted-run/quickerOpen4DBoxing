// The machine plays a movie and, from a frame on, writes the data segment (64 KB) at every frame end to a file, frame
// after frame: o4dbDump GAMEDIR MOVIE FROM OUT [SOUND] [CLOCK]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "movie.h"

static FILE *out;
static uint64_t from;

static void frame_hook(uint64_t frame, void *ctx)
{
  (void)ctx;
  if (frame < from) return;
  const uint8_t *ds = o4db_memory() + ((uint32_t)o4db_data_segment() << 4);
  fwrite(ds, 1, 0x10000, out);
}

int main(int argc, char **argv)
{
  if (argc < 5) { fprintf(stderr, "usage: o4dbDump GAMEDIR MOVIE FROM OUT [SOUND] [CLOCK]\n"); return 2; }
  size_t n;
  MovieInput *rows = movie_load(argv[2], &n);
  if (!rows) return 1;
  from = strtoull(argv[3], NULL, 10);
  out = fopen(argv[4], "wb");
  if (!o4db_setup(argv[1], argc > 5 ? argv[5] : "mt32", argc > 6 ? argv[6] : "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  o4db_set_frame_hook(frame_hook, NULL);
  O4dbInput *in = malloc(n * sizeof *in);
  for (size_t k = 0; k < n; k++) in[k] = (O4dbInput){ rows[k].buttons, rows[k].mouseX, rows[k].mouseY };
  o4db_new(&in[0]);
  o4db_run(in + 1, n - 1);
  fclose(out);
  return 0;
}
