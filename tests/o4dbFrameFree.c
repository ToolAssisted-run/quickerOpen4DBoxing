// Whether the fight's logic depends on when the frames come (the drawing's time): a movie played to a frame and
// saved; from there played on twice, as it is and with the fight's scene not drawn (1c47:229c skipped: the frames
// come at other times), each logging the fight's events (a boxer's step, 1eb6:3354): the boxer, the game's time of it
// (the time passed, DS:CC58, and the boxer's timer) and the logic's bytes (the boxers but their timers, their
// statistics, the random number, the controls' replay read pointers, counts and timers); the logs compared:
//   o4dbFrameFree GAMEDIR MOVIE FROM FRAMES [SKIP]   (SKIP: o4db_test_hooks's, default 1; the environment's POKES:
//   from FROM on given at the first frame's fight after their frame, where both runs are at the same tick,
//   CYCLES: the machine's speed from FROM on, LONG: the frames of more than a tick listed, CHAIN=A-B: the call chain at
//   those frames' ends, DUMP=K: the data segment at event K's entry into ds-event-drawn.bin and ds-event-undrawn.bin, READS=K:
//   the program's reads from event K's entry to the next event's or frame's compared, the first that differs shown,
//   WATCHIP=SEG:OFF: every read of the instruction at Ghidra's SEG:OFF listed, SLOT=OFF: the data segment's word
//   there at the frames' starts counted by value, MIMIC=SEG:OFF: that instruction's reads in the run not drawn given
//   the drawn run's values, read by read)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include "machine.h"
#include "movie.h"
#include "pokes.h"

#define LOGIC 1024
static uint64_t pokesDone, pokesDue;  // (the pokes of the frames up to pokesDue are given at the next frame's fight)
typedef struct { int boxer; int32_t t; uint8_t bytes[LOGIC]; size_t n; uint16_t rec, in, rp, wp; uint64_t frames; uint8_t pose[2 * 0x8A]; } Event;
static Event *logs[2];
static size_t nLog[2], cap[2];
static int run;
static uint64_t f0 = UINT64_MAX;

static const uint8_t *ds(void) { return o4db_memory() + ((uint32_t)o4db_data_segment() << 4); }
static uint16_t dw(uint16_t off) { const uint8_t *d = ds(); return (uint16_t)(d[off] | d[off + 1] << 8); }

static void add(Event *e, uint16_t off, size_t size) { memcpy(e->bytes + e->n, ds() + off, size); e->n += size; }

typedef struct { uint32_t addr, ip; uint16_t value; uint8_t size; } Read;
static Read *reads[2];
static size_t nReads[2], capReads[2];
static int tracing;
static long long traceEvent = -1;
// (MIMIC=SEG:OFF: the instruction there reads in the run not drawn what it read in the drawn run, read by read)
static uint32_t mimicIp;
static uint16_t mimicValues[65536];
static size_t nMimic[2];
static void pre_read_hook(uint32_t addr, int size)
{
  if (!mimicIp || run < 0 || size != 2) return;
  const uint32_t ip = o4db_test_trace_ip();
  if (((((ip >> 16) + 0x1000 - 0x823) & 0xFFFF) << 16 | (ip & 0xFFFF)) != mimicIp) return;
  uint8_t *m = o4db_memory();
  if (run == 0) { if (nMimic[0] < 65536) mimicValues[nMimic[0]++] = (uint16_t)(m[addr] | m[addr + 1] << 8); return; }
  if (nMimic[1] < nMimic[0]) { const uint16_t v = mimicValues[nMimic[1]++]; m[addr] = (uint8_t)v; m[addr + 1] = (uint8_t)(v >> 8); }
}
static uint32_t watchIp;  // (WATCHIP: the reads of the instruction there listed, Ghidra's CS:IP)
static void read_hook(uint32_t addr, uint16_t value, int size)
{
  if (watchIp && run >= 0)
  {
    const uint32_t ip = o4db_test_trace_ip();
    if (((((ip >> 16) + 0x1000 - 0x823) & 0xFFFF) << 16 | (ip & 0xFFFF)) == watchIp)
      printf("  (%s, event %zu, frame %llu: [%05X] = %04X)\n", run ? "not drawn" : "drawn", nLog[run], (unsigned long long)o4db_frames(), addr, value);
  }
  if (!tracing || run < 0) return;
  if (nReads[run] == capReads[run]) reads[run] = realloc(reads[run], (capReads[run] = capReads[run] ? 2 * capReads[run] : 65536) * sizeof(Read));
  reads[run][nReads[run]++] = (Read){ addr, o4db_test_trace_ip(), value, (uint8_t)size };
}

