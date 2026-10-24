// A movie's rows again with function keys pressed (the ring's cameras), its inputs kept where the game reads them:
// o4dbRetime GAMEDIR MOVIE PLAN OUT
// The environment's STATE: the machine's start (o4dbBench save, the menuing's end; INITIAL_SPEED its speed, SPEED the
// run's, 220,000 both by default); PLAN: the rows from there (a row a frame end, Chimera's). CAMERA: "READ:KEY,..."
// (a key's mnemonic, A F G H I J M P S V for F1-F10) pressed for HELD frames (3) from the first frame end after the
// game's READ-th reading of the boxers' controls (25bb:0202), counted from the start. A camera's frame may take longer than the game's tick, the
// next readings then later in the frames: the rows are played once as they are, the row in force at each frame noted
// by the readings so far and the frames since the last; then again with the keys, each frame end given the first
// pass's row of the same reading and frame since it (the interval's last when it is longer), the keys added. Written
// to OUT: the second pass's rows; printed: the readings of each pass and how many of them found the fight the same
// (sportsBoxing4D.hpp's hash of the boxers, their statistics, the random number, the computer's memory and the
// rest, at each reading; CORE: the boxers, their statistics and the random number alone), the first that did not.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "movie.h"

static uint64_t fight_hash(void)
{
  static const struct { uint16_t off, n; } parts[] = {
    { 0xBA12, 0xF4 }, { 0xBB06, 0xF4 }, { 0xA488, 0x74 }, { 0xA4FC, 0x74 }, { 0x5A1C, 4 },   { 0x8472, 4 },   { 0x89F6, 10 },
    { 0x8A40, 2 },    { 0x8CC8, 16 },   { 0x8E3E, 8 },    { 0x9302, 2 },    { 0xA6B0, 16 }, { 0xAEC0, 2 },   { 0xB130, 16 },
    { 0xB6B8, 0x48 }, { 0xBD08, 12 },   { 0xCC58, 4 },    { 0xCF02, 0x60 },
  };
  const uint8_t *ds = o4db_memory() + ((uint32_t)o4db_data_segment() << 4);
  uint64_t h = 0xcbf29ce484222325ull;
  // (CORE: the boxers, their statistics and the random number alone)
  const size_t n = getenv("CORE") ? 4 : sizeof parts / sizeof *parts;
  for (size_t k = 0; k < n; k++) h = hash_bytes(ds + parts[k].off, parts[k].n, h);
  if (getenv("CORE")) h = hash_bytes(ds + 0x8472, 4, h);
  return h;
}

#define MAXR 400000
static uint64_t hashes[2][MAXR];
static size_t nReads[2];
static int pass, done;
static uint64_t passStart;  // (the machine's count of readings goes on across loads: each pass's from its start)
static void read_hook(uint64_t entries, void *ctx)
{
  (void)ctx;
  const uint64_t r = entries - passStart;
  if (r < MAXR) hashes[pass][r] = fight_hash(), nReads[pass] = r + 1;
}

// the first pass's row at each (reading, frames since it): a frame's index by the reading's first frame
static MovieInput *plan;
static size_t nPlan;
static uint64_t firstFrame[MAXR];  // (the plan's frame index where each reading count began, +1; 0 none)
static uint64_t lastEntries, sinceFrame, frameIdx;
static O4dbInput cur;
static struct { uint64_t read; uint64_t key; } cams[256];
static int nCams, held = 3;
static FILE *out;

