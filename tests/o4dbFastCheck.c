// The fast fight against the game: a movie played to a frame (at its speed), the machine set to another (CYCLES,
// default 220,000), saved; from there the same steps (the controls' reads; inputs drawn by a seed from the fight's:
// moves, punches held) run twice, as the game (drawn) and as the fast fight (o4db_set_fast), each logging every
// boxer's step (1eb6:3354: its boxer, its time, the boxers but their timers, their statistics, the random number, the
// controls' replay) and every step's end; the logs compared:
//   o4dbFastCheck GAMEDIR MOVIE FROM STEPS [SEED]   (the environment's POKES: pokes.h, during the movie; SAVE: the
//   start saved to that file)
// (o4dbFastCheckTrace, on the trace variant: each miss shown with the instruction that last wrote the word it read)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "machine.h"
#include "movie.h"
#include "pokes.h"

#define LOGIC 1024
typedef struct { int boxer; int32_t t; uint8_t bytes[LOGIC]; size_t n; uint64_t frames, passed; uint16_t elapsed, ticks, hw; size_t step; char summary[160]; } Event;
static size_t curStep;
static Event *logs[2];
static size_t nLog[2], cap[2], stepStart;
static int run = -1;
// (and the frames' times taken, 25bb:063e, the game's time paused and resumed, 25bb:018e and 25bb:0164, the misses'
// garbage, 1eb6:1e84: shown near a difference)
typedef struct { size_t step; uint32_t fn; uint16_t v[8]; size_t writes; } Aux;
#define AUX_STEPS (getenv("AUX_STEPS") ? atoi(getenv("AUX_STEPS")) : 3)
static Aux *aux[2];
static size_t nAux[2], capAux[2], auxStart;

static const uint8_t *ds(void) { return o4db_memory() + ((uint32_t)o4db_data_segment() << 4); }
static uint16_t dw(uint16_t off) { const uint8_t *d = ds(); return (uint16_t)(d[off] | d[off + 1] << 8); }
static void add(Event *e, uint16_t off, size_t size) { memcpy(e->bytes + e->n, ds() + off, size); e->n += size; }
static void logic(Event *e)
{
  e->n = 0;
  add(e, 0xBA12, 0x58); add(e, 0xBA12 + 0x5A, 0xF4 - 0x5A);
  add(e, 0xBB06, 0x58); add(e, 0xBB06 + 0x5A, 0xF4 - 0x5A);
  add(e, 0xA488, 0x74); add(e, 0xA4FC, 0x74);
  add(e, 0x8472, 4); add(e, 0x8CC8, 16);
}

static Event *next_event(void)
{
  if (nLog[run] == cap[run]) logs[run] = realloc(logs[run], (cap[run] = cap[run] ? 2 * cap[run] : 4096) * sizeof(Event));
  return &logs[run][nLog[run]++];
}

#ifdef FASTCHECK_TRACE
// the stack segment's last writers (CS:IP, from the trace variant's write hook)
static uint32_t stackSeg, lastWriter[0x10000];
// (the environment's WATCH, a stack offset: its writes logged, from the step before WATCHSTEP on)
typedef struct { size_t step; uint32_t ip; uint16_t value, ticks; uint64_t frames; } Write;
static Write *watched[2];
static size_t nWatched[2], capWatched[2];
static long watchOff = -1, watchStep;
static void write_hook(uint32_t addr, uint16_t value, int size)
{
  const uint32_t off = addr - (stackSeg << 4);
  if (off >= 0x10000) return;
  const uint32_t ip = o4db_test_trace_ip();
  lastWriter[off] = ip;
  if (size == 2) lastWriter[(off + 1) & 0xFFFF] = ip;
  if (run >= 0 && (long)curStep + 1 >= watchStep && (off == (uint32_t)watchOff || (size == 2 && off + 1 == (uint32_t)watchOff)))
  {
    if (nWatched[run] == capWatched[run]) watched[run] = realloc(watched[run], (capWatched[run] = capWatched[run] ? 2 * capWatched[run] : 4096) * sizeof(Write));
    watched[run][nWatched[run]++] = (Write){ curStep, ip, value, dw(0xCF4A), o4db_frames() };
    if (nAux[run] == capAux[run]) aux[run] = realloc(aux[run], (capAux[run] = capAux[run] ? 2 * capAux[run] : 4096) * sizeof(Aux));
    aux[run][nAux[run]++] = (Aux){ curStep, 0x57524954, { value, (uint16_t)(ip >> 16), (uint16_t)ip, dw(0xCF4A) }, 0 };
  }
}
#endif