static size_t firstLong[2];
static uint16_t slotOff;
// (SLOTWRITERS: the last writer of the slot in each frame, counted by its CS:IP)
typedef struct { uint32_t ip; unsigned n; int16_t minV, maxV; } Writer;
static Writer writers[256];
static int nWriters;
static uint32_t lastWriterIp;
static int16_t lastWriterValue;
static int sawWrite;
static void write_hook(uint32_t addr, uint16_t value, int size)
{
  const uint32_t slot = ((uint32_t)o4db_data_segment() << 4) + slotOff;
  if (run != 0 || addr + (uint32_t)size <= slot || addr > slot + 1) return;
  const uint32_t ip = o4db_test_trace_ip();
  lastWriterIp = ((((ip >> 16) + 0x1000 - 0x823) & 0xFFFF) << 16) | (ip & 0xFFFF);
  lastWriterValue = (int16_t)dw(slotOff);
  sawWrite = 1;
}
typedef struct { int16_t v; unsigned n; } SlotValue;
static SlotValue slotValues[2][4096];
static int nSlotValues[2];
static unsigned tickHist[2][8];
static int frameBetween[2][1 << 16];  // (an event index's game frames before it, since the event before)

static void event_hook(uint32_t fn)
{
  if (run < 0) return;
  tracing = fn == 0x1EB63354 && (long long)nLog[run] == traceEvent;
  if (fn == 0x1EB63624)
  {
    while (pokesDone < pokesDue) pokes_apply(++pokesDone);
    if (nLog[run] < (1 << 16)) frameBetween[run][nLog[run]]++;
    // (SLOT=OFF: the data segment's word there at each frame's start, counted by value)
    if (slotOff && run == 0 && sawWrite)
    {
      int k = 0;
      while (k < nWriters && writers[k].ip != lastWriterIp) k++;
      if (k == nWriters && k < 256) writers[nWriters++] = (Writer){ lastWriterIp, 0, lastWriterValue, lastWriterValue };
      if (k < 256)
      {
        writers[k].n++;
        if (lastWriterValue < writers[k].minV) writers[k].minV = lastWriterValue;
        if (lastWriterValue > writers[k].maxV) writers[k].maxV = lastWriterValue;
      }
    }
    if (slotOff)
    {
      const int16_t v = (int16_t)dw(slotOff);
      int k = 0;
      while (k < nSlotValues[run] && slotValues[run][k].v != v) k++;
      if (k == nSlotValues[run] && k < 4096) slotValues[run][nSlotValues[run]++] = (SlotValue){ v, 0 };
      if (k < 4096) slotValues[run][k].n++;
    }
    // (the time the frame's fight takes: the timer's 10 ms ticks since the last frame, DS:CEF6)
    const unsigned ticks = dw(0xCEF6) / 10;
    if (ticks < 8) tickHist[run][ticks]++;
    if (ticks >= 2 && !firstLong[run]) firstLong[run] = nLog[run] + 1;
    if (ticks >= 2 && getenv("LONG")) printf("  (%s: a frame of %u ticks before event %zu, at frame %llu)\n", run ? "not drawn" : "drawn", ticks, nLog[run],
                                             (unsigned long long)o4db_frames());
    return;
  }
  const uint16_t *r = (const uint16_t *)o4db_registers();
  const uint8_t *m = o4db_memory();
  const uint32_t sp = ((uint32_t)r[10] << 4) + r[7];
  if (nLog[run] == cap[run]) logs[run] = realloc(logs[run], (cap[run] = cap[run] ? 2 * cap[run] : 4096) * sizeof(Event));
  Event *e = &logs[run][nLog[run]++];
  e->boxer = m[sp + 4] | m[sp + 5] << 8;
  const uint16_t b = e->boxer ? 0xBB06 : 0xBA12;
  e->t = (int32_t)(dw(0xCC58) | (uint32_t)dw(0xCC5A) << 16) + (int16_t)dw(b + 0x58);
  e->n = 0;
  add(e, 0xBA12, 0x58); add(e, 0xBA12 + 0x5A, 0xF4 - 0x5A);
  add(e, 0xBB06, 0x58); add(e, 0xBB06 + 0x5A, 0xF4 - 0x5A);
  add(e, 0xA488, 0x74); add(e, 0xA4FC, 0x74);
  add(e, 0x8472, 4); add(e, 0x8CC8, 16);
  e->rec = dw(0x6240);
  e->in = dw(0x8A40);
  e->rp = dw(e->boxer ? 0x8CD0 : 0x8CC8);
  e->wp = dw(e->boxer ? 0x8E42 : 0x8E3E);
  e->frames = o4db_frames();
  if (getenv("DUMP") && nLog[run] - 1 == strtoull(getenv("DUMP"), NULL, 10))
  {
    char name[64];
    snprintf(name, sizeof name, "ds-event-%s.bin", run ? "undrawn" : "drawn");
    FILE *f = fopen(name, "wb");
    fwrite(ds(), 1, 0x10000, f);
    fclose(f);
  }
  memcpy(e->pose, ds() + 0x9566, 0x8A);
  memcpy(e->pose + 0x8A, ds() + 0x9640, 0x8A);
}

