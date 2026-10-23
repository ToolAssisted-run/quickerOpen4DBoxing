// Two machines on two threads made as the JaffarPlus emulator makes them (the initial sequence at INITIAL_SPEED, the
// speed then SPEED, the step function, the fast fight if FAST, the pictures unsaved, the reference), and the pages of
// the second's memory that differ from the first's: o4dbThreads GAMEDIR INITIAL
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "movie.h"

static const char *gameDir;
static O4dbInput *rows;
static size_t nRows;
static uint8_t *mem[2];

static void *make(void *arg)
{
  const int k = (int)(intptr_t)arg;
  const uint32_t speed = getenv("SPEED") ? (uint32_t)strtoul(getenv("SPEED"), NULL, 10) : 22000;
  const uint32_t initial = getenv("INITIAL_SPEED") ? (uint32_t)strtoul(getenv("INITIAL_SPEED"), NULL, 10) : speed;
  o4db_set_speed(initial);
  if (!o4db_setup(gameDir, "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); exit(1); }
  o4db_new(&rows[0]);
  o4db_run(rows + 1, nRows - 1);
  if (initial != speed) o4db_test_set_cycles(speed);
  o4db_set_step_function(0x25BB0202, 8);
  o4db_set_fast(getenv("FAST") != NULL);
  o4db_set_unsaved(0xA0000, 0xAFFFF);
  o4db_set_unsaved(0x68000, 0x71FFF);
  o4db_set_unsaved(0x88000, 0x88FFF);
  o4db_set_unsaved(0x51000, 0x51FFF);
  mem[k] = malloc(0x110000);
  memcpy(mem[k], o4db_memory(), 0x110000);
  printf("machine %d: %d pages differ from the reference, frame %llu\n", k, o4db_set_reference(), (unsigned long long)o4db_frames());
  return NULL;
}

int main(int argc, char **argv)
{
  if (argc < 3) { fprintf(stderr, "usage: o4dbThreads GAMEDIR INITIAL\n"); return 2; }
  gameDir = argv[1];
  MovieInput *m = movie_load(argv[2], &nRows);
  rows = malloc(nRows * sizeof *rows);
  for (size_t k = 0; k < nRows; k++) rows[k] = (O4dbInput){ m[k].buttons, m[k].mouseX, m[k].mouseY };
  for (int k = 0; k < 2; k++)
  {
    pthread_t t;
    pthread_create(&t, NULL, make, (void *)(intptr_t)k);
    pthread_join(t, NULL);
  }
  int shown = 0;
  for (size_t p = 0; p < 0x110000 / 256; p++)
    if (memcmp(mem[0] + p * 256, mem[1] + p * 256, 256) && shown++ < 60)
    {
      size_t first = 0;
      while (mem[0][p * 256 + first] == mem[1][p * 256 + first]) first++;
      printf("  page %05zX differs (first at %05zX: %02X %02X)\n", p * 256, p * 256 + first, mem[0][p * 256 + first], mem[1][p * 256 + first]);
    }
  printf("%d pages differ\n", shown);
  return 0;
}
