// Inputs a search could leave out, checked against the futures they lead to: o4dbEquiv GAMEDIR MOVIE PLAN K GROUPS
// The environment's STATE: the machine's start (a round's: o4dbBench save, o4dbBreak SAVE; at SPEED, the machine set up
// at it), the fast fight. Along PLAN (a solution: a line a step), every SAMPLE steps (1 by default) the machine is
// saved; from it each input of each group (GROUPS: inputs as mnemonics, ',' between inputs, ';' between groups, "-" for
// none) is stepped, then the plan's next K steps (the same for all), and what the search's hash covers is compared
// (sportsBoxing4D.hpp's computeAdditionalHashing: the boxers, their statistics, the random number, the computer's
// memory, the controls as read, their replay's unread bytes, the moves requested, the pose blocks, the round and the
// referee, the call chain and the stack): the group agrees there, or not. Printed for each group: the sample points by
// the player's punching (+0x28) and the boxers' distance at the sample, and how many of each the group agreed at.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "movie.h"

#define STACK_TOP 0xE5A8  // (as the game entry's: the program's stack, SS = DS, its top)

static const uint8_t *ds(void) { return o4db_memory() + ((uint32_t)o4db_data_segment() << 4); }
static uint16_t dw(uint16_t off) { const uint8_t *d = ds(); return (uint16_t)(d[off] | d[off + 1] << 8); }

// what the search hashes, as bytes
static size_t hashed(uint8_t *out)
{
  static const struct { uint16_t off, n; } parts[] = {
    { 0xBA12, 0xF4 }, { 0xBB06, 0xF4 }, { 0xA488, 0x74 }, { 0xA4FC, 0x74 }, { 0x5A1C, 4 },   { 0x8472, 4 },   { 0x89F6, 10 },
    { 0x8A40, 2 },    { 0x8CC8, 16 },   { 0x8E3E, 8 },    { 0x9302, 2 },    { 0xA6B0, 16 }, { 0xAEC0, 2 },   { 0xB130, 16 },
    { 0xB6B8, 0x48 }, { 0xBD08, 12 },   { 0xCC58, 4 },    { 0xCF02, 0x60 },
  };
  size_t n = 0;
  for (size_t k = 0; k < sizeof parts / sizeof *parts; k++) { memcpy(out + n, ds() + parts[k].off, parts[k].n); n += parts[k].n; }
  for (int boxer = 0; boxer < 2; boxer++)
  {
    const uint16_t readOff = dw((uint16_t)(0x8CC8 + 8 * boxer)), readSeg = dw((uint16_t)(0x8CCA + 8 * boxer));
    const uint16_t writeOff = dw((uint16_t)(0x8E3E + 4 * boxer)), writeSeg = dw((uint16_t)(0x8E40 + 4 * boxer));
    if (readSeg != writeSeg || writeOff <= readOff || writeOff - readOff > 16) continue;
    memcpy(out + n, o4db_memory() + ((uint32_t)readSeg << 4) + readOff, writeOff - readOff);
    n += writeOff - readOff;
  }
  int depth = 0;
  const uint32_t *chain = o4db_chain(&depth);
  memcpy(out + n, chain, (size_t)depth * 8);
  n += (size_t)depth * 8;
  const uint16_t sp = o4db_stack_pointer();
  if (sp < STACK_TOP) { memcpy(out + n, ds() + sp, STACK_TOP - sp); n += STACK_TOP - sp; }
  return n;
}

static O4dbButtons parse(const char *s, size_t len)
{
  O4dbButtons b = 0;
  for (size_t k = 0; k < len; k++)
  {
    const char *p = strchr(k_mnemonics, s[k]);
    if (p && s[k] != '-') b |= (O4dbButtons)1 << (p - k_mnemonics);
  }
  return b;
}