static uint64_t chainFrom = UINT64_MAX, chainTo;
static void frame_hook(uint64_t frame, void *ctx)
{
  (void)ctx;
  if (run < 0) pokes_apply(frame), pokesDone = frame;
  else pokesDue = frame;
  if (run == 0 && frame >= chainFrom && frame <= chainTo)
  {
    int depth;
    const uint32_t *c = o4db_test_chain(&depth);
    printf("  frame %llu (cef6 %u): chain", (unsigned long long)frame, dw(0xCEF6));
    for (int k = 0; k < depth; k++) printf(" %u", c[2 * k]);
    printf("\n");
  }
}

static const char *field(size_t k)
{
  static char s[64];
  static const struct { const char *name; size_t size; uint16_t base; } parts[] = {
    { "blue", 0x58, 0 }, { "blue", 0xF4 - 0x5A, 0x5A }, { "red", 0x58, 0 }, { "red", 0xF4 - 0x5A, 0x5A },
    { "blue stats", 0x74, 0 }, { "red stats", 0x74, 0 }, { "random", 4, 0 }, { "controls 8CC8", 16, 0 } };
  for (size_t p = 0; p < sizeof parts / sizeof *parts; p++)
  {
    if (k < parts[p].size) { snprintf(s, sizeof s, "%s +0x%02zX", parts[p].name, parts[p].base + k); return s; }
    k -= parts[p].size;
  }
  return "?";
}

