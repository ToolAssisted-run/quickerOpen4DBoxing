// The opponent's decisions against a plan of the player's: o4dbAIWatch GAMEDIR MOVIE PLAN [STEPS]
// MOVIE: the initial sequence's rows (Chimera's), played a row a frame end at the environment's INITIAL_SPEED (22,000 by
// default, as the emulator's "Initial Speed"), its POKES given during them; then the machine at SPEED (220,000), the
// fast fight unless FAST=0, steps of the game's sampling of the controls. PLAN: a line a step, the player's buttons as
// mnemonics (inputParser.hpp: U D L R Q E Z C the keypad's directions, N Enter, ! the right Shift; an empty line or
// "." none; or Chimera's rows, a JaffarPlus solution), the last line held to STEPS. Each step printed: the round's clock, the boxers' distance, the opponent's
// decision (234a:0012's last input, DS:89F9: 1 up 2 down 4 right 8 left, 0x10 the steps' button, 0x20 a punch), his
// punch (+0x28, its definition +0x30), both boxers' places, headings and postures, the player's health, stamina,
// punches thrown and landed, the opponent's health. The environment's STATE: a saved machine (o4dbBench save,
// o4dbBreak SAVE: at SPEED, the machine set up at it) instead of the movie played (its first row still the first input)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "movie.h"
#include "pokes.h"

static const char *mnemonics = "<>UDLRQEZCKON_BXT^!%&abcdefghijklmnopqrstuvwxyz0123456789";

static const uint8_t *ds(void) { return o4db_memory() + ((uint32_t)o4db_data_segment() << 4); }
static uint16_t dw(uint16_t off) { const uint8_t *d = ds(); return (uint16_t)(d[off] | d[off + 1] << 8); }
static int16_t sw(uint16_t off) { return (int16_t)dw(off); }

static void frame_hook(uint64_t frame, void *ctx) { (void)ctx; pokes_apply(frame); }

#ifdef AIWATCH_TRACE
// (o4dbAIWatchTrace, on the trace variant: the writes of the environment's WATCH, data segment offsets "A810,A81C",
// printed with their instructions)
static uint16_t watch[8];
static int nWatch, watching;
static size_t curStep;
static void write_hook(uint32_t addr, uint16_t value, int size)
{
  if (!watching) return;
  const uint32_t off = addr - ((uint32_t)o4db_data_segment() << 4);
  for (int k = 0; k < nWatch; k++)
    if (off == watch[k] || (size == 2 && off + 1 == watch[k]))
    {
      const uint32_t ip = o4db_test_trace_ip();
      printf("      write %04X <- %d at %04X:%04X (step %zu)\n", watch[k], (int16_t)value, ip >> 16, ip & 0xFFFF, curStep);
    }
}
#endif

static O4dbButtons parse(const char *line)
{
  O4dbButtons b = 0;
  if (line[0] == '|')
  {
    // (a Chimera row, a JaffarPlus solution's line: "|  x,  y,<67 buttons>|", or the 57 before the function keys)
    const char *c = strchr(line, ',');
    c = c ? strchr(c + 1, ',') : NULL;
    for (int k = 0; c && k < MOVIE_BUTTONS && c[1 + k] && c[1 + k] != '|'; k++)
      if (c[1 + k] != '.') b |= (O4dbButtons)1 << k;
    return b;
  }
  for (const char *c = line; *c && *c != '\n'; c++)
  {
    const char *k = strchr(mnemonics, *c);
    if (k && *c != '.') b |= (O4dbButtons)1 << (k - mnemonics);
  }
  return b;
}

