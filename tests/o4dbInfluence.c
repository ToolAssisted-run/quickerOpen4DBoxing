// Which changing regions of the data segment decide the fight: o4dbInfluence GAMEDIR MOVIE FROM REGIONS RUN
// The movie played to frame FROM (state S0) and 50 frames more (S1); for each region "START END" (data segment
// offsets, a line each, of REGIONS) S0 with that region's bytes taken from S1, both run RUN frames on the movie's next
// rows; the region is "decides" when the boxers' structures and round statistics differ at the end
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "movie.h"

static uint8_t *ds(void) { return o4db_memory() + ((uint32_t)o4db_data_segment() << 4); }
static uint64_t fight_hash(void)
{
  uint64_t h = 0xcbf29ce484222325ull;
  h = hash_bytes(ds() + 0xBA12, 0xF4 * 2, h);
  h = hash_bytes(ds() + 0xA488, 0x74 * 2, h);
  h = hash_bytes(ds() + 0x8472, 4, h);
  return h;
}

int main(int argc, char **argv)
{
  if (argc < 6) { fprintf(stderr, "usage: o4dbInfluence GAMEDIR MOVIE FROM REGIONS RUN\n"); return 2; }
  size_t n;
  MovieInput *rows = movie_load(argv[2], &n);
  const size_t from = strtoull(argv[3], NULL, 10), run = strtoull(argv[5], NULL, 10);
  if (from + 50 + run + 1 > n) { fprintf(stderr, "o4dbInfluence: the movie is too short\n"); return 1; }
  O4dbInput *in = malloc(n * sizeof *in);
  for (size_t k = 0; k < n; k++) in[k] = (O4dbInput){ rows[k].buttons, rows[k].mouseX, rows[k].mouseY };
  if (!o4db_setup(argv[1], "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  o4db_new(&in[0]);
  o4db_run(in + 1, from - 1);
  const size_t size = o4db_state_size();
  uint8_t *s0 = malloc(size), *s1 = malloc(size), *dsS1 = malloc(0x10000);
  o4db_save(s0);
  const uint64_t f0 = o4db_frames();
  o4db_run(in + from, 50);
  memcpy(dsS1, ds(), 0x10000);
  o4db_save(s1);
  // the reference: S0 run on
  o4db_load(s0);
  o4db_run(in + from, run);
  const uint64_t ref = fight_hash();
  printf("reference from frame %llu: %016llx\n", (unsigned long long)f0, (unsigned long long)ref);
  FILE *f = fopen(argv[4], "r");
  unsigned a, b;
  while (f && fscanf(f, "%x %x", &a, &b) == 2)
  {
    o4db_load(s0);
    int same = !memcmp(ds() + a, dsS1 + a, b - a + 1);
    memcpy(ds() + a, dsS1 + a, b - a + 1);
    o4db_run(in + from, run);
    const uint64_t h = fight_hash();
    printf("%04X-%04X %s%s\n", a, b, h == ref ? "no effect" : "DECIDES", same ? " (same at both frames)" : "");
    fflush(stdout);
  }
  return 0;
}
