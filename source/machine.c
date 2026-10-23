// quickerOpen4DBoxing's machine (machine.h): Open4DBoxing's game_run on the thread, stopped at the program's backward
// jumps after a frame end and resumed from its saved state alone (the generated sources: tools/generate.py).
#include "machine.h"

#include <dirent.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "generated/asm2c.h"
#include "generated/dos.h"
#include "generated/game.h"
#include "generated/state.h"

// --------------------------------------------------------------------------------------------------- the setup

static O4DB_TLS GameHost host;
static O4DB_TLS char gameDir[1024], writeDir[1024], cmdline[32], errorText[1024];
static O4DB_TLS int setupDone;

static const struct { const char *id, *driver; } k_sounds[] = {
  { "mt32", "mt15.drv" }, { "sb", "sb15.drv" }, { "adlib", "ad15.drv" }, { "speaker", "pc15.drv" }, { "tandy", "td15.drv" },
};

const char *o4db_error(void) { return errorText; }

// days since 1970-01-01 of a date (the proleptic Gregorian calendar), as Chimera's core counts them
static int64_t days_from_civil(int64_t y, int m, int d)
{
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static int64_t parse_clock(const char *s)
{
  int y, mo, d, h, mi, se;
  char tail;
  static const int mdays[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (sscanf(s, "%d-%d-%d %d:%d:%d%c", &y, &mo, &d, &h, &mi, &se, &tail) != 6) return -1;
  if (y < 1980 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > mdays[mo - 1] || h < 0 || h > 23 || mi < 0 || mi > 59 || se < 0 || se > 59)
    return -1;
  if (mo == 2 && d == 29 && (y % 4 || (y % 100 == 0 && y % 400))) return -1;
  return days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
}

// the folders made for the threads' writes, removed at the process's end
static pthread_mutex_t madeLock = PTHREAD_MUTEX_INITIALIZER;
static char **made;
static int madeCount;
static void remove_made(void)
{
  for (int i = 0; i < madeCount; i++)
  {
    DIR *d = opendir(made[i]);
    struct dirent *e;
    char path[2048];
    while (d && (e = readdir(d)))
      if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
      {
        snprintf(path, sizeof path, "%s/%s", made[i], e->d_name);
        unlink(path);
      }
    if (d) closedir(d);
    rmdir(made[i]);
  }
}

static bool host_frame(void *ctx, uint64_t frame);

int o4db_setup(const char *dir, const char *sound, const char *clockStart, const char *wdir)
{
  errorText[0] = 0;
  int s = -1;
  for (int i = 0; i < (int)(sizeof k_sounds / sizeof k_sounds[0]); i++)
    if (!strcmp(sound, k_sounds[i].id)) s = i;
  if (s < 0) { snprintf(errorText, sizeof errorText, "the sound is %s; it is mt32, sb, adlib, speaker or tandy", sound); return 0; }
  const int64_t start = parse_clock(clockStart);
  if (start < 0) { snprintf(errorText, sizeof errorText, "the start date and time is \"%s\"; it is written YYYY-MM-DD HH:MM:SS, from 1980", clockStart); return 0; }
  snprintf(gameDir, sizeof gameDir, "%s", dir);
  snprintf(cmdline, sizeof cmdline, "n %s d", k_sounds[s].driver);
  if (wdir) snprintf(writeDir, sizeof writeDir, "%s", wdir);
  else
  {
    snprintf(writeDir, sizeof writeDir, "/dev/shm/o4db-%d-XXXXXX", (int)getpid());
    if (!mkdtemp(writeDir)) { snprintf(errorText, sizeof errorText, "no folder for the game's writes in /dev/shm"); return 0; }
    pthread_mutex_lock(&madeLock);
    if (madeCount == 0) atexit(remove_made);
    made = realloc(made, sizeof *made * (size_t)(madeCount + 1));
    made[madeCount++] = strdup(writeDir);
    pthread_mutex_unlock(&madeLock);
  }
  // as Chimera's core sets the game up (boxing-driver.c's boxdrv_init): the MCGA build, 22,000 instructions a
  // millisecond, the clock's start; no MT-32 to play the MIDI to (the game's run does not depend on it)
  host = (GameHost){
    .gameDir = gameDir,
    .writeDir = writeDir,
    .program = "4DBMCGA.EXE",
    .cmdline = cmdline,
    .frame = host_frame,
    .midi = NULL,
    .ctx = NULL,
    .cycles = 22000,
    .startTime = start,
  };
  setupDone = 1;
  return 1;
}

// ----------------------------------------------------------------------------------------------- the machine

// The machine's own state: the buttons the game was given, the run's progress
typedef struct
{
  uint8_t held[O4DB_BUTTONS];
  int32_t given[2];
  O4dbInput boot;
  int32_t booted, exited, exitCode;
  uint64_t frames;  // frame ends passed
} Machine;
static O4DB_TLS Machine M;

// the run in progress (not state: a run starts and ends in one call)
static O4DB_TLS const O4dbInput *runInputs;
static O4DB_TLS size_t runCount, runAvail, runIndex;
static O4DB_TLS void (*frameHook)(uint64_t frame, void *ctx);
static O4DB_TLS void *frameHookCtx;
void o4db_set_frame_hook(void (*hook)(uint64_t frame, void *ctx), void *ctx) { frameHook = hook; frameHookCtx = ctx; }
static O4DB_TLS jmp_buf yieldJump;
static O4DB_TLS int threadReady;  // the thread's setup made (game_setup: by a start or a load)

void o4db_new(const O4dbInput *boot)
{
  memset(&M, 0, sizeof M);
  M.boot = boot ? *boot : (O4dbInput){ 0, O4DB_MOUSE_X_NEUTRAL, O4DB_MOUSE_Y_NEUTRAL };
  M.given[0] = O4DB_MOUSE_X_NEUTRAL;
  M.given[1] = O4DB_MOUSE_Y_NEUTRAL;
}

// What each key button is on the PC's keyboard: its scan code (set 1), as boxing-driver.c's k_scan
static const uint8_t k_scan[O4DB_BUTTONS] = {
  0, 0, 0x48, 0x50, 0x4B, 0x4D, 0x47, 0x49, 0x4F, 0x51, 0x4C, 0x52,
  0x1C, 0x39, 0x0E, 0x01, 0x0F, 0x2A, 0x36, 0x1D, 0x38,
  0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F,
  0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C,
  0x0B, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
};
static int modifier(int b) { return b == 17 || b == 18 || b == 19 || b == 20; }

// The buttons that changed, as Chimera's core gives them (boxing-driver.c's apply_buttons): the modifiers that went
// down first, then the rest in their order, then the modifiers that came up; the mouse moved only when its place
// changed, to the place asked, its motion counted
static void apply_buttons(const O4dbInput *in)
{
  for (int pass = 0; pass < 3; pass++)
    for (int b = 0; b < O4DB_BUTTONS; b++)
    {
      const uint8_t want = (uint8_t)((in->buttons >> b) & 1);
      if (want == M.held[b]) continue;
      const int down = want;
      if ((pass == 0 && !(modifier(b) && down)) || (pass == 1 && modifier(b)) || (pass == 2 && !(modifier(b) && !down))) continue;
      M.held[b] = want;
      if (b == 0 || b == 1)
      {
        const u16 bit = b == 0 ? 1 : 2;
        if (down) asm_mouse.buttons |= bit;
        else asm_mouse.buttons &= (u16)~bit;
      }
      else
        game_key(k_scan[b], false, down);
    }
  int32_t x = in->mouseX < 0 ? 0 : in->mouseX > 319 ? 319 : in->mouseX;
  int32_t y = in->mouseY < 0 ? 0 : in->mouseY > 199 ? 199 : in->mouseY;
  if (x != M.given[0] || y != M.given[1])
  {
    M.given[0] = x;
    M.given[1] = y;
    const int dx = x * 2, dy = y;
    asm_mouse.dx = (i16)(asm_mouse.dx + (dx - asm_mouse.x));
    asm_mouse.dy = (i16)(asm_mouse.dy + (dy - asm_mouse.y));
    asm_mouse.x = (u16)dx;
    asm_mouse.y = (u16)dy;
    asm_mouse_clamp();
  }
}

// Steps ended by the game reading its controls (o4db_set_step_function): the function, the frames at most, and the
// run in progress being such a step
static O4DB_TLS uint32_t stepFn;
static O4DB_TLS unsigned stepMaxFrames;
static O4DB_TLS int stepping;
static O4DB_TLS uint64_t stepFnEntries;
void cheats_fn(uint32_t addr);
static void machine_fn(uint32_t addr)
{
  if (addr == stepFn)
  {
    stepFnEntries++;
    if (stepping && runIndex >= 1) asm_yield_req = 1;
  }
  cheats_fn(addr);
}
void o4db_set_step_function(uint32_t fn, unsigned maxFrames)
{
  stepFn = fn;
  stepMaxFrames = maxFrames;
}
uint64_t o4db_step_function_entries(void) { return stepFnEntries; }

// A frame end: the run's next input given (its last held), and a stop due once the run's are given (a step: when the
// game read its controls after a frame end of the step, or at its last frame)
static bool host_frame(void *ctx, uint64_t frame)
{
  (void)ctx, (void)frame;
  if (frameHook) frameHook(M.frames, frameHookCtx);
  M.frames++;
  apply_buttons(&runInputs[runIndex < runAvail ? runIndex : runAvail - 1]);
  runIndex++;
  if (stepping ? (stepMaxFrames && runIndex >= stepMaxFrames) : runIndex >= runCount) asm_yield_req = 1;
  return true;
}

// The stop (asm2c.h): where, then out of the program
void asm_yield(u32 site)
{
  asm_shadow[asm_sdepth].site = site;
  asm_chain = asm_sdepth + 1;
  asm_yield_req = 0;
  longjmp(yieldJump, 1);
}

uint64_t o4db_run(const O4dbInput *inputs, size_t n) { return o4db_run_ahead(inputs, n, n); }

uint64_t o4db_step(const O4dbInput *input)
{
  if (!stepFn) return o4db_run(input, 1);
  stepping = 1;
  const uint64_t f = o4db_run_ahead(input, 1, 1);
  stepping = 0;
  return f;
}

uint64_t o4db_run_ahead(const O4dbInput *inputs, size_t n, size_t avail)
{
  if (M.exited || n == 0) return 0;
  if (avail < n) avail = n;
  runInputs = inputs;
  runCount = n;
  runAvail = avail;
  runIndex = 0;
  asm_yield_req = 0;
  if (!setjmp(yieldJump))
  {
    int code;
    if (!M.booted)
    {
      // Chimera's first step: its buttons before the game starts
      M.booted = 1;
      threadReady = 1;
      asm_fn_hook = machine_fn;
      apply_buttons(&M.boot);
      asm_unclean = 0;
      asm_sdepth = 0;
      asm_resuming = 0;
      code = game_run(&host);
    }
    else
    {
      asm_unclean = 0;
      asm_sdepth = 0;
      asm_resuming = 1;
      code = game_resume();
    }
    // (here only when the program ended)
    M.exited = 1;
    M.exitCode = code;
  }
  runInputs = NULL;
  return runIndex;
}

// ------------------------------------------------------------------------------------------------- the state

static void count_size(void *data, size_t size, void *c) { (void)data; *(size_t *)c += size; }
static void save_part(void *data, size_t size, void *c)
{
  uint8_t **p = (uint8_t **)c;
  memcpy(*p, data, size);
  *p += size;
}
static void load_part(void *data, size_t size, void *c)
{
  const uint8_t **p = (const uint8_t **)c;
  memcpy(data, *p, size);
  *p += size;
}

size_t o4db_state_size(void)
{
  size_t n = sizeof M;
  o4db_state_all(count_size, &n);
  return n;
}

void o4db_save(void *out)
{
  uint8_t *p = out;
  memcpy(p, &M, sizeof M);
  p += sizeof M;
  o4db_state_all(save_part, &p);
}

void o4db_load(const void *in)
{
  // a thread whose machine never started has not its setup (the hooks, DOS, the program's loading): made first
  if (!threadReady)
  {
    asm_fn_hook = machine_fn;
    game_setup(&host);
    threadReady = 1;
  }
  const uint8_t *p = in;
  memcpy(&M, p, sizeof M);
  p += sizeof M;
  o4db_state_all(load_part, &p);
}

// ------------------------------------------------------------------------------------------- the states as deltas

#define PAGE 256
#define PAGES (sizeof g_mem / PAGE)
static pthread_mutex_t referenceLock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t *reference;  // the process's (every machine's states are deltas from the same memory)

int o4db_set_reference(void)
{
  int differ = 0;
  pthread_mutex_lock(&referenceLock);
  if (!reference)
  {
    reference = malloc(sizeof g_mem);
    memcpy(reference, g_mem, sizeof g_mem);
  }
  else
    for (size_t p = 0; p < PAGES; p++) differ += memcmp(reference + p * PAGE, g_mem + p * PAGE, PAGE) != 0;
  pthread_mutex_unlock(&referenceLock);
  return differ;
}

// the rest of the machine: the visitors' parts but the memory
static void count_rest(void *data, size_t size, void *c) { if (data != (void *)g_mem) *(size_t *)c += size; }
static void save_rest(void *data, size_t size, void *c) { if (data != (void *)g_mem) save_part(data, size, c); }
static void load_rest(void *data, size_t size, void *c) { if (data != (void *)g_mem) load_part(data, size, c); }

size_t o4db_delta_size(unsigned maxPages)
{
  size_t n = sizeof M + sizeof(uint16_t) * (1 + maxPages) + (size_t)PAGE * maxPages;
  o4db_state_all(count_rest, &n);
  return n;
}

int o4db_save_delta(void *out, unsigned maxPages)
{
  uint8_t *p = out;
  memcpy(p, &M, sizeof M);
  p += sizeof M;
  o4db_state_all(save_rest, &p);
  uint16_t *count = (uint16_t *)p, *index = count + 1;
  uint8_t *pages = (uint8_t *)(index + maxPages);
  unsigned n = 0;
  for (size_t k = 0; k < PAGES; k++)
    if (memcmp(reference + k * PAGE, g_mem + k * PAGE, PAGE))
    {
      if (n == maxPages) return -1;
      index[n] = (uint16_t)k;
      memcpy(pages + (size_t)n * PAGE, g_mem + k * PAGE, PAGE);
      n++;
    }
  *count = (uint16_t)n;
  memset(index + n, 0, sizeof *index * (maxPages - n));
  memset(pages + (size_t)n * PAGE, 0, (size_t)PAGE * (maxPages - n));
  return (int)n;
}

void o4db_load_delta(const void *in, unsigned maxPages)
{
  if (!threadReady)
  {
    asm_fn_hook = machine_fn;
    game_setup(&host);
    threadReady = 1;
  }
  const uint8_t *p = in;
  memcpy(&M, p, sizeof M);
  p += sizeof M;
  o4db_state_all(load_rest, &p);
  const uint16_t *count = (const uint16_t *)p, *index = count + 1;
  const uint8_t *pages = (const uint8_t *)(index + maxPages);
  memcpy(g_mem, reference, sizeof g_mem);
  for (unsigned k = 0; k < *count; k++) memcpy(g_mem + (size_t)index[k] * PAGE, pages + (size_t)k * PAGE, PAGE);
}

uint8_t *o4db_memory(void) { return g_mem; }
uint16_t o4db_data_segment(void) { return g_dsSeg; }
const uint8_t *o4db_registers(void) { return (const uint8_t *)&R; }
const uint8_t *o4db_screen(void) { return game_screen(); }
const uint8_t (*o4db_palette(void))[3] { return game_palette(); }
uint64_t o4db_frames(void) { return M.frames; }
int o4db_exited(void) { return M.exited; }
int o4db_open_files(void) { return dos_open_host_files(); }
const uint32_t *o4db_chain(int *length)
{
  *length = asm_chain;
  return (const uint32_t *)asm_shadow;
}
uint16_t o4db_stack_pointer(void) { return R.sp; }
