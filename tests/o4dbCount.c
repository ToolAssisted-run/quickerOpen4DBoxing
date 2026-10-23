// The machine plays a movie and, from a frame on, counts the entries of functions (their Ghidra addresses, as asm2c's
// FN hook names them: 25BB0202 for 25bb:0202) at each frame end: o4dbCount GAMEDIR MOVIE FROM TO ADDR... prints a
// line per frame: the frame and each function's entries since the last frame end
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "movie.h"

extern __thread __attribute__((tls_model("initial-exec"))) void (*asm_fn_hook)(uint32_t addr);
static uint64_t from, to;
static uint32_t addr[16], hits[16];
static int nAddr;

static void fn_hook(uint32_t a)
{
  for (int k = 0; k < nAddr; k++)
    if (a == addr[k]) hits[k]++;
}

static void frame_hook(uint64_t frame, void *ctx)
{
  (void)ctx;
  if (frame >= from && frame < to)
  {
    printf("%llu", (unsigned long long)frame);
    for (int k = 0; k < nAddr; k++) printf(" %u", hits[k]);
    printf("\n");
  }
  memset(hits, 0, sizeof hits);
}

int main(int argc, char **argv)
{
  if (argc < 6) { fprintf(stderr, "usage: o4dbCount GAMEDIR MOVIE FROM TO ADDR...\n"); return 2; }
  size_t n;
  MovieInput *rows = movie_load(argv[2], &n);
  if (!rows) return 1;
  from = strtoull(argv[3], NULL, 10);
  to = strtoull(argv[4], NULL, 10);
  for (int k = 5; k < argc && nAddr < 16; k++) addr[nAddr++] = (uint32_t)strtoul(argv[k], NULL, 16);
  if (!o4db_setup(argv[1], "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  o4db_set_frame_hook(frame_hook, NULL);
  O4dbInput *in = malloc(n * sizeof *in);
  for (size_t k = 0; k < n; k++) in[k] = (O4dbInput){ rows[k].buttons, rows[k].mouseX, rows[k].mouseY };
  o4db_new(&in[0]);
  // (the fn hook the game's setup installs is the cheats'; replaced once the program runs)
  o4db_run(in + 1, 1);
  asm_fn_hook = fn_hook;
  o4db_run(in + 2, n - 2);
  return 0;
}
