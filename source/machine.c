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
#include "generated/pit.h"

// --------------------------------------------------------------------------------------------------- the setup

static O4DB_TLS GameHost host;
static O4DB_TLS char gameDir[1024], writeDir[1024], cmdline[32], errorText[1024];
static O4DB_TLS int setupDone;
static uint32_t setupCycles = 22000;  // the machine's speed (o4db_set_speed)
void o4db_set_speed(uint32_t cycles) { setupCycles = cycles ? cycles : 22000; }

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
  // as Chimera's core sets the game up (boxing-driver.c's boxdrv_init): the MCGA build, the machine's speed
  // (o4db_set_speed: 22,000 instructions a millisecond, the oracle's 486, by default), the clock's start; no MT-32 to
  // play the MIDI to (the game's run does not depend on it)
  host = (GameHost){
    .gameDir = gameDir,
    .writeDir = writeDir,
    .program = "4DBMCGA.EXE",
    .cmdline = cmdline,
    .frame = host_frame,
    .midi = NULL,
    .ctx = NULL,
    .cycles = setupCycles,
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
  // (the fast fight: whole frames to draw before a tick held back, the tick's time, the frame in progress drawn)
  int32_t fastFullFrames, fastDraw;
  uint16_t fastHeldTime, fastPad;
} Machine;
static O4DB_TLS Machine M;

static void o4db_load_pristine(void);

// the run in progress (not state: a run starts and ends in one call)
static O4DB_TLS const O4dbInput *runInputs;
static O4DB_TLS size_t runCount, runAvail, runIndex;
static O4DB_TLS void (*frameHook)(uint64_t frame, void *ctx);
static O4DB_TLS void *frameHookCtx;
void o4db_set_frame_hook(void (*hook)(uint64_t frame, void *ctx), void *ctx) { frameHook = hook; frameHookCtx = ctx; }
static O4DB_TLS jmp_buf yieldJump;
static O4DB_TLS int threadReady;  // the thread's setup made (game_setup: by a start or a load)

// the thread's machine before its first start (every later start begins from it: the game's setup does not reset all
// the machine's state, a video mode set by the last run among it)
static O4DB_TLS uint8_t *pristine;