// (the environment's DUMPSTEP: the data segment at that step's first frame with a tick, written to DUMPDIR/ds-<run>.bin)
static long dumpStep = -1;
static int dumped[2];
static void aux_event(uint32_t fn)
{
  const uint16_t *r = (const uint16_t *)o4db_registers();
  const uint8_t *m = o4db_memory();
  if (fn == 0x255002AC && (long)curStep == dumpStep && !dumped[run] && dw(0xCEF6))
  {
    char name[512];
    snprintf(name, sizeof name, "%s/ds-%d.bin", getenv("DUMPDIR") ? getenv("DUMPDIR") : ".", run);
    FILE *f = fopen(name, "wb");
    if (f) fwrite(ds(), 1, 0x10000, f), fclose(f);
    dumped[run] = 1;
  }
  if (nAux[run] == capAux[run]) aux[run] = realloc(aux[run], (capAux[run] = capAux[run] ? 2 * capAux[run] : 4096) * sizeof(Aux));
  Aux *a = &aux[run][nAux[run]++];
  a->step = curStep;
  a->fn = fn;
  if (fn == 0x255002AC || fn == 0x1EB63624)
  {
    const uint32_t sp = ((uint32_t)r[10] << 4) + r[7];
    a->v[0] = (uint16_t)(m[sp] | m[sp + 1] << 8); a->v[1] = (uint16_t)(m[sp + 2] | m[sp + 3] << 8); a->v[2] = dw(0xCEF6); a->v[3] = dw(0xCF4A); a->v[4] = dw(0x690E);
  }
  else if (fn == 0x1EB61E84)
  {
    const uint32_t at = ((uint32_t)r[10] << 4) + (uint16_t)(r[6] - 0x12);
    a->v[0] = (uint16_t)(m[at] | m[at + 1] << 8); a->v[1] = r[6]; a->v[2] = r[7]; a->v[3] = dw(0xCF4A); a->v[4] = dw(0x690E); a->v[5] = dw(0xCEF6);
#ifdef FASTCHECK_TRACE
    const uint32_t w = lastWriter[(uint16_t)(r[6] - 0x12)];
    a->v[6] = (uint16_t)(w >> 16); a->v[7] = (uint16_t)w;
    a->writes = nWatched[run];
#endif
  }
  else
  {
    a->v[0] = dw(0xCEF6); a->v[1] = dw(0xCC58); a->v[2] = dw(0xA478); a->v[3] = dw(0x6240); a->v[4] = dw(0xC700); a->v[5] = dw(0xC89C);
    a->v[6] = dw(0xCF4A); a->v[7] = dw(0x690E);
  }
}

static void event_hook(uint32_t fn)
{
  if (run < 0) return;
  if (fn == 0xFFFFFFFF) { nLog[run] = stepStart; nAux[run] = auxStart; return; }  // (the fast fight runs the step again)
  if (fn == 0x25BB063E || fn == 0x25BB018E || fn == 0x25BB0164 || fn == 0x1EB61E84 || fn == 0x255002AC || fn == 0x1EB63624) { aux_event(fn); return; }
  if (fn != 0x1EB63354) return;
  const uint16_t *r = (const uint16_t *)o4db_registers();
  const uint8_t *m = o4db_memory();
  const uint32_t sp = ((uint32_t)r[10] << 4) + r[7];
  Event *e = next_event();
  e->step = curStep;
  e->boxer = m[sp + 4] | m[sp + 5] << 8;
  e->t = (int32_t)(dw(0xCC58) | (uint32_t)dw(0xCC5A) << 16) + (int16_t)dw((uint16_t)((e->boxer ? 0xBB06 : 0xBA12) + 0x58));
  logic(e);
}

static void frame_hook(uint64_t frame, void *ctx) { (void)ctx; if (run < 0) pokes_apply(frame); }

static uint64_t rnd = 88172645463325252ull;
static unsigned next_rand(void) { rnd ^= rnd << 13; rnd ^= rnd >> 7; rnd ^= rnd << 17; return (unsigned)(rnd >> 33); }

