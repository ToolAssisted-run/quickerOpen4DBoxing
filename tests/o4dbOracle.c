// Open4DBoxing itself plays a movie (movie.h) as Chimera's core does (boxing-driver.c: the first row before the game
// starts, row k+1 at the k-th frame end), printing at every frame end, before its row is given, the frame and the hash
// o4dbPlay prints: o4dbOracle GAMEDIR MOVIE [SOUND] [CLOCK]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "asm2c.h"
#include "game.h"
#include "movie.h"

static MovieInput *rows;
static size_t nRows, next;
static uint8_t held[67];
static int32_t given[2] = { 160, 100 };
static const uint8_t k_scan[67] = {
  0, 0, 0x48, 0x50, 0x4B, 0x4D, 0x47, 0x49, 0x4F, 0x51, 0x4C, 0x52,
  0x1C, 0x39, 0x0E, 0x01, 0x0F, 0x2A, 0x36, 0x1D, 0x38,
  0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F,
  0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C,
  0x0B, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
  0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44,
};
static int modifier(int b) { return b == 17 || b == 18 || b == 19 || b == 20; }

static void apply(const MovieInput *in)
{
  for (int pass = 0; pass < 3; pass++)
    for (int b = 0; b < 67; b++)
    {
      const uint8_t want = (uint8_t)((in->buttons >> b) & 1);
      if (want == held[b]) continue;
      const int down = want;
      if ((pass == 0 && !(modifier(b) && down)) || (pass == 1 && modifier(b)) || (pass == 2 && !(modifier(b) && !down))) continue;
      held[b] = want;
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
  if (x != given[0] || y != given[1])
  {
    given[0] = x;
    given[1] = y;
    const int dx = x * 2, dy = y;
    asm_mouse.dx = (i16)(asm_mouse.dx + (dx - asm_mouse.x));
    asm_mouse.dy = (i16)(asm_mouse.dy + (dy - asm_mouse.y));
    asm_mouse.x = (u16)dx;
    asm_mouse.y = (u16)dy;
    asm_mouse_clamp();
  }
}

static bool frame(void *ctx, uint64_t f)
{
  (void)ctx;
  uint64_t h = hash_bytes(g_mem, 0xB0000, 0xcbf29ce484222325ull);
  h = hash_bytes((const uint8_t *)&R, 24, h);
  printf("%llu %016llx\n", (unsigned long long)f, (unsigned long long)h);
  if (next >= nRows) return false;
  apply(&rows[next++]);
  return true;
}

static int64_t days_from_civil(int64_t y, int m, int d)
{
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

int main(int argc, char **argv)
{
  if (argc < 3) { fprintf(stderr, "usage: o4dbOracle GAMEDIR MOVIE [SOUND] [CLOCK]\n"); return 2; }
  rows = movie_load(argv[2], &nRows);
  if (!rows || nRows < 2) { fprintf(stderr, "o4dbOracle: no movie in %s\n", argv[2]); return 1; }
  const char *sound = argc > 3 ? argv[3] : "mt32", *clk = argc > 4 ? argv[4] : "1991-07-30 14:47:00";
  int y, mo, d, h, mi, s;
  sscanf(clk, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s);
  char cmd[32], wdir[64];
  snprintf(cmd, sizeof cmd, "n %s15.drv d", !strcmp(sound, "mt32") ? "mt" : !strcmp(sound, "sb") ? "sb" : !strcmp(sound, "adlib") ? "ad" : !strcmp(sound, "tandy") ? "td" : "pc");
  snprintf(wdir, sizeof wdir, "/dev/shm/o4dboracle-%d", (int)getpid());
  char mk[96];
  snprintf(mk, sizeof mk, "mkdir -p %s", wdir);
  if (system(mk)) return 1;
  GameHost host = { .gameDir = argv[1], .writeDir = wdir, .program = "4DBMCGA.EXE", .cmdline = cmd, .frame = frame,
                    .cycles = getenv("SPEED") ? (uint32_t)strtoul(getenv("SPEED"), NULL, 10) : 22000,  // (the environment's SPEED: the PC's speed)
                    .startTime = days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s };
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  apply(&rows[0]);
  next = 1;
  game_run(&host);
  clock_gettime(CLOCK_MONOTONIC, &t1);
  fprintf(stderr, "o4dbOracle: %zu rows, %.2f s\n", nRows, (double)(t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9);
  snprintf(mk, sizeof mk, "rm -rf %s", wdir);
  if (system(mk)) return 1;
  return 0;
}
