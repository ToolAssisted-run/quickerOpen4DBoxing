// Two machines, each booted on its own thread, play the same movie; the memory where they differ: o4dbTwoBoots GAMEDIR MOVIE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "movie.h"

static const char *gameDir;
static MovieInput *rows;
static size_t nRows;
static uint8_t *mem[2];

static void *worker(void *arg)
{
  const int me = (int)(long)arg;
  if (!o4db_setup(gameDir, "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); exit(1); }
  O4dbInput *in = malloc(nRows * sizeof *in);
  for (size_t k = 0; k < nRows; k++) in[k] = (O4dbInput){ rows[k].buttons, rows[k].mouseX, rows[k].mouseY };
  o4db_new(&in[0]);
  o4db_run(in + 1, nRows - 1);
  if (me == 0 && getenv("TWICE"))  // a second start on the same thread (as JaffarPlus's driver and first runner)
  {
    o4db_new(&in[0]);
    o4db_run(in + 1, nRows - 1);
  }
  mem[me] = malloc(0x110000);
  memcpy(mem[me], o4db_memory(), 0x110000);
  return NULL;
}

int main(int argc, char **argv)
{
  gameDir = argv[1];
  rows = movie_load(argv[2], &nRows);
  pthread_t a, b;
  pthread_create(&a, NULL, worker, (void *)0L);
  pthread_create(&b, NULL, worker, (void *)1L);
  pthread_join(a, NULL);
  pthread_join(b, NULL);
  int n = 0;
  for (size_t i = 0; i < 0x110000; i++)
    if (mem[0][i] != mem[1][i] && n++ < 40) printf("%05zX: %02X %02X '%c' '%c'\n", i, mem[0][i], mem[1][i], mem[0][i] >= 32 && mem[0][i] < 127 ? mem[0][i] : '.', mem[1][i] >= 32 && mem[1][i] < 127 ? mem[1][i] : '.');
  printf("%d bytes differ\n", n);
  return 0;
}