int main(int argc, char **argv)
{
  if (argc < 5) { fprintf(stderr, "usage: o4dbFrameFree GAMEDIR MOVIE FROM FRAMES [SKIP]\n"); return 2; }
  size_t n;
  MovieInput *rows = movie_load(argv[2], &n);
  if (!rows) return 1;
  const uint64_t from = strtoull(argv[3], NULL, 10);
  uint64_t frames = strtoull(argv[4], NULL, 10);
  const unsigned skip = argc > 5 ? (unsigned)strtoul(argv[5], NULL, 0) : 1;
  if (!o4db_setup(argv[1], "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  O4dbInput *in = malloc(n * sizeof *in);
  for (size_t k = 0; k < n; k++) in[k] = (O4dbInput){ rows[k].buttons, rows[k].mouseX, rows[k].mouseY };
  pokes_load();
  if (getenv("READS")) traceEvent = strtoll(getenv("READS"), NULL, 10), o4db_test_trace_reads(read_hook);
  if (getenv("MIMIC")) { unsigned a, b; sscanf(getenv("MIMIC"), "%x:%x", &a, &b); mimicIp = a << 16 | b; o4db_test_trace_pre_reads(pre_read_hook); }
  if (getenv("SLOT")) slotOff = (uint16_t)strtoul(getenv("SLOT"), NULL, 16), o4db_test_trace_writes(write_hook);
  if (getenv("WATCHIP")) { unsigned a, b; sscanf(getenv("WATCHIP"), "%x:%x", &a, &b); watchIp = a << 16 | b; o4db_test_trace_reads(read_hook); }
  if (getenv("CHAIN")) sscanf(getenv("CHAIN"), "%llu-%llu", (unsigned long long *)&chainFrom, (unsigned long long *)&chainTo);
  o4db_set_frame_hook(frame_hook, NULL);
  run = -1;
  o4db_test_hooks(event_hook, 0);
  o4db_new(&in[0]);
  o4db_run_ahead(in + 1, from, n - 1);
  if (getenv("CYCLES")) o4db_test_set_cycles((uint32_t)strtoul(getenv("CYCLES"), NULL, 10));
  f0 = o4db_frames() + 1;
  if (f0 + frames > n) frames = n - f0;
  void *state = malloc(o4db_state_size());
  o4db_save(state);
  const uint64_t pokesAtSave = pokesDone;
  double seconds[2];
  for (run = 0; run < 2; run++)
  {
    pokesDone = pokesDue = pokesAtSave;
    o4db_load(state);
    o4db_test_hooks(event_hook, run ? skip : 0);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    o4db_run_ahead(in + f0, frames, n - f0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    seconds[run] = (double)(t1.tv_sec - t0.tv_sec) + 1e-9 * (double)(t1.tv_nsec - t0.tv_nsec);
  }
  printf("host time: drawn %.3f s, not drawn %.3f s\n", seconds[0], seconds[1]);
  o4db_test_hooks(NULL, 0);
  if (traceEvent >= 0)
  {
    // (the instructions' CS as Ghidra's segments: the program loaded at 0823, Ghidra's image at 1000)
#define GIP(ip) ((((ip) >> 16) + 0x1000 - 0x823) & 0xFFFF), ((ip) & 0xFFFF)
    printf("event %lld's reads: %zu drawn, %zu not drawn\n", traceEvent, nReads[0], nReads[1]);
    const size_t m = nReads[0] < nReads[1] ? nReads[0] : nReads[1];
    for (size_t k = 0; k < m; k++)
    {
      const Read *a = &reads[0][k], *b = &reads[1][k];
      if (a->addr == b->addr && a->value == b->value && a->ip == b->ip) continue;
      for (size_t j = k >= 12 ? k - 12 : 0; j <= k + 4 && j < m; j++)
      {
        const Read *x = &reads[0][j], *y = &reads[1][j];
        printf("  %s read %zu at %04X:%04X: [%05X] = %04X (%d)   not drawn: at %04X:%04X [%05X] = %04X\n", j == k ? ">>" : "  ", j, GIP(x->ip), x->addr, x->value, x->size,
               GIP(y->ip), y->addr, y->value);
      }
      break;
    }
  }
  printf("from frame %llu, %llu frames: %zu events drawn, %zu not drawn\n", (unsigned long long)f0, (unsigned long long)frames, nLog[0], nLog[1]);
  for (int r = 0; r < 2; r++)
    printf("  %s: frames of 0..7 ticks: %u %u %u %u %u %u %u %u; the first of 2 or more ticks before event %zd\n", r ? "not drawn" : "drawn    ", tickHist[r][0], tickHist[r][1],
           tickHist[r][2], tickHist[r][3], tickHist[r][4], tickHist[r][5], tickHist[r][6], tickHist[r][7], (ssize_t)firstLong[r] - 1);
  for (int r = 0; slotOff && r < 2; r++)
  {
    printf("  %s: DS:%04X at the frames' starts:", r ? "not drawn" : "drawn", slotOff);
    for (int k = 0; k < nSlotValues[r] && k < 40; k++) printf(" %d(x%u)", slotValues[r][k].v, slotValues[r][k].n);
    printf("%s\n", nSlotValues[r] > 40 ? " ..." : "");
  }
  for (int k = 0; k < nWriters; k++)
    printf("  drawn: the slot's last writer at a frame's start %04X:%04X x%u (values %d..%d)\n", writers[k].ip >> 16, writers[k].ip & 0xFFFF, writers[k].n, writers[k].minV,
           writers[k].maxV);
  const size_t common = nLog[0] < nLog[1] ? nLog[0] : nLog[1];
  for (size_t k = 0; k < common; k++)
    if (memcmp(logs[0][k].pose, logs[1][k].pose, sizeof logs[0][k].pose))
    {
      printf("event %zu (boxer %d at %d ms): the poses (9566 blue, 9640 red: 23 joints x, y, z) differ first:\n", k, logs[0][k].boxer, logs[0][k].t);
      const int16_t *a = (const int16_t *)logs[0][k].pose, *b = (const int16_t *)logs[1][k].pose;
      for (int j = 0; j < 2 * 23; j++)
        if (memcmp(a + 3 * j, b + 3 * j, 6))
          printf("  %s joint %2d: (%6d %6d %6d) / (%6d %6d %6d)\n", j < 23 ? "blue" : "red ", j % 23, a[3 * j], a[3 * j + 1], a[3 * j + 2], b[3 * j], b[3 * j + 1], b[3 * j + 2]);
      break;
    }
  for (size_t k = 0; k < common; k++)
  {
    const Event *a = &logs[0][k], *b = &logs[1][k];
    if (a->boxer == b->boxer && a->t == b->t && !memcmp(a->bytes, b->bytes, a->n)) continue;
    printf("event %zu differs: drawn boxer %d at %d ms, not drawn boxer %d at %d ms\n", k, a->boxer, a->t, b->boxer, b->t);
    for (size_t j = k >= 6 ? k - 6 : 0; j <= k; j++)
      printf("  event %zu: boxer %d at %d ms, frame %llu/%llu, game frames before it %d/%d, recording %u, 8A40 %04X/%04X, read %04X/%04X written %04X/%04X\n", j,
             logs[0][j].boxer, logs[0][j].t, (unsigned long long)logs[0][j].frames, (unsigned long long)logs[1][j].frames, frameBetween[0][j], frameBetween[1][j],
             logs[0][j].rec, logs[0][j].in, logs[1][j].in, logs[0][j].rp, logs[1][j].rp, logs[0][j].wp, logs[1][j].wp);
    int shown = 0;
    for (size_t i = 0; i < a->n && shown < 40; i++)
      if (a->bytes[i] != b->bytes[i]) { printf("  %-16s %02X %02X\n", field(i), a->bytes[i], b->bytes[i]); shown++; }
    return 1;
  }
  printf("the first %zu events are the same (the last at %d ms)\n", common, common ? logs[0][common - 1].t : 0);
  return 0;
}