int main(int argc, char **argv)
{
  if (argc < 4) { fprintf(stderr, "usage: o4dbAIWatch GAMEDIR MOVIE PLAN [STEPS]\n"); return 2; }
  size_t n;
  MovieInput *rows = movie_load(argv[2], &n);
  if (!rows) return 1;
  FILE *pf = fopen(argv[3], "r");
  if (!pf) { fprintf(stderr, "o4dbAIWatch: no plan %s\n", argv[3]); return 1; }
  uint64_t *plan = NULL;
  size_t nPlan = 0, capPlan = 0;
  char line[256];
  while (fgets(line, sizeof line, pf))
  {
    if (nPlan == capPlan) plan = realloc(plan, (capPlan = capPlan ? 2 * capPlan : 256) * sizeof *plan);
    plan[nPlan++] = parse(line);
  }
  fclose(pf);
  if (!nPlan) { fprintf(stderr, "o4dbAIWatch: an empty plan\n"); return 1; }
  const size_t steps = argc > 4 ? strtoull(argv[4], NULL, 10) : nPlan;
  const uint32_t speed = getenv("SPEED") ? (uint32_t)strtoul(getenv("SPEED"), NULL, 10) : 220000;
  o4db_set_speed(getenv("STATE") ? speed : getenv("INITIAL_SPEED") ? (uint32_t)strtoul(getenv("INITIAL_SPEED"), NULL, 10) : 22000);
  if (!o4db_setup(argv[1], "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  O4dbInput *in = malloc(n * sizeof *in);
  for (size_t k = 0; k < n; k++) in[k] = (O4dbInput){ rows[k].buttons, rows[k].mouseX, rows[k].mouseY };
  pokes_load();
  o4db_set_frame_hook(frame_hook, NULL);
  o4db_new(&in[0]);
  if (getenv("STATE"))
  {
    FILE *f = fopen(getenv("STATE"), "rb");
    void *st = malloc(o4db_state_size());
    if (!f || fread(st, 1, o4db_state_size(), f) != o4db_state_size()) { fprintf(stderr, "o4dbAIWatch: no state in %s\n", getenv("STATE")); return 1; }
    fclose(f);
    o4db_load(st);
    free(st);
  }
  else
  {
    o4db_run(in + 1, n - 1);
    o4db_test_set_cycles(speed);
  }
  o4db_set_frame_hook(NULL, NULL);
  o4db_set_step_function(0x25BB0202, 8);
  o4db_set_fast(getenv("FAST") ? atoi(getenv("FAST")) : 1);
#ifdef AIWATCH_TRACE
  if (getenv("WATCH"))
    for (char *w = getenv("WATCH"); *w && nWatch < 8;)
    {
      watch[nWatch++] = (uint16_t)strtoul(w, &w, 16);
      if (*w == ',') w++;
    }
  o4db_test_trace_writes(write_hook);
  watching = 1;
#endif
  printf("step  clock   dist  ai   punch def   blue x     z  head pos  red x     z  head pos  health stamina thrown landed  red hp  his block: +0 +2 +4 +C +1C +2E\n");
  for (size_t k = 0; k < steps; k++)
  {
#ifdef AIWATCH_TRACE
    curStep = k;
#endif
    const O4dbInput step = { plan[k < nPlan ? k : nPlan - 1], 135, 65 };
    o4db_step(&step);
    printf("%4zu %6u %6d  %02X %5u %4X  %6d %6d %5u %3u  %6d %6d %5u %3u  %6d %6d %6u %6u  %6d\n", k, dw(0xCF02) | (uint32_t)dw(0xCF04) << 16, sw(0xCF54),
           ds()[0x89F9], dw(0xBB06 + 0x28), dw(0xBB06 + 0x30), sw(0xBA12 + 6), sw(0xBA12 + 0xA), dw(0xBA12 + 4), dw(0xBA12 + 0x16),
           sw(0xBB06 + 6), sw(0xBB06 + 0xA), dw(0xBB06 + 4), dw(0xBB06 + 0x16), sw(0xBA12 + 0x10), sw(0xBA12 + 0xE), dw(0xA488 + 6), dw(0xA488 + 8),
           sw(0xBB06 + 0x10));
    if (getenv("HIS_PUNCHES")) printf("      his punches: thrown %u landed %u\n", dw(0xA4FC + 6), dw(0xA4FC + 8));
    // (DEFENSE: both boxers' guards (+0x40) and leans (+0x48), the player's input byte as the game took it)
    if (getenv("DEFENSE"))
      printf("      guard %u lean %d punching %u / his guard %u lean %d\n", dw(0xBA12 + 0x40), sw(0xBA12 + 0x48), dw(0xBA12 + 0x28), dw(0xBB06 + 0x40), sw(0xBB06 + 0x48));
    const uint16_t blk = dw(0xBB06 + 2);
    printf("      block %04X: %6d %6d %6d %6d %6d %6d\n", blk, sw(blk), sw((uint16_t)(blk + 2)), sw((uint16_t)(blk + 4)), sw((uint16_t)(blk + 0xC)), sw((uint16_t)(blk + 0x1C)),
           sw((uint16_t)(blk + 0x2E)));
  }
  return 0;
}