static void frame_hook(uint64_t frame, void *ctx)
{
  (void)frame, (void)ctx;
  const uint64_t e = o4db_step_function_entries() - passStart;
  if (e != lastEntries || frameIdx == 0) { lastEntries = e; sinceFrame = frameIdx; }
  MovieInput m;
  if (pass == 0)
  {
    if (e < MAXR && !firstFrame[e]) firstFrame[e] = frameIdx + 1;
    m = plan[frameIdx < nPlan ? frameIdx : nPlan - 1];
  }
  else
  {
    // the first pass's row of this reading, as many frames after its start (its last when this interval is longer)
    uint64_t base = e < MAXR && firstFrame[e] ? firstFrame[e] - 1 : nPlan - 1, end = nPlan;
    for (uint64_t r = e + 1; r < MAXR && r <= e + 4; r++) if (firstFrame[r]) { end = firstFrame[r] - 1; break; }
    uint64_t idx = base + (frameIdx - sinceFrame);
    if (idx >= end) idx = end ? end - 1 : 0;
    if (idx >= nPlan) idx = nPlan - 1;
    m = plan[idx];
    for (int c = 0; c < nCams; c++)
      if (e == cams[c].read && frameIdx - sinceFrame < (uint64_t)held) m.buttons |= cams[c].key;
    movie_write_row(out, m.buttons, m.mouseX, m.mouseY);
    // (done: past the plan's last reading by as many frames as the plan had after it)
    if (e + 1 >= nReads[0] && firstFrame[nReads[0] - 1] && frameIdx - sinceFrame + 1 >= nPlan - (firstFrame[nReads[0] - 1] - 1)) done = 1;
  }
  cur = (O4dbInput){ m.buttons, m.mouseX, m.mouseY };
  frameIdx++;
}

int main(int argc, char **argv)
{
  if (argc < 5 || !getenv("STATE")) { fprintf(stderr, "usage: STATE=... o4dbRetime GAMEDIR MOVIE PLAN OUT\n"); return 2; }
  const uint32_t speed = getenv("SPEED") ? (uint32_t)strtoul(getenv("SPEED"), NULL, 10) : 220000;
  const uint32_t initial = getenv("INITIAL_SPEED") ? (uint32_t)strtoul(getenv("INITIAL_SPEED"), NULL, 10) : speed;
  if (getenv("HELD")) held = atoi(getenv("HELD"));
  for (const char *c = getenv("CAMERA"); c && *c && nCams < 256;)
  {
    char *end;
    cams[nCams].read = strtoull(c, &end, 10);
    if (*end != ':' || !end[1]) break;
    const char *p = strchr(k_mnemonics, end[1]);
    cams[nCams++].key = p ? 1ull << (p - k_mnemonics) : 0;
    c = end + 2;
    if (*c == ',') c++;
  }
  o4db_set_speed(initial);
  if (!o4db_setup(argv[1], "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  size_t n;
  MovieInput *rows = movie_load(argv[2], &n);
  plan = movie_load(argv[3], &nPlan);
  if (!rows || !plan || !nPlan) { fprintf(stderr, "o4dbRetime: no movie or plan\n"); return 1; }
  out = fopen(argv[4], "w");
  void *st = malloc(o4db_state_size());
  FILE *f = fopen(getenv("STATE"), "rb");
  if (!f || fread(st, 1, o4db_state_size(), f) != o4db_state_size()) { fprintf(stderr, "o4dbRetime: no state in %s\n", getenv("STATE")); return 1; }
  fclose(f);
  const O4dbInput boot = { rows[0].buttons, rows[0].mouseX, rows[0].mouseY };
  o4db_new(&boot);
  o4db_set_frame_hook(frame_hook, NULL);
  o4db_test_read_hook(read_hook, NULL);
  uint64_t frames[2];
  for (pass = 0; pass < 2; pass++)
  {
    o4db_load(st);
    if (initial != speed) o4db_test_set_cycles(speed);
    passStart = o4db_step_function_entries();
    frameIdx = 0, lastEntries = 0, done = 0;
    uint64_t passed = 0;
    while (passed < (pass == 0 ? nPlan : nPlan * 2) && !done && !o4db_exited()) passed += o4db_run(&cur, 1);
    frames[pass] = frameIdx;
  }
  fclose(out);
  size_t same = 0, firstDiff = (size_t)-1, common = nReads[0] < nReads[1] ? nReads[0] : nReads[1];
  for (size_t r = 0; r < common; r++)
    if (hashes[0][r] == hashes[1][r]) same++;
    else if (firstDiff == (size_t)-1) firstDiff = r;
  printf("readings: %zu as the plan, %zu with the keys (frames %llu, %llu); the fight the same at %zu of %zu", nReads[0], nReads[1], (unsigned long long)frames[0],
         (unsigned long long)frames[1], same, common);
  if (firstDiff != (size_t)-1) printf(", first different at reading %zu", firstDiff);
  printf("\n");
  return firstDiff == (size_t)-1 ? 0 : 1;
}
