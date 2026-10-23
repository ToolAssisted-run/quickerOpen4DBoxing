// The search's way of stepping checked against the plain one: o4dbDeltaCheck GAMEDIR INITIAL SOLUTION
// The machine made as the JaffarPlus emulator makes it (the environment's POKES during the initial rows, played at
// INITIAL_SPEED (22,000), then SPEED (220,000), the step function, the fast fight unless FAST=0, the pictures unsaved,
// the reference), and the solution's steps (a JaffarPlus solution: Chimera's rows, a step a line) run twice: one after
// another, as jaffar-player replays them, and as the search runs them: each from the delta of the state before it,
// that delta loaded again after a step of it with another input (the next step's, a search's sibling). After every
// step the two machines' memories compared (but the unsaved pictures): the first step where they differ, its pages,
// and the data segment's words that differ.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "movie.h"
#include "pokes.h"

#define MEM 0x100000
// (the pictures the JaffarPlus emulator leaves out of its states: not 51000-51FFF, which the fast fight reads; the
// environment's EXTRA_UNSAVED, "START-END" hexadecimal, one more region tried)
static uint32_t unsaved[4][2] = { { 0xA0000, 0xAFFFF }, { 0x68000, 0x71FFF }, { 0x88000, 0x88FFF } };
static size_t nUnsaved = 3;

// (the environment's SCRAMBLE: after every delta loaded, the unsaved memory filled with noise, as a search's thread
// leaves there whatever its last state drew; the steps must not read it)
static int scramble;
static uint64_t noise = 0x9E3779B97F4A7C15ull;
static void load_state(const void *delta, unsigned maxPages)
{
  o4db_load_delta(delta, maxPages);
  if (!scramble) return;
  uint8_t *m = o4db_memory();
  for (size_t k = 0; k < nUnsaved; k++)
    for (uint32_t a = unsaved[k][0]; a <= unsaved[k][1]; a++)
    {
      noise ^= noise << 13; noise ^= noise >> 7; noise ^= noise << 17;
      m[a] = (uint8_t)noise;
    }
}

static void frame_hook(uint64_t frame, void *ctx) { (void)ctx; pokes_apply(frame); }
static int saved_page(uint32_t page)
{
  for (size_t k = 0; k < nUnsaved; k++)
    if (page * 256 >= unsaved[k][0] && page * 256 <= unsaved[k][1]) return 0;
  return 1;
}

static O4dbInput *load(const char *path, size_t *n)
{
  MovieInput *m = movie_load(path, n);
  if (!m) { fprintf(stderr, "o4dbDeltaCheck: cannot read %s\n", path); exit(1); }
  O4dbInput *in = malloc((*n ? *n : 1) * sizeof *in);
  for (size_t k = 0; k < *n; k++) in[k] = (O4dbInput){ m[k].buttons, m[k].mouseX, m[k].mouseY };
  free(m);
  return in;
}

static const char *gameDir;
static O4dbInput *init, *steps;
static size_t ni, ns;
static unsigned maxPages;
static uint8_t *seq, *deltas;
static size_t deltaSize;

