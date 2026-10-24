// A movie's pictures and sound as a video's: o4dbVideo GAMEDIR MOVIE OUTDIR FROM TO
// The movie played from the start (a row a frame end, Chimera's; the environment's SPEED, POKES, INITIAL_SPEED and
// INITIAL_ROWS as o4dbBench's). At each frame end from FROM to TO (frame ends passed before it): the screen through
// the DAC to stdout (320x200 RGB, 3 bytes a pixel: ffmpeg's rawvideo rgb24) and a line of OUTDIR/frames.txt (the
// frame, the PIT's time, the round, its clock in ms, the boxers' punches landed in the round, their health); every
// MIDI byte the game writes, from the start (the MT-32's setup), to OUTDIR/midi.bin (the PIT's time, 8 bytes, the byte)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "movie.h"
#include "pokes.h"

static uint64_t from, to;
static FILE *info, *midi;

static const uint8_t *ds(void) { return o4db_memory() + ((uint32_t)o4db_data_segment() << 4); }
static uint16_t dw(uint16_t off) { const uint8_t *d = ds(); return (uint16_t)(d[off] | d[off + 1] << 8); }

static void frame_hook(uint64_t frame, void *ctx)
{
  (void)ctx;
  pokes_apply(frame);
  if (frame < from || frame >= to) return;
  static uint8_t rgb[320 * 200 * 3];
  const uint8_t *screen = o4db_screen();
  const uint8_t(*dac)[3] = o4db_palette();
  for (int k = 0; k < 320 * 200; k++)
    for (int c = 0; c < 3; c++) rgb[3 * k + c] = (uint8_t)(dac[screen[k]][c] << 2 | dac[screen[k]][c] >> 4);
  fwrite(rgb, 1, sizeof rgb, stdout);
  fprintf(info, "%llu %llu %u %u %u %u %d %d\n", (unsigned long long)frame, (unsigned long long)o4db_test_now(), dw(0x9302),
          dw(0xCF02) | (uint32_t)dw(0xCF04) << 16, dw(0xA488 + 2), dw(0xA4FC + 2), (int16_t)dw(0xBA12 + 0x10), (int16_t)dw(0xBB06 + 0x10));
}

static void midi_hook(uint8_t byte, uint64_t t, void *ctx)
{
  (void)ctx;
  fwrite(&t, 8, 1, midi);
  fwrite(&byte, 1, 1, midi);
}

int main(int argc, char **argv)
{
  if (argc < 6) { fprintf(stderr, "usage: o4dbVideo GAMEDIR MOVIE OUTDIR FROM TO\n"); return 2; }
  const size_t initialRows = getenv("INITIAL_SPEED") && getenv("INITIAL_ROWS") ? strtoull(getenv("INITIAL_ROWS"), NULL, 10) : 0;
  if (initialRows) o4db_set_speed((uint32_t)strtoul(getenv("INITIAL_SPEED"), NULL, 10));
  else if (getenv("SPEED")) o4db_set_speed((uint32_t)strtoul(getenv("SPEED"), NULL, 10));
  if (!o4db_setup(argv[1], "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  size_t n;
  MovieInput *rows = movie_load(argv[2], &n);
  if (!rows || !n) { fprintf(stderr, "o4dbVideo: no movie in %s\n", argv[2]); return 1; }
  O4dbInput *in = malloc(n * sizeof *in);
  for (size_t k = 0; k < n; k++) in[k] = (O4dbInput){ rows[k].buttons, rows[k].mouseX, rows[k].mouseY };
  from = strtoull(argv[4], NULL, 10);
  to = strtoull(argv[5], NULL, 10);
  char path[4096];
  snprintf(path, sizeof path, "%s/frames.txt", argv[3]);
  info = fopen(path, "w");
  snprintf(path, sizeof path, "%s/midi.bin", argv[3]);
  midi = fopen(path, "wb");
  if (!info || !midi) { fprintf(stderr, "o4dbVideo: cannot write in %s\n", argv[3]); return 1; }
  pokes_load();
  o4db_set_frame_hook(frame_hook, NULL);
  o4db_test_midi_hook(midi_hook, NULL);
  o4db_new(&in[0]);
  uint64_t frames = 0;
  if (initialRows > 1)
  {
    frames = o4db_run(in + 1, (initialRows < n ? initialRows : n) - 1);
    if (getenv("SPEED")) o4db_test_set_cycles((uint32_t)strtoul(getenv("SPEED"), NULL, 10));
  }
  if (frames + 1 < n) frames += o4db_run(in + 1 + frames, n - 1 - frames);
  fclose(info);
  fclose(midi);
  fprintf(stderr, "o4dbVideo: %zu rows, %llu frame ends, frames %llu to %llu written\n", n, (unsigned long long)frames, (unsigned long long)from,
          (unsigned long long)(to < frames ? to : frames));
  return 0;
}