int main(int argc, char **argv)
{
  if (argc < 5) { fprintf(stderr, "usage: o4dbFastCheck GAMEDIR MOVIE FROM STEPS [SEED]\n"); return 2; }
  size_t n;
  MovieInput *rows = movie_load(argv[2], &n);
  if (!rows) return 1;
  const uint64_t from = strtoull(argv[3], NULL, 10);
  const size_t steps = strtoull(argv[4], NULL, 10);
  if (argc > 5) rnd ^= strtoull(argv[5], NULL, 10) * 0x9E3779B97F4A7C15ull;
  if (!o4db_setup(argv[1], "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  O4dbInput *in = malloc(n * sizeof *in);
  for (size_t k = 0; k < n; k++) in[k] = (O4dbInput){ rows[k].buttons, rows[k].mouseX, rows[k].mouseY };
  pokes_load();
  o4db_set_frame_hook(frame_hook, NULL);
  o4db_new(&in[0]);
  o4db_run_ahead(in + 1, from, n - 1);
  o4db_test_set_cycles(getenv("CYCLES") ? (uint32_t)strtoul(getenv("CYCLES"), NULL, 10) : 220000);
  o4db_set_step_function(0x25BB0202, 8);
  void *state = malloc(o4db_state_size());
  o4db_save(state);
  if (getenv("SAVE"))
  {
    // (the start, for benchmarks: o4dbBench's state, its speed already the fast fight's)
    FILE *f = fopen(getenv("SAVE"), "wb");
    if (f) fwrite(state, 1, o4db_state_size(), f), fclose(f);
  }
  // the steps' inputs: a direction of the keypad's (or none), Enter or not, kept a while
  static const int dirs[9] = { -1, 2, 3, 4, 5, 6, 7, 8, 9 };
  O4dbInput *stepIn = malloc(steps * sizeof *stepIn);
  uint64_t held = 0;
  for (size_t k = 0; k < steps; k++)
  {
    if (k == 0 || next_rand() % 10 < 6)
    {
      const int d = dirs[next_rand() % 9];
      held = (d >= 0 ? 1ull << d : 0) | (next_rand() % 2 ? 1ull << 12 : 0);
    }
    stepIn[k] = (O4dbInput){ held, 135, 65 };
  }
  Event *ends[2] = { malloc(steps * sizeof(Event)), malloc(steps * sizeof(Event)) };
  double seconds[2];
  o4db_test_hooks(event_hook, 0);
  if (getenv("DUMPSTEP")) dumpStep = atol(getenv("DUMPSTEP"));
#ifdef FASTCHECK_TRACE
  stackSeg = ((const uint16_t *)o4db_registers())[10];
  o4db_test_trace_writes(write_hook);
  if (getenv("WATCH")) watchOff = strtol(getenv("WATCH"), NULL, 16), watchStep = getenv("WATCHSTEP") ? atol(getenv("WATCHSTEP")) : 0;
#endif
  // (the environment's ONLYFAST: the fast fight alone, for a profile)
  for (run = getenv("ONLYFAST") ? 1 : 0; run < 2; run++)
  {
    o4db_load(state);
#ifdef FASTCHECK_TRACE
    memset(lastWriter, 0, sizeof lastWriter);
#endif
    o4db_set_fast(run);
    // (the environment's FASTSKIP: o4db_test_hooks's parts skipped in the fast fight)
    o4db_test_hooks(event_hook, run && getenv("FASTSKIP") ? (unsigned)strtoul(getenv("FASTSKIP"), NULL, 0) : 0);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (size_t k = 0; k < steps; k++)
    {
      stepStart = nLog[run];
      auxStart = nAux[run];
      curStep = k;
      const uint64_t passed = o4db_step(&stepIn[k]);
      snprintf(ends[run][k].summary, sizeof ends[run][k].summary, "phase %u over %u/%u blue down %u kd %u hp %6d, red down %u kd %u hp %6d, count %2u, 9754 %u, 6234 %u",
               dw(0xB100), dw(0xCF0A), dw(0xCF0C), dw(0xBA12 + 0xC), dw(0xA488 + 0xA), (int16_t)dw(0xBA12 + 0x10), dw(0xBB06 + 0xC), dw(0xA4FC + 0xA),
               (int16_t)dw(0xBB06 + 0x10), dw(0xCF12), dw(0x9754), dw(0x6234));
      logic(&ends[run][k]);
      ends[run][k].t = (int32_t)(dw(0xCC58) | (uint32_t)dw(0xCC5A) << 16);
      ends[run][k].frames = o4db_frames();
      ends[run][k].passed = passed;
      ends[run][k].elapsed = dw(0xCEF6);
      ends[run][k].ticks = dw(0xCF4A);
      ends[run][k].hw = dw(0x690E);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    seconds[run] = (double)(t1.tv_sec - t0.tv_sec) + 1e-9 * (double)(t1.tv_nsec - t0.tv_nsec);
    o4db_set_fast(0);
  }
  o4db_test_hooks(NULL, 0);
  if (getenv("ONLYFAST")) { printf("%zu steps: the fast fight %.3f s (%.3f ms a step, %llu replays)\n", steps, seconds[1], seconds[1] * 1000.0 / (double)steps, (unsigned long long)o4db_fast_replays()); return 0; }
  printf("%zu steps: the game %.3f s (%zu boxer steps), the fast fight %.3f s (%zu boxer steps, %llu replays): %.1fx\n", steps, seconds[0], nLog[0], seconds[1], nLog[1],
         (unsigned long long)o4db_fast_replays(), seconds[0] / seconds[1]);
  // the misses' garbage (1eb6:1e84's reads), the game's and the fast fight's, in their order
  {
    size_t i = 0, j = 0, n = 0, same = 0;
    for (;;)
    {
      while (i < nAux[0] && aux[0][i].fn != 0x1EB61E84) i++;
      while (j < nAux[1] && aux[1][j].fn != 0x1EB61E84) j++;
      if (i >= nAux[0] || j >= nAux[1]) break;
      n++;
      if (aux[0][i].v[0] == aux[1][j].v[0]) same++;
      else if (getenv("MISSES")) printf("miss %zu (steps %zu, %zu): the game's %04X, the fast fight's %04X\n", n, aux[0][i].step, aux[1][j].step, aux[0][i].v[0], aux[1][j].v[0]);
      i++, j++;
    }
    printf("misses: %zu, their garbage the same: %zu\n", n, same);
  }
  const size_t common = nLog[0] < nLog[1] ? nLog[0] : nLog[1];
  for (size_t k = 0; k < common; k++)
  {
    const Event *a = &logs[0][k], *b = &logs[1][k];
    if (a->boxer == b->boxer && a->t == b->t && !memcmp(a->bytes, b->bytes, a->n)) continue;
    printf("boxer step %zu differs: the game's boxer %d at %d ms, the fast fight's boxer %d at %d ms (steps %zu, %zu)\n", k, a->boxer, a->t, b->boxer, b->t, a->step, b->step);
    for (size_t j = a->step >= 8 ? a->step - 8 : 0; j <= a->step + 1 && j < steps; j++)
      for (int r = 0; r < 2; r++)
        printf("  step %zu %-10s: %6d ms, frame %llu (%llu passed), ticks %u: %s\n", j, r ? "fast fight" : "game", ends[r][j].t, (unsigned long long)ends[r][j].frames,
               (unsigned long long)ends[r][j].passed, ends[r][j].ticks, ends[r][j].summary);
    for (size_t j = k >= 14 ? k - 14 : 0; j < k + 10 && j < common; j++)
      printf("  %s step %4zu: the game's boxer %d at %6d ms, the fast fight's boxer %d at %6d ms\n", j == k ? ">>" : "  ", j, logs[0][j].boxer, logs[0][j].t,
             logs[1][j].boxer, logs[1][j].t);
    int shown = 0;
    for (size_t i = 0; i < a->n && shown < 24; i++)
      if (a->bytes[i] != b->bytes[i]) { printf("  byte %4zu: %02X %02X\n", i, a->bytes[i], b->bytes[i]); shown++; }
    const size_t from = a->step >= (size_t)AUX_STEPS ? a->step - (size_t)AUX_STEPS : 0;
    for (int r = 0; r < 2; r++)
    {
      printf("  %s, steps %zu-%zu (frames with no time taken: counted):\n", r ? "the fast fight" : "the game", from, a->step + 1);
      unsigned idle = 0;
      for (size_t j = 0; j < nAux[r]; j++)
      {
        const Aux *x = &aux[r][j];
        if (x->step < from || x->step > a->step + 1) continue;
        if (x->fn == 0x25BB063E && x->v[0] == 0) { idle++; continue; }
        // (a frame's call: shown when it is not the round's loop's, or it has time)
        if (x->fn == 0x255002AC && x->v[0] == 0x29B9 && x->v[2] == 0 && !getenv("ALLFRAMES")) continue;
        if (idle) printf("    (%u frames with no time)\n", idle), idle = 0;
        if (x->fn == 0x57524954)
          printf("    step %zu write %04X at %04X:%04X, ticks %u\n", x->step, x->v[0], x->v[1], x->v[2], x->v[3]);
        else if (x->fn == 0x255002AC || x->fn == 0x1EB63624)
          printf("    step %zu %s from %04X:%04X, cef6 %u, ticks %u, interrupts %u\n", x->step, x->fn == 0x255002AC ? "frame" : "logic", x->v[1], x->v[0], x->v[2], x->v[3], x->v[4]);
        else if (x->fn == 0x1EB61E84)
        {
          printf("    step %zu miss: [bp-12h] %04X (bp %04X sp %04X), ticks %u, interrupts %u, cef6 %u, written at %04X:%04X\n", x->step, x->v[0], x->v[1], x->v[2], x->v[3],
                 x->v[4], x->v[5], x->v[6], x->v[7]);
#ifdef FASTCHECK_TRACE
          for (size_t j = x->writes >= 16 ? x->writes - 16 : 0; watchOff >= 0 && j < x->writes; j++)
            printf("      write: step %zu frame %llu ticks %u: %04X at %04X:%04X\n", watched[r][j].step, (unsigned long long)watched[r][j].frames, watched[r][j].ticks,
                   watched[r][j].value, watched[r][j].ip >> 16, watched[r][j].ip & 0xFFFF);
#endif
        }
        else
          printf("    step %zu %s: cef6 %u cc58 %u a478 %u 6240 %u c700 %u c89c %u ticks %u interrupts %u\n", x->step,
                 x->fn == 0x25BB063E ? "frame time" : x->fn == 0x25BB018E ? "time paused" : "time resumed",
                 x->v[0], x->v[1], x->v[2], x->v[3], x->v[4], x->v[5], x->v[6], x->v[7]);
      }
    }
#ifdef FASTCHECK_TRACE
#endif
    return 1;
  }
  // the steps' ends: the same frame ends passed (a solution's rows), the same ticks and timer interrupts (the game may
  // stop a little later in the tick's frame, its interrupt where its drawing put it: the logic's bytes then differ by
  // that frame's start, and its steps are the same)
  size_t later = 0;
  for (size_t k = 0; k < steps; k++)
    if (ends[0][k].t != ends[1][k].t || memcmp(ends[0][k].bytes, ends[1][k].bytes, ends[0][k].n)) later++;
  for (size_t k = 0; k < steps; k++)
    if (ends[0][k].frames != ends[1][k].frames || ends[0][k].passed != ends[1][k].passed || ends[0][k].ticks != ends[1][k].ticks || ends[0][k].hw != ends[1][k].hw)
    {
      printf("step %zu's end differs (the game at %d ms, the fast fight at %d ms)\n", k, ends[0][k].t, ends[1][k].t);
      for (size_t j = k >= 4 ? k - 4 : 0; j < k + 3 && j < steps; j++)
        for (int r = 0; r < 2; r++)
          printf("  %s step %zu %-10s: %d ms, frame %llu (%llu passed), elapsed %u, ticks %u, interrupts %u\n", j == k ? ">>" : "  ", j, r ? "fast fight" : "game",
                 ends[r][j].t, (unsigned long long)ends[r][j].frames, (unsigned long long)ends[r][j].passed, ends[r][j].elapsed, ends[r][j].ticks, ends[r][j].hw);
      return 1;
    }
  printf("the same: %zu boxer steps, %zu steps' frames and ticks (the last at %d ms; %zu ends a little later in the game's tick)\n", common, steps,
         steps ? ends[0][steps - 1].t : 0, later);
  if (steps) printf("the last step: %s\n", ends[0][steps - 1].summary);
  return nLog[0] == nLog[1] ? 0 : 1;
}