// a machine as the JaffarPlus emulator makes one (every thread its own)
static void make(void)
{
  const uint32_t speed = getenv("SPEED") ? (uint32_t)strtoul(getenv("SPEED"), NULL, 10) : 220000;
  const uint32_t initial = getenv("INITIAL_SPEED") ? (uint32_t)strtoul(getenv("INITIAL_SPEED"), NULL, 10) : 22000;
  o4db_set_speed(initial);
  if (!o4db_setup(gameDir, "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); exit(1); }
  o4db_set_frame_hook(frame_hook, NULL);
  o4db_new(&init[0]);
  if (ni > 1) o4db_run(init + 1, ni - 1);
  o4db_set_frame_hook(NULL, NULL);
  if (initial != speed) o4db_test_set_cycles(speed);
  o4db_set_step_function(0x25BB0202, 8);
  o4db_set_fast(getenv("FAST") ? atoi(getenv("FAST")) : 1);
  for (size_t k = 0; k < nUnsaved; k++) o4db_set_unsaved(unsaved[k][0], unsaved[k][1]);
  const int differ = o4db_set_reference();
  if (differ) printf("this thread's machine differs from the first one's in %d pages\n", differ);
}

// one after another: the memory after every step, and the delta before it
static void *replay(void *arg)
{
  (void)arg;
  make();
  int most = 0;
  for (size_t k = 0; k < ns; k++)
  {
    const int pages = o4db_save_delta(deltas + k * deltaSize, maxPages);
    if (pages < 0) { fprintf(stderr, "o4dbDeltaCheck: step %zu's state has more than %u pages\n", k, maxPages); exit(1); }
    if (pages > most) most = pages;
    // (PAGES_EVERY: the delta's pages printed every that many steps)
    if (getenv("PAGES_EVERY") && k % strtoull(getenv("PAGES_EVERY"), NULL, 10) == 0) printf("step %zu: %d pages\n", k, pages);
    o4db_step(&steps[k]);
    memcpy(seq + k * (size_t)MEM, o4db_memory(), MEM);
  }
  printf("replayed: %zu steps (%llu replays of steps), %d pages at most in a state\n", ns, (unsigned long long)o4db_fast_replays(), most);
  // (PAGEMAP: the last state's delta pages, in runs)
  if (getenv("PAGEMAP"))
  {
    void *last = malloc(deltaSize);
    o4db_save_delta(last, maxPages);
    const uint8_t *p = last;
    size_t off = o4db_delta_size(maxPages) - (size_t)maxPages * 256 - 2 - (size_t)maxPages * 2;  // (the machine and the rest before the count)
    const uint16_t count = (uint16_t)(p[off] | p[off + 1] << 8), *index = (const uint16_t *)(p + off + 2);
    printf("the last state: %u pages differ from the start:", count);
    for (unsigned k = 0; k < count;)
    {
      unsigned j = k;
      while (j + 1 < count && index[j + 1] == index[j] + 1) j++;
      printf(" %05X-%05X(%u)", index[k] * 256, index[j] * 256 + 255, j - k + 1);
      k = j + 1;
    }
    printf("\n");
  }
  return NULL;
}

int main(int argc, char **argv)
{
  if (argc < 4) { fprintf(stderr, "usage: o4dbDeltaCheck GAMEDIR INITIAL SOLUTION   (THREADS=2: the deltas loaded on another thread's machine)\n"); return 2; }
  gameDir = argv[1];
  init = load(argv[2], &ni);
  steps = load(argv[3], &ns);
  pokes_load();
  scramble = getenv("SCRAMBLE") != NULL;
  if (getenv("EXTRA_UNSAVED"))
  {
    unsigned a, b;
    if (sscanf(getenv("EXTRA_UNSAVED"), "%x-%x", &a, &b) == 2) unsaved[nUnsaved][0] = a, unsaved[nUnsaved][1] = b, nUnsaved++;
  }
  maxPages = getenv("PAGES") ? (unsigned)atoi(getenv("PAGES")) : 192;
  seq = malloc(ns * (size_t)MEM);
  const int threads = getenv("THREADS") ? atoi(getenv("THREADS")) : 1;
  if (threads > 1)
  {
    pthread_t t;
    o4db_set_speed(getenv("INITIAL_SPEED") ? (uint32_t)strtoul(getenv("INITIAL_SPEED"), NULL, 10) : 22000);
    deltaSize = 0;
    // (the delta's size known once a machine is set up: the first thread's)
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 << 20);
    make();
    deltaSize = o4db_delta_size(maxPages);
    deltas = malloc(ns * deltaSize);
    pthread_create(&t, &attr, replay, NULL);
    pthread_join(t, NULL);
    printf("the deltas loaded on the main thread's machine\n");
  }
  else
  {
    make();
    deltaSize = o4db_delta_size(maxPages);
    deltas = malloc(ns * deltaSize);
    void *start = malloc(o4db_state_size());
    o4db_save(start);
    replay(NULL);
    o4db_load(start);
  }

  // (the environment's CALIBRATE: as JaffarPlus's engine starts, that many steps of the first input from the start,
  // the start's delta loaded back, then the solution's steps one after another, as the search's first states come)
  if (getenv("CALIBRATE"))
  {
    const int n = atoi(getenv("CALIBRATE"));
    // (CALIBRATE_INPUT: the calibration's input as mnemonics, "" none; the solution's first by default)
    O4dbInput cal = steps[0];
    if (getenv("CALIBRATE_INPUT"))
    {
      cal = (O4dbInput){ 0, 135, 65 };
      for (const char *c = getenv("CALIBRATE_INPUT"); *c; c++) cal.buttons |= 1ull << (strchr(k_mnemonics, *c) - k_mnemonics);
    }
    load_state(deltas, maxPages);
    for (int k = 0; k < n; k++) o4db_step(&cal);
    load_state(deltas, maxPages);
    for (size_t k = 0; k < ns; k++)
    {
      o4db_step(&steps[k]);
      const uint8_t *a = seq + k * (size_t)MEM, *b = o4db_memory();
      int pages = 0;
      for (uint32_t p = 0; p < MEM / 256; p++)
        if (saved_page(p) && memcmp(a + p * 256, b + p * 256, 256)) pages++;
      if (!pages) continue;
      printf("after %d steps from the start and its delta loaded back, step %zu differs: %d pages\n", n, k, pages);
      const uint32_t dsBase = (uint32_t)o4db_data_segment() << 4;
      for (uint32_t o = 0, shown = 0; o < 0x10000 && shown < 30; o += 2)
        if (a[dsBase + o] != b[dsBase + o] || a[dsBase + o + 1] != b[dsBase + o + 1])
        {
          printf("  DS:%04X  replayed %04X  calibrated %04X\n", o, a[dsBase + o] | a[dsBase + o + 1] << 8, b[dsBase + o] | b[dsBase + o + 1] << 8);
          shown++;
        }
      return 1;
    }
    printf("the same after %d steps from the start and its delta loaded back\n", n);
    return 0;
  }

  // as the search: each step from the delta before it, loaded again after its siblings' steps (each sibling's state
  // saved as the search saves its children); the siblings: the solution's next input, or (SIBLINGS=all) every input
  // of round.py's sets (the keypad's 9 directions, alone, with Enter, with the right Shift)
  static const char *dirs[9] = { "", "U", "D", "L", "R", "Q", "E", "Z", "C" }, *buttons[3] = { "", "N", "!" };
  O4dbInput all[27];
  for (int b = 0, n = 0; b < 3; b++)
    for (int d = 0; d < 9; d++, n++)
    {
      char keys[4];
      snprintf(keys, sizeof keys, "%s%s", dirs[d], buttons[b]);
      all[n] = (O4dbInput){ 0, 135, 65 };
      for (const char *c = keys; *c; c++) all[n].buttons |= 1ull << (strchr(k_mnemonics, *c) - k_mnemonics);
    }
  const int everySibling = getenv("SIBLINGS") && !strcmp(getenv("SIBLINGS"), "all");
  int differed = 0, dsDiffered = 0;
  void *child = malloc(deltaSize);
  for (size_t k = 0; k < ns; k++)
  {
    load_state(deltas + k * deltaSize, maxPages);
    for (int j = 0; j < (everySibling ? 27 : 1); j++)
    {
      if (j) load_state(deltas + k * deltaSize, maxPages);
      const O4dbInput sibling = everySibling ? all[j] : k + 1 < ns ? steps[k + 1] : steps[0];
      o4db_step(&sibling);
      o4db_save_delta(child, maxPages);
    }
    load_state(deltas + k * deltaSize, maxPages);
    o4db_step(&steps[k]);
    const uint8_t *a = seq + k * (size_t)MEM, *b = o4db_memory();
    int pages = 0;
    for (uint32_t p = 0; p < MEM / 256; p++)
      if (saved_page(p) && memcmp(a + p * 256, b + p * 256, 256)) pages++;
    if (!pages) continue;
    printf("step %zu differs as the search steps it: %d pages", k, pages);
    for (uint32_t p = 0, shown = 0; p < MEM / 256 && shown < 16; p++)
      if (saved_page(p) && memcmp(a + p * 256, b + p * 256, 256)) printf(" %05X", p * 256), shown++;
    printf("\n");
    const uint32_t dsBase = (uint32_t)o4db_data_segment() << 4;
    int dsWords = 0;
    for (uint32_t o = 0, shown = 0; o < 0x10000; o += 2)
      if (a[dsBase + o] != b[dsBase + o] || a[dsBase + o + 1] != b[dsBase + o + 1])
      {
        if (shown < 40) printf("  DS:%04X  replayed %04X  searched %04X\n", o, a[dsBase + o] | a[dsBase + o + 1] << 8, b[dsBase + o] | b[dsBase + o + 1] << 8);
        shown++, dsWords++;
      }
    // (KEEPGOING: on to the next steps, each from the replay's delta again, the steps with differences counted and
    // those that reach the fight's data segment)
    if (!getenv("KEEPGOING")) return 1;
    differed++;
    if (dsWords) dsDiffered++;
  }
  if (differed) { printf("%d steps differed as the search steps them, %d of them in the data segment\n", differed, dsDiffered); return 1; }
  printf("the same: %zu steps stepped as the search steps them\n", ns);
  return 0;
}
