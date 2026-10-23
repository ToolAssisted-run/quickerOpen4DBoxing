// (tests) Words of the data segment changed at frame ends: the file named by the environment's POKES, lines "FRAME
// OFFSET VALUE" (hexadecimal offset and value), given at that frame end before its input (as the emulator's "Initial
// Pokes")
#ifndef O4DB_TESTS_POKES_H
#define O4DB_TESTS_POKES_H
#include <stdio.h>
#include <stdlib.h>

#include "machine.h"

typedef struct { unsigned long long frame; unsigned off, value; } TestPoke;
static TestPoke *testPokes;
static size_t nTestPokes;

static void pokes_load(void)
{
  const char *path = getenv("POKES");
  if (!path) return;
  FILE *f = fopen(path, "r");
  size_t cap = 64;
  testPokes = malloc(cap * sizeof *testPokes);
  TestPoke p;
  while (f && fscanf(f, "%llu %x %x", &p.frame, &p.off, &p.value) == 3)
  {
    if (nTestPokes == cap) testPokes = realloc(testPokes, (cap *= 2) * sizeof *testPokes);
    testPokes[nTestPokes++] = p;
  }
  if (f) fclose(f);
}

static void pokes_apply(uint64_t frame)
{
  uint8_t *ds = o4db_memory() + ((uint32_t)o4db_data_segment() << 4);
  for (size_t k = 0; k < nTestPokes; k++)
    if (testPokes[k].frame == frame)
    {
      ds[testPokes[k].off] = (uint8_t)testPokes[k].value;
      ds[testPokes[k].off + 1] = (uint8_t)(testPokes[k].value >> 8);
    }
}
#endif
