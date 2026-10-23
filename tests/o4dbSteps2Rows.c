// A search's solution as Chimera movie rows: o4dbSteps2Rows GAMEDIR INITIAL SOLUTION OUT [MAXFRAMES] [SOUND] [CLOCK]
// INITIAL: the initial sequence's rows (the emulator's "Initial Sequence File Path"), played a row a frame as the
// emulator plays them; SOLUTION: the search's steps (an input a line, the emulator's steps: they end where the game
// reads the boxers' controls, 25BB0202, or after MAXFRAMES frames, 8 by default); OUT: every row Chimera's core is given,
// the initial ones, the frames the stop after them held the last one, and each step's input once a frame it passed.
// The last line printed: the frames, and the hash of the memory and registers where the last step stopped.
// The environment's SPEED: the machine's (o4db_set_speed), the project's CPU speed; INITIAL_SPEED (experiments): the
// initial sequence's, the machine switched to SPEED at its end (as the emulator's "Initial Speed"); FASTCHECK: the steps
// also run as the fast fight (o4db_set_fast, as the emulator's "Fast Fight") from the same start, its boxers' steps
// (1eb6:3354) and its steps' frames compared with the game's.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "movie.h"
#include "pokes.h"

// (HASHES set: the frame and the memory's hash at every frame end, as o4dbOracle prints them, to stdout)
static int printHashes;
static void frame_hook(uint64_t frame, void *ctx)
{
  (void)ctx;
  pokes_apply(frame);
  if (!printHashes) return;
  uint64_t h = hash_bytes(o4db_memory(), 0xB0000, 0xcbf29ce484222325ull);
  h = hash_bytes(o4db_registers(), 24, h);
  printf("%llu %016llx\n", (unsigned long long)frame, (unsigned long long)h);
}

// (FASTCHECK: every boxer's step, its time and the logic's bytes: the boxers but their timers, their statistics, the
// random number, the controls' replay)
typedef struct { int32_t t; uint8_t bytes[1024]; size_t n; } Event;
static Event *events[2];
static size_t nEvents[2], capEvents[2], stepStart;
static int checking = -1;
static const uint8_t *dseg(void) { return o4db_memory() + ((uint32_t)o4db_data_segment() << 4); }
static uint16_t dword_at(uint16_t off) { const uint8_t *d = dseg(); return (uint16_t)(d[off] | d[off + 1] << 8); }
static void event_hook(uint32_t fn)
{
  if (checking < 0) return;
  if (fn == 0xFFFFFFFF) { nEvents[checking] = stepStart; return; }
  if (fn != 0x1EB63354) return;
  if (nEvents[checking] == capEvents[checking]) events[checking] = realloc(events[checking], (capEvents[checking] = capEvents[checking] ? 2 * capEvents[checking] : 4096) * sizeof(Event));
  Event *e = &events[checking][nEvents[checking]++];
  const uint16_t *r = (const uint16_t *)o4db_registers();
  const uint8_t *m = o4db_memory();
  const uint32_t sp = ((uint32_t)r[10] << 4) + r[7];
  const int boxer = m[sp + 4] | m[sp + 5] << 8;
  e->t = (int32_t)(dword_at(0xCC58) | (uint32_t)dword_at(0xCC5A) << 16) + (int16_t)dword_at((uint16_t)((boxer ? 0xBB06 : 0xBA12) + 0x58));
  static const uint16_t parts[][2] = { { 0xBA12, 0x58 }, { 0xBA12 + 0x5A, 0xF4 - 0x5A }, { 0xBB06, 0x58 }, { 0xBB06 + 0x5A, 0xF4 - 0x5A },
                                       { 0xA488, 0x74 }, { 0xA4FC, 0x74 }, { 0x8472, 4 }, { 0x8CC8, 16 } };
  e->n = 0;
  for (size_t p = 0; p < sizeof parts / sizeof *parts; p++) { memcpy(e->bytes + e->n, dseg() + parts[p][0], parts[p][1]); e->n += parts[p][1]; }
}

static void write_row(FILE *f, const O4dbInput *in)
{
  fprintf(f, "|%5d,%5d,", in->mouseX, in->mouseY);
  for (int k = 0; k < 57; k++) fputc((in->buttons >> k) & 1 ? k_mnemonics[k] : '.', f);
  fputs("|\n", f);
}

static O4dbInput *load(const char *path, size_t *n)
{
  MovieInput *m = movie_load(path, n);
  if (!m) { fprintf(stderr, "o4dbSteps2Rows: cannot read %s\n", path); exit(1); }
  O4dbInput *in = malloc((*n ? *n : 1) * sizeof *in);
  for (size_t k = 0; k < *n; k++) in[k] = (O4dbInput){ m[k].buttons, m[k].mouseX, m[k].mouseY };
  free(m);
  return in;
}