void o4db_new(const O4dbInput *boot)
{
  if (!pristine)
  {
    pristine = malloc(o4db_state_size());
    o4db_save(pristine);
  }
  else
    o4db_load_pristine();
  memset(&M, 0, sizeof M);
  asm_fast = asm_skip_draw = 0;  // (a new machine plays the whole game: o4db_set_fast after its start)
  asm_dirty_all = 1;
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
// run in progress being such a step. The generated program marks the controls' reader's entry (25bb:0202:
// asm_step_mark), the only step function this machine knows
static O4DB_TLS uint32_t stepFn;
static O4DB_TLS unsigned stepMaxFrames;
static O4DB_TLS int stepping;
static O4DB_TLS uint64_t stepFnEntries;
void asm_step_mark(void)
{
  stepFnEntries++;
  if (stepping && runIndex >= 1) asm_yield_req = 1;
}
void o4db_set_step_function(uint32_t fn, unsigned maxFrames)
{
  if (fn != 0 && fn != 0x25BB0202)
  {
    fprintf(stderr, "quickerOpen4DBoxing: the step function can only be 25BB0202 (the boxers' controls)\n");
    abort();
  }
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

// ------------------------------------------------------------------------- the fight's logic alone (o4db_set_fast)
// The fight's logic depends on the drawing only through the time the drawing takes and one bug of the game's. At a
// speed where every frame fits in one of the timer's 10 ms ticks (220,000 instructions a millisecond), the time does
// not matter: the boxers' steps are the ticks', and the frames between two ticks have nothing to do. So the fast
// machine draws no scene (1c47:229c), and at the end of the frames' driver (2550:02ac) with no tick due it lets time
// go to the next one, event by event (the frame ends with their inputs, the keys, the timer's interrupts, in their
// order). The bug: a punch that misses (1eb6:19fa's miss path) takes the defender's stamina by a local never set
// ([bp - 0x12]): the stack's garbage there, left by the game's last whole frame (its drawing, its displays) or by
// that tick's own logic. The fast machine fills the stack below each tick's frame with a sentinel; when a miss finds
// it, the step is run again from its start with that tick held back for two whole frames (drawn, as the game draws
// between its ticks), which leave the game's garbage. Their drawing is the game's because the world's transforms of
// the camera and the boxers (1b1d:115a) are made at every tick: what they leave carries from frame to frame (made
// only for a replay's frames, a third of the misses read the game's garbage; made at every tick, all but about one
// in 200). (What a timer interrupt in the middle of the game's drawing leaves there, its music driver's locals, is
// not reproduced: the game's interrupts land where its drawing's instructions put them. Nor the time its drawing
// takes where the game's time is paused, 25bb:018e to 25bb:0164, after a round's bell: the ticks that pass there are
// lost to the fight's clock. A round's end to the next round's start goes as the game's: o4db_set_fast(0).)
#define FAST_SENTINEL 0xDEAD
#define FAST_FILL 3072        // (bytes of the stack below the frames' driver: 1eb6:19fa's local is 0xCE below)
#define DS_ELAPSED 0xCEF6     // the timer's milliseconds not yet taken by a frame (25bb:02b8 adds 10 a tick)
#define DS_TICKS 0xCF4A       // the timer's ticks (25bb:02b8's count)
#define DS_HW_TICKS 0x690E    // the timer's interrupts (2afc:0ec9's count)
O4DB_TLS int asm_fast, asm_skip_draw;
static O4DB_TLS uint16_t replayTicks[32];
static O4DB_TLS uint8_t replayDone[32];
static O4DB_TLS int nReplay, missAbort;
static O4DB_TLS uint64_t fastReplays;
static O4DB_TLS uint8_t *kept;  // the step's start, for its replays: a delta (keptPages) or a whole state
static O4DB_TLS size_t keptCap;
static O4DB_TLS int keptKind;   // 0 none, 1 a delta, 2 a whole state
static O4DB_TLS unsigned keptPages;
extern uint64_t game_next_event(void);
extern uint32_t game_cycles(void);

static uint16_t ds_word(uint16_t off) { const uint8_t *d = g_mem + ((uint32_t)g_dsSeg << 4) + off; return (uint16_t)(d[0] | d[1] << 8); }
static void ds_set(uint16_t off, uint16_t v) { mem_w16(((uint32_t)g_dsSeg << 4) + off, v); }  // (its page marked: mem_w16)
static int replay_index(uint16_t tick)
{
  for (int k = 0; k < nReplay; k++)
    if (replayTicks[k] == tick) return k;
  return -1;
}

// the frames' driver's end, before its stop: with no tick due (and no whole frames pending), time to the timer's next
// tick (its interrupt handler, 2afc:0ec9, counts them at DS:690E; the game's callback, 25bb:02b8, at DS:CF4A and adds
// the tick's 10 ms to DS:CEF6 unless the game is paused), event by event, as the game's interrupt lands in its
// frame's drawing: after the frame's logic took its time, before the caller's checks
// (the callers whose frames loop until a tick's checks, by their return addresses, CS 095E as loaded: the round's
// loop, 113b:0776's call of 113b:28e4 (returning to 0ac7) and its frame (the one view's, returning to 29b9), and the
// bell's waits, 113b:06a0's two "while (!2afc:3871()) frame" loops (06db, 075d); elsewhere a frame may be followed
// by a wait of its own, 113b:06a0's ring before 2afc:385d, 113b:0776's frame before 2afc:388b's 400 ms, and goes as
// the game's. The frames' driver's return address is above its BP, its caller's above the BP it saved. And the
// round's last frame, whose logic ended it (its clock at the round's length, or DS:CF0A set), goes as the game's too:
// 113b:28e4 rings the bell right after it, before the timer's next tick, the bell's waits counted from then)
static uint16_t stack_word(uint16_t a) { const uint8_t *stack = g_mem + ((uint32_t)R.ss << 4); return (uint16_t)(stack[a] | stack[(uint16_t)(a + 1)] << 8); }
static uint32_t ds_long(uint16_t off) { return (uint32_t)ds_word(off) | (uint32_t)ds_word((uint16_t)(off + 2)) << 16; }
static int fast_forward_caller(void)
{
  if (stack_word((uint16_t)(R.bp + 4)) != 0x095E) return 0;
  const uint16_t ip = stack_word((uint16_t)(R.bp + 2));
  if (ip == 0x06DB || ip == 0x075D) return 1;
  if (ip != 0x29B9) return 0;
  const uint16_t bp = stack_word(R.bp);
  if (stack_word((uint16_t)(bp + 4)) != 0x095E || stack_word((uint16_t)(bp + 2)) != 0x0AC7) return 0;
  return (int32_t)(ds_long(0x5ED8) - ds_long(0xCF02)) > 0 && ds_word(0xCF0A) == 0;  // (113b:29b9's check)
}

void asm_fast_frame(void)
{
  if (M.fastFullFrames > 0 || ds_word(DS_ELAPSED) != 0 || !fast_forward_caller()) return;
  const uint32_t cycles = game_cycles();
  const uint16_t hw = ds_word(DS_HW_TICKS), ticks = ds_word(DS_TICKS);
  for (int guard = 0; ds_word(DS_HW_TICKS) == hw && ds_word(DS_TICKS) == ticks && !asm_yield_req && R.if_; guard++)
  {
    const uint64_t t = game_next_event();
    if (t == UINT64_MAX || guard > 64) break;
    const uint64_t ic = (t * cycles * 1000 + PIT_HZ - 1) / PIT_HZ;
    if (ic > asm_icount) { asm_waits += ic - asm_icount; asm_icount = ic; }
    asm_back_slow();
  }
}

// a busy wait on the timer's count (2afc:385d's and 2afc:388b's loops): nothing changes until the timer's next
// interrupt, so time goes to it, event by event
void asm_fast_wait(void)
{
  const uint32_t cycles = game_cycles();
  const uint16_t hw = ds_word(DS_HW_TICKS);
  for (int guard = 0; ds_word(DS_HW_TICKS) == hw && !asm_yield_req && R.if_ && guard < 64; guard++)
  {
    const uint64_t t = game_next_event();
    if (t == UINT64_MAX) break;
    const uint64_t ic = (t * cycles * 1000 + PIT_HZ - 1) / PIT_HZ;
    if (ic > asm_icount) { asm_waits += ic - asm_icount; asm_icount = ic; }
    asm_back_slow();
  }
}

// the frames' driver's entry: the frame's drawing, a tick's start (the sentinel; a replay's whole frames first)
void asm_fast_tick(void)
{
  if (M.fastFullFrames > 0)
  {
    if (--M.fastFullFrames > 0) { M.fastDraw = 1; asm_skip_draw = 0; return; }
    // the whole frames drawn: the tick held back is the frame's
    ds_set(DS_ELAPSED, M.fastHeldTime);
    M.fastDraw = 0;
    asm_skip_draw = 1;
    return;
  }
  M.fastDraw = 0;
  asm_skip_draw = 1;
  if (ds_word(DS_ELAPSED) == 0) return;
  const int k = replay_index(ds_word(DS_TICKS));
  if (k >= 0)
  {
    if (replayDone[k]) return;
    // a replay's tick: held back for two whole frames (the game's frames between ticks, their garbage)
    replayDone[k] = 1;
    M.fastHeldTime = ds_word(DS_ELAPSED);
    ds_set(DS_ELAPSED, 0);
    M.fastFullFrames = 2;
    M.fastDraw = 1;
    asm_skip_draw = 0;
    return;
  }
  for (uint32_t a = (uint32_t)R.sp - FAST_FILL; a + 1 < R.sp; a += 2) mem_w16(((uint32_t)R.ss << 4) + (uint16_t)a, FAST_SENTINEL);
}

// 1eb6:19fa's miss path about to read its local [bp - 0x12]: the sentinel there is no garbage of the game's (the step
// again, the tick with whole frames before it)
void asm_fast_miss(void)
{
  const uint8_t *stack = g_mem + ((uint32_t)R.ss << 4);
  const uint16_t a = (uint16_t)(R.bp - 0x12);
  if ((stack[a] | stack[(uint16_t)(a + 1)] << 8) != FAST_SENTINEL) return;
  const uint16_t tick = ds_word(DS_TICKS);
  if (replay_index(tick) >= 0) return;  // (a replay's tick: the game's garbage, the sentinel's value by chance)
  if (nReplay == (int)(sizeof replayTicks / sizeof *replayTicks))
  {
    fprintf(stderr, "quickerOpen4DBoxing: more than %d missed punches in a step\n", nReplay);
    abort();
  }
  replayTicks[nReplay] = tick;
  replayDone[nReplay] = 0;
  nReplay++;
  missAbort = 1;
  longjmp(yieldJump, 1);
}

static void keep(const void *state, size_t size, int kind, unsigned pages)
{
  if (state == kept) return;
  if (size > keptCap) kept = realloc(kept, keptCap = size);
  memcpy(kept, state, size);
  keptKind = kind;
  keptPages = pages;
}

void o4db_set_fast(int on)
{
  asm_fast = on;
  asm_skip_draw = on && !M.fastDraw;
}
uint64_t o4db_fast_replays(void) { return fastReplays; }

static uint64_t fast_step(const O4dbInput *input)
{
  if (!keptKind)
  {
    // (a step not after a load: the machine's own copy of its state)
    if (o4db_state_size() > keptCap) kept = realloc(kept, keptCap = o4db_state_size());
    o4db_save(kept);
    keptKind = 2;
  }
  nReplay = 0;
  for (int attempt = 0;; attempt++)
  {
    if (attempt)
    {
      if (keptKind == 1) o4db_load_delta(kept, keptPages);
      else o4db_load(kept);
      if (asm_test_hook) asm_test_hook(0xFFFFFFFF);  // (tests: the step's events again)
      fastReplays++;
    }
    for (int k = 0; k < nReplay; k++) replayDone[k] = 0;
    missAbort = 0;
    stepping = 1;
    const uint64_t f = o4db_run_ahead(input, 1, 1);
    stepping = 0;
    if (!missAbort)
    {
      keptKind = 0;
      return f;
    }
  }
}

uint64_t o4db_step(const O4dbInput *input)
{
  if (!stepFn) return o4db_run(input, 1);
  if (asm_fast) return fast_step(input);
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
  asm_skip_draw = asm_fast && !M.fastDraw;
  if (!setjmp(yieldJump))
  {
    int code;
    if (!M.booted)
    {
      // Chimera's first step: its buttons before the game starts
      M.booted = 1;
      threadReady = 1;
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
  if (vga.planar || asm_vramCost || asm_tandyBase)
  {
    fprintf(stderr, "quickerOpen4DBoxing: the game left the MCGA build's video (planar %d, CGA waits %u, Tandy bank %u)\n", vga.planar, asm_vramCost, asm_tandyBase);
    abort();
  }
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

static void o4db_load_pristine(void)
{
  const uint8_t *p = pristine + sizeof M;
  o4db_state_all(load_part, &p);
  asm_dirty_all = 1;
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
    game_setup(&host);
    threadReady = 1;
  }
  if (asm_fast) keep(in, o4db_state_size(), 2, 0);
  const uint8_t *p = in;
  memcpy(&M, p, sizeof M);
  p += sizeof M;
  o4db_state_all(load_part, &p);
  asm_dirty_all = 1;
}

// ------------------------------------------------------------------------------------------- the states as deltas

#define PAGE 256
#define PAGES (sizeof g_mem / PAGE)
static pthread_mutex_t referenceLock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t *reference;  // the process's (every machine's states are deltas from the same memory)
static uint8_t unsaved[PAGES];  // pages no delta holds (o4db_set_unsaved)
static struct { uint16_t first, end; } savedRuns[PAGES / 2 + 1] = { { 0, PAGES } };  // the others, as runs of pages
static unsigned nSavedRuns = 1;

void o4db_set_unsaved(uint32_t start, uint32_t end)
{
  // (every thread's machine sets the same: only the first changes anything)
  pthread_mutex_lock(&referenceLock);
  int changed = 0;
  for (uint32_t p = start / PAGE; p <= end / PAGE && p < PAGES; p++)
    if (!unsaved[p]) unsaved[p] = 1, changed = 1;
  if (changed)
  {
    unsigned n = 0;
    for (unsigned p = 0; p < PAGES;)
    {
      if (unsaved[p]) { p++; continue; }
      savedRuns[n].first = (uint16_t)p;
      while (p < PAGES && !unsaved[p]) p++;
      savedRuns[n++].end = (uint16_t)p;
    }
    nSavedRuns = n;
  }
  pthread_mutex_unlock(&referenceLock);
}

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
    for (size_t p = 0; p < PAGES; p++)
      if (!unsaved[p] && memcmp(reference + p * PAGE, g_mem + p * PAGE, PAGE) != 0)
      {
        // (the environment's O4DB_REFERENCE_DIFF: the pages that differ listed)
        if (differ < 64 && getenv("O4DB_REFERENCE_DIFF"))
        {
          size_t first = 0;
          while (reference[p * PAGE + first] == g_mem[p * PAGE + first]) first++;
          fprintf(stderr, "quickerOpen4DBoxing: page %05zX differs from the reference (first at %05zX: %02X, the reference's %02X)\n", p * PAGE,
                  p * PAGE + first, g_mem[p * PAGE + first], reference[p * PAGE + first]);
        }
        differ++;
      }
  pthread_mutex_unlock(&referenceLock);
  // (the memory is the reference now: no page differs)
  if (!differ)
  {
    memset(asm_dirty, 0, PAGES);
    asm_dirty_all = 0;
  }
  return differ;
}

// The pages that may differ from the reference: those written since the last delta was loaded (every write of the
// program's marks its page, asm2c.h; the runtime's word writes, types.h; the BIOS's services and its tick, the BIOS's
// data page or everything, asm2c.c and game.c) and the loaded delta's own; every page after a whole state was loaded
// or a machine made (asm_dirty_all). A delta is then those pages compared, and a load restores only them. (The
// environment's O4DB_CHECK_DIRTY: every delta's pages also found by comparing all of them, a page missed an abort.)
O4DB_TLS u8 asm_dirty[PAGES];
O4DB_TLS int asm_dirty_all = 1;
static int check_dirty(void)
{
  static int check = -1;
  if (check < 0) check = getenv("O4DB_CHECK_DIRTY") != NULL;
  return check;
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
  if (asm_dirty_all)
  {
    for (unsigned r = 0; r < nSavedRuns; r++)
      for (size_t k = savedRuns[r].first; k < savedRuns[r].end; k++)
        if (memcmp(reference + k * PAGE, g_mem + k * PAGE, PAGE))
        {
          if (n == maxPages) return -1;
          index[n] = (uint16_t)k;
          memcpy(pages + (size_t)n * PAGE, g_mem + k * PAGE, PAGE);
          n++;
        }
  }
  else
  {
    for (size_t w = 0; w < PAGES / 8; w++)
    {
      uint64_t marks;
      memcpy(&marks, asm_dirty + 8 * w, 8);
      if (!marks) continue;
      for (size_t k = 8 * w; k < 8 * w + 8; k++)
        if (asm_dirty[k] && !unsaved[k] && memcmp(reference + k * PAGE, g_mem + k * PAGE, PAGE))
        {
          if (n == maxPages) return -1;
          index[n] = (uint16_t)k;
          memcpy(pages + (size_t)n * PAGE, g_mem + k * PAGE, PAGE);
          n++;
        }
    }
    if (check_dirty())
      for (size_t k = 0; k < PAGES; k++)
        if (!asm_dirty[k] && !unsaved[k] && memcmp(reference + k * PAGE, g_mem + k * PAGE, PAGE))
        {
          size_t first = 0;
          while (reference[k * PAGE + first] == g_mem[k * PAGE + first]) first++;
          fprintf(stderr, "quickerOpen4DBoxing: page %05zX differs from the reference unmarked (first at %05zX: %02X, the reference's %02X), frame %llu\n", k * PAGE,
                  k * PAGE + first, g_mem[k * PAGE + first], reference[k * PAGE + first], (unsigned long long)M.frames);
          abort();
        }
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
    game_setup(&host);
    threadReady = 1;
  }
  if (asm_fast) keep(in, o4db_delta_size(maxPages), 1, maxPages);
  const uint8_t *p = in;
  memcpy(&M, p, sizeof M);
  p += sizeof M;
  o4db_state_all(load_rest, &p);
  const uint16_t *count = (const uint16_t *)p, *index = count + 1;
  const uint8_t *pages = (const uint8_t *)(index + maxPages);
  if (asm_dirty_all)
    for (unsigned r = 0; r < nSavedRuns; r++)
      memcpy(g_mem + (size_t)savedRuns[r].first * PAGE, reference + (size_t)savedRuns[r].first * PAGE, (size_t)(savedRuns[r].end - savedRuns[r].first) * PAGE);
  else
    for (size_t w = 0; w < PAGES / 8; w++)
    {
      uint64_t marks;
      memcpy(&marks, asm_dirty + 8 * w, 8);
      if (!marks) continue;
      for (size_t k = 8 * w; k < 8 * w + 8; k++)
        if (asm_dirty[k] && !unsaved[k]) memcpy(g_mem + k * PAGE, reference + k * PAGE, PAGE);
    }
  memset(asm_dirty, 0, PAGES);
  asm_dirty_all = 0;
  for (unsigned k = 0; k < *count; k++)
  {
    memcpy(g_mem + (size_t)index[k] * PAGE, pages + (size_t)k * PAGE, PAGE);
    asm_dirty[index[k]] = 1;
  }
}

void (*asm_test_hook)(u32 fn);
unsigned asm_test_skip;
#ifdef O4DB_TRACE
u32 asm_trace_ip;
void (*asm_trace_read)(u32 addr, u16 value, int size);
void (*asm_trace_write)(u32 addr, u16 value, int size);
void (*asm_trace_pre_read)(u32 addr, int size);
void o4db_test_trace_pre_reads(void (*hook)(uint32_t addr, int size)) { asm_trace_pre_read = hook; }
void o4db_test_trace_reads(void (*hook)(uint32_t addr, uint16_t value, int size)) { asm_trace_read = hook; }
void o4db_test_trace_writes(void (*hook)(uint32_t addr, uint16_t value, int size)) { asm_trace_write = hook; }
uint32_t o4db_test_trace_ip(void) { return asm_trace_ip; }
#endif
void o4db_test_hooks(void (*eventHook)(uint32_t fn), unsigned skip)
{
  asm_test_hook = eventHook;
  asm_test_skip = skip;
}

void o4db_test_set_cycles(uint32_t cycles)
{
  asm_icount = asm_icount * cycles / host.cycles;
  asm_waits = asm_waits * cycles / host.cycles;
  host.cycles = cycles;
  asm_ioCostIn = (u32)((uint64_t)cycles * 1020 / 1000000);
  asm_ioCostOut = (u32)((uint64_t)cycles * 1020 * 3 / 4000000);
  asm_ioCostIn16 = (u32)((uint64_t)cycles * 660 / 1000000);
  asm_ioCostOut16 = (u32)((uint64_t)cycles * 660 * 3 / 4000000);
}

const uint32_t *o4db_test_chain(int *depth)
{
  *depth = asm_sdepth;
  return (const uint32_t *)asm_shadow;
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
