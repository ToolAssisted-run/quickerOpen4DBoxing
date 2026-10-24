// The fight's own state at every frame end of a movie: o4dbFightHash GAMEDIR MOVIE [FROM]
// The movie played from the start (a row a frame end, Chimera's; the environment's SPEED); at each frame end from FROM
// a line: the frame and a hash of what decides the fight (the search's hash but the stack: the boxers, their
// statistics, the random number, the computer's memory, the controls as read and their replay's pointers, the moves
// requested, the pose blocks, the round and the referee; PARTS: each part's hash too). Two movies that differ only in what is shown (the cameras,
// the function keys) give the same lines if the showing changes nothing of the fight.
#include <stdio.h>
#include <stdlib.h>

#include "machine.h"
#include "movie.h"

static uint64_t from;

static void frame_hook(uint64_t frame, void *ctx)
{
  (void)ctx;
  if (frame < from) return;
  static const struct { uint16_t off, n; } parts[] = {
    { 0xBA12, 0xF4 }, { 0xBB06, 0xF4 }, { 0xA488, 0x74 }, { 0xA4FC, 0x74 }, { 0x5A1C, 4 },   { 0x8472, 4 },   { 0x89F6, 10 },
    { 0x8A40, 2 },    { 0x8CC8, 16 },   { 0x8E3E, 8 },    { 0x9302, 2 },    { 0xA6B0, 16 }, { 0xAEC0, 2 },   { 0xB130, 16 },
    { 0xB6B8, 0x48 }, { 0xBD08, 12 },   { 0xCC58, 4 },    { 0xCF02, 0x60 },
  };
  const uint8_t *ds = o4db_memory() + ((uint32_t)o4db_data_segment() << 4);
  uint64_t h = 0xcbf29ce484222325ull;
  for (size_t k = 0; k < sizeof parts / sizeof *parts; k++) h = hash_bytes(ds + parts[k].off, parts[k].n, h);
  printf("%llu %016llx", (unsigned long long)frame, (unsigned long long)h);
  // (PARTS: each part's own hash too, its offset first)
  if (getenv("PARTS"))
    for (size_t k = 0; k < sizeof parts / sizeof *parts; k++)
      printf(" %04X:%08llx", parts[k].off, (unsigned long long)(hash_bytes(ds + parts[k].off, parts[k].n, 0xcbf29ce484222325ull) & 0xFFFFFFFF));
  // (CLOCK: the round's clock in milliseconds, DS:CF02)
  if (getenv("CLOCK")) printf(" clock %u", (unsigned)(ds[0xCF02] | ds[0xCF03] << 8 | ds[0xCF04] << 16 | ds[0xCF05] << 24));
  printf("\n");
}

int main(int argc, char **argv)
{
  if (argc < 3) { fprintf(stderr, "usage: o4dbFightHash GAMEDIR MOVIE [FROM]\n"); return 2; }
  if (getenv("SPEED")) o4db_set_speed((uint32_t)strtoul(getenv("SPEED"), NULL, 10));
  if (!o4db_setup(argv[1], "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  size_t n;
  MovieInput *rows = movie_load(argv[2], &n);
  if (!rows || !n) { fprintf(stderr, "o4dbFightHash: no movie in %s\n", argv[2]); return 1; }
  from = argc > 3 ? strtoull(argv[3], NULL, 10) : 0;
  O4dbInput *in = malloc(n * sizeof *in);
  for (size_t k = 0; k < n; k++) in[k] = (O4dbInput){ rows[k].buttons, rows[k].mouseX, rows[k].mouseY };
  o4db_set_frame_hook(frame_hook, NULL);
  o4db_new(&in[0]);
  if (n > 1) o4db_run(in + 1, n - 1);
  return 0;
}