int main(int argc, char **argv)
{
  if (argc < 5) { fprintf(stderr, "usage: o4dbSteps2Rows GAMEDIR INITIAL SOLUTION OUT [MAXFRAMES] [SOUND] [CLOCK]\n"); return 2; }
  size_t ni, ns;
  O4dbInput *init = load(argv[2], &ni), *steps = load(argv[3], &ns);
  const unsigned maxFrames = argc > 5 ? (unsigned)atoi(argv[5]) : 8;
  const uint32_t speed = getenv("SPEED") ? (uint32_t)strtoul(getenv("SPEED"), NULL, 10) : 22000;
  const uint32_t initialSpeed = getenv("INITIAL_SPEED") ? (uint32_t)strtoul(getenv("INITIAL_SPEED"), NULL, 10) : speed;
  o4db_set_speed(initialSpeed);
  if (!o4db_setup(argv[1], argc > 6 ? argv[6] : "mt32", argc > 7 ? argv[7] : "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  FILE *out = fopen(argv[4], "w");
  printHashes = getenv("HASHES") != NULL;
  pokes_load();
  o4db_set_frame_hook(frame_hook, NULL);
  o4db_new(&init[0]);
  write_row(out, &init[0]);
  const uint64_t f0 = ni > 1 ? o4db_run(init + 1, ni - 1) : 0;
  for (size_t k = 1; k < ni; k++) write_row(out, &init[k]);
  for (uint64_t k = ni > 1 ? ni - 1 : 0; k < f0; k++) write_row(out, &init[ni - 1]);  // (held to the stop)
  if (initialSpeed != speed) o4db_test_set_cycles(speed);
  o4db_set_step_function(0x25BB0202, maxFrames);
  uint64_t frames = f0;
  const int fastCheck = getenv("FASTCHECK") != NULL;
  void *start = NULL;
  uint64_t *passed = calloc(ns ? ns : 1, sizeof *passed);
  if (fastCheck)
  {
    start = malloc(o4db_state_size());
    o4db_save(start);
    o4db_test_hooks(event_hook, 0);
    checking = 0;
  }
  const int trace = getenv("TRACE") != NULL;
  for (size_t k = 0; k < ns; k++)
  {
    stepStart = nEvents[0];
    const uint64_t f = o4db_step(&steps[k]);
    passed[k] = f;
    if (trace)
    {
      const uint8_t *ds = o4db_memory() + ((uint32_t)o4db_data_segment() << 4);
#define W(o) ((int)(int16_t)(ds[(o)] | ds[(o) + 1] << 8))
#define UW(o) ((unsigned)(uint16_t)(ds[(o)] | ds[(o) + 1] << 8))
      char keys[64] = "";
      int n = 0;
      for (int b = 0; b < 57; b++) if ((steps[k].buttons >> b) & 1) keys[n++] = k_mnemonics[b];
      keys[n] = 0;
      fprintf(stderr, "%4zu %-3s f%2llu clk %6u dist %5d | blue (%5d,%5d) hd %4u sta %5d punch %04X ph %3u min %6d req %04X busy %u/%u/%u | red (%5d,%5d) hd %4u hp %5d sta %5d down %u guard %u | thr %u land %u\n",
              k, n ? keys : "-", (unsigned long long)f, UW(0xCF02) | UW(0xCF04) << 16, W(0xCF54), W(0xBA18), W(0xBA1C), UW(0xBA16), W(0xBA20),
              UW(0xBA42), UW(0xBA12 + 0x34) ? (unsigned)ds[UW(0xBA12 + 0x34) + 0x10] : 0u, W(0xBA40), UW(0xB136), UW(0xBA12 + 0x26), UW(0xBA12 + 0x28), UW(0xBA12 + 0x5C),
              W(0xBB0C), W(0xBB10), UW(0xBB0A), W(0xBB16), W(0xBB14), UW(0xBB12), UW(0xBB46), UW(0xA48E), UW(0xA490));
    }
    for (uint64_t j = 0; j < f; j++) write_row(out, &steps[k]);
    frames += f;
  }
  fclose(out);
  if (fastCheck)
  {
    // the same steps as the fast fight: the same boxers' steps, the same frames a step
    checking = 1;
    o4db_load(start);
    o4db_set_fast(1);
    int bad = 0;
    for (size_t k = 0; k < ns && !bad; k++)
    {
      stepStart = nEvents[1];
      const uint64_t f = o4db_step(&steps[k]);
      if (f != passed[k]) { fprintf(stderr, "o4dbSteps2Rows: step %zu passes %llu frames as the fast fight, %llu as the game\n", k, (unsigned long long)f, (unsigned long long)passed[k]); bad = 1; }
    }
    o4db_set_fast(0);
    checking = -1;
    o4db_test_hooks(NULL, 0);
    const size_t common = nEvents[0] < nEvents[1] ? nEvents[0] : nEvents[1];
    for (size_t k = 0; k < common && !bad; k++)
      if (events[0][k].t != events[1][k].t || memcmp(events[0][k].bytes, events[1][k].bytes, events[0][k].n))
      {
        fprintf(stderr, "o4dbSteps2Rows: boxer step %zu (at %d ms in the game) differs as the fast fight\n", k, events[0][k].t);
        bad = 1;
      }
    if (!bad && nEvents[0] != nEvents[1]) { fprintf(stderr, "o4dbSteps2Rows: %zu boxers' steps in the game, %zu as the fast fight\n", nEvents[0], nEvents[1]); bad = 1; }
    fprintf(stderr, "o4dbSteps2Rows: the fast fight %s the game (%zu boxers' steps, %llu replays)\n", bad ? "DIFFERS FROM" : "is", nEvents[0], (unsigned long long)o4db_fast_replays());
    if (bad) return 3;
    // (the game's machine again, for the hash below)
    o4db_load(start);
    for (size_t k = 0; k < ns; k++) o4db_step(&steps[k]);
  }
  uint64_t h = hash_bytes(o4db_memory(), 0xB0000, 0xcbf29ce484222325ull);
  h = hash_bytes(o4db_registers(), 24, h);
  fprintf(getenv("HASHES") ? stderr : stdout, "%zu initial rows (%llu frames), %zu steps, %llu frames in all; rows %llu; hash %016llx\n", ni, (unsigned long long)f0, ns,
         (unsigned long long)frames, (unsigned long long)(frames + 1), (unsigned long long)h);
  return 0;
}
