// The machine plays a movie (movie.h), printing at every frame end its frame and a hash of the conventional and video
// memory and the registers (as o4dbOracle does at the same points): o4dbPlay GAMEDIR MOVIE [MODE] [SOUND] [CLOCK]
//   MODE whole   the movie in one run (default)
//        steps   one run a frame (the stops between), the rows the stops pass given as they come
//        threads one run a frame, the machine moved between two threads through its saved state at every stop
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "machine.h"
#include "movie.h"

static const char *gameDir, *sound = "mt32", *clockStart = "1991-07-30 14:47:00";
static MovieInput *rows;
static size_t nRows;

static void frame_hook(uint64_t frame, void *ctx)
{
  (void)ctx;
  const uint8_t *m = o4db_memory();
  uint64_t h = hash_bytes(m, 0xB0000, 0xcbf29ce484222325ull);
  h = hash_bytes(o4db_registers(), 24, h);
  printf("%llu %016llx\n", (unsigned long long)frame, (unsigned long long)h);
}

static O4dbInput in_of(const MovieInput *m) { return (O4dbInput){ m->buttons, m->mouseX, m->mouseY }; }

// two threads taking turns: each runs a frame on the state the other left
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int turn;
static size_t at = 1;
static uint8_t *state;
static size_t stateSize;
static int started;

static void *worker(void *arg)
{
  const int me = (int)(long)arg;
  if (getenv("SPEED")) o4db_set_speed((uint32_t)strtoul(getenv("SPEED"), NULL, 10));  // (the environment's SPEED: the PC's speed)
  if (!o4db_setup(gameDir, sound, clockStart, NULL)) { fprintf(stderr, "%s\n", o4db_error()); exit(1); }
  o4db_set_frame_hook(frame_hook, NULL);
  O4dbInput boot = in_of(&rows[0]);
  o4db_new(&boot);
  for (;;)
  {
    pthread_mutex_lock(&lock);
    while (turn != me) pthread_cond_wait(&cond, &lock);
    if (at >= nRows) { turn = 1 - me; pthread_cond_broadcast(&cond); pthread_mutex_unlock(&lock); break; }
    if (started) o4db_load(state);
    O4dbInput in[64];
    size_t avail = nRows - at < 64 ? nRows - at : 64;
    for (size_t k = 0; k < avail; k++) in[k] = in_of(&rows[at + k]);
    const uint64_t f = o4db_run_ahead(in, 1, avail);
    if (f == 0) { at = nRows; }
    at += f;
    if (!stateSize) { stateSize = o4db_state_size(); state = malloc(stateSize); }
    o4db_save(state);
    started = 1;
    fflush(stdout);
    turn = 1 - me;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
  }
  return NULL;
}

int main(int argc, char **argv)
{
  if (argc < 3) { fprintf(stderr, "usage: o4dbPlay GAMEDIR MOVIE [whole|steps|threads] [SOUND] [CLOCK]\n"); return 2; }
  gameDir = argv[1];
  rows = movie_load(argv[2], &nRows);
  if (!rows || nRows < 2) { fprintf(stderr, "o4dbPlay: no movie in %s\n", argv[2]); return 1; }
  const char *mode = argc > 3 ? argv[3] : "whole";
  if (argc > 4) sound = argv[4];
  if (argc > 5) clockStart = argv[5];
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  if (!strcmp(mode, "threads"))
  {
    pthread_t a, b;
    pthread_create(&a, NULL, worker, (void *)0L);
    pthread_create(&b, NULL, worker, (void *)1L);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
  }
  else
  {
    if (getenv("SPEED")) o4db_set_speed((uint32_t)strtoul(getenv("SPEED"), NULL, 10));
    if (!o4db_setup(gameDir, sound, clockStart, NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
    o4db_set_frame_hook(frame_hook, NULL);
    O4dbInput boot = in_of(&rows[0]);
    o4db_new(&boot);
    O4dbInput *in = malloc(nRows * sizeof *in);
    for (size_t k = 0; k < nRows; k++) in[k] = in_of(&rows[k]);
    if (!strcmp(mode, "whole")) o4db_run(in + 1, nRows - 1);
    else
      for (size_t i = 1; i < nRows;)
      {
        const uint64_t f = o4db_run_ahead(in + i, 1, nRows - i);
        if (!f) break;
        i += f;
      }
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  fprintf(stderr, "o4dbPlay: %zu rows, %llu frames, state %zu bytes, %.2f s\n", nRows, (unsigned long long)o4db_frames(), o4db_state_size(),
          (double)(t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9);
  return 0;
}