int main(int argc, char **argv)
{
  if (argc < 6 || !getenv("STATE")) { fprintf(stderr, "usage: STATE=... o4dbEquiv GAMEDIR MOVIE PLAN K GROUPS\n"); return 2; }
  o4db_set_speed(getenv("SPEED") ? (uint32_t)strtoul(getenv("SPEED"), NULL, 10) : 220000);
  if (!o4db_setup(argv[1], "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  size_t n, np;
  MovieInput *rows = movie_load(argv[2], &n), *plan = movie_load(argv[3], &np);
  if (!rows || !plan) { fprintf(stderr, "o4dbEquiv: no movie or plan\n"); return 1; }
  const int K = atoi(argv[4]);
  const size_t sample = getenv("SAMPLE") ? strtoull(getenv("SAMPLE"), NULL, 10) : 1;
  // the groups
  O4dbButtons groups[16][32];
  int nGroups = 0, nIn[16] = { 0 };
  for (const char *g = argv[5]; *g && nGroups < 16;)
  {
    const char *end = strchr(g, ';');
    if (!end) end = g + strlen(g);
    for (const char *i = g; i < end;)
    {
      const char *c = memchr(i, ',', (size_t)(end - i));
      if (!c) c = end;
      if (nIn[nGroups] < 32) groups[nGroups][nIn[nGroups]++] = parse(i, (size_t)(c - i));
      i = c < end ? c + 1 : end;
    }
    nGroups++;
    g = *end ? end + 1 : end;
  }
  const O4dbInput boot = { rows[0].buttons, rows[0].mouseX, rows[0].mouseY };
  o4db_new(&boot);
  void *st = malloc(o4db_state_size()), *base = malloc(o4db_state_size());
  FILE *f = fopen(getenv("STATE"), "rb");
  if (!f || fread(st, 1, o4db_state_size(), f) != o4db_state_size()) { fprintf(stderr, "o4dbEquiv: no state in %s\n", getenv("STATE")); return 1; }
  fclose(f);
  o4db_load(st);
  o4db_set_step_function(0x25BB0202, 8);
  o4db_set_fast(1);
  static uint8_t ref[1 << 17], got[1 << 17];
  // [group][punching][distance class: <400, 400-519, >=520][agreed, samples]
  static unsigned stats[16][2][3][2];
  for (size_t k = 0; k + K + 1 < np; k++)
  {
    if (k % sample == 0)
    {
      o4db_save(base);
      const int punching = dw(0xBA12 + 0x28) != 0, dist = (int16_t)dw(0xCF54);
      const int cls = dist < 400 ? 0 : dist < 520 ? 1 : 2;
      for (int g = 0; g < nGroups; g++)
      {
        int agree = 1;
        size_t refN = 0;
        for (int i = 0; i < nIn[g] && agree; i++)
        {
          o4db_load(base);
          const O4dbInput in = { groups[g][i], plan[k].mouseX, plan[k].mouseY };
          o4db_step(&in);
          for (int j = 1; j <= K; j++)
          {
            const O4dbInput c = { plan[k + j].buttons, plan[k + j].mouseX, plan[k + j].mouseY };
            o4db_step(&c);
          }
          if (i == 0) refN = hashed(ref);
          else
          {
            const size_t m = hashed(got);
            if (m != refN || memcmp(ref, got, m)) agree = 0;
          }
        }
        stats[g][punching][cls][0] += (unsigned)agree;
        stats[g][punching][cls][1]++;
      }
      o4db_load(base);
    }
    const O4dbInput in = { plan[k].buttons, plan[k].mouseX, plan[k].mouseY };
    o4db_step(&in);
  }
  static const char *cls[] = { "nearer than 400", "400-519", "520 or more" };
  for (int g = 0; g < nGroups; g++)
  {
    printf("group %d (%d inputs):\n", g, nIn[g]);
    for (int p = 0; p < 2; p++)
      for (int c = 0; c < 3; c++)
        if (stats[g][p][c][1]) printf("  %-12s %-16s agreed at %u of %u\n", p ? "punching" : "not punching", cls[c], stats[g][p][c][0], stats[g][p][c][1]);
  }
  return 0;
}
