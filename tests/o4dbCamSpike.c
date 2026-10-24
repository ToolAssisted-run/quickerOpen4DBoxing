// (spike) What a camera's function key changes: o4dbCamSpike GAMEDIR MOVIE PLAN AT K KEY OUTDIR
// The environment's STATE (a fight's start, at SPEED 220,000); PLAN's rows played to frame AT, the machine saved; then K
// more frames twice, as the plan, and with KEY (a mnemonic: A F G H I J M P S V for F1-F10) held their first 3: the
// data segment's bytes that differ after them printed as ranges, both screens written (OUTDIR/plain.ppm, key.ppm).
// Other modes, by the environment: STATEDIFF (the whole states' bytes instead), REGIONS (the camera's memory, mapped),
// RENDER (a copy of each frame re-run with the keys, the camera's regions carried from copy to copy), SHADOW (a second
// machine kept from frame to frame, the fight's state given it at each frame's start: the design that works, the
// cameras' pictures as the game's, the fight the machine's).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "movie.h"

static void shot(const char *path)
{
  FILE *f = fopen(path, "wb");
  fprintf(f, "P6\n320 200\n255\n");
  const uint8_t *s = o4db_screen();
  const uint8_t(*dac)[3] = o4db_palette();
  for (int k = 0; k < 320 * 200; k++)
    for (int c = 0; c < 3; c++) fputc(dac[s[k]][c] << 2 | dac[s[k]][c] >> 4, f);
  fclose(f);
}

int main(int argc, char **argv)
{
  if (argc < 8 || !getenv("STATE")) { fprintf(stderr, "usage: STATE=... o4dbCamSpike GAMEDIR MOVIE PLAN AT K KEY OUTDIR\n"); return 2; }
  o4db_set_speed(getenv("SPEED") ? (uint32_t)strtoul(getenv("SPEED"), NULL, 10) : 220000);
  if (!o4db_setup(argv[1], "mt32", "1991-07-30 14:47:00", NULL)) { fprintf(stderr, "%s\n", o4db_error()); return 1; }
  size_t n, np;
  MovieInput *rows = movie_load(argv[2], &n), *plan = movie_load(argv[3], &np);
  const size_t at = strtoull(argv[4], NULL, 10), K = strtoull(argv[5], NULL, 10);
  const O4dbButtons key = (O4dbButtons)1 << (strchr(k_mnemonics + 57, argv[6][0]) - k_mnemonics);
  const O4dbInput boot = { rows[0].buttons, rows[0].mouseX, rows[0].mouseY };
  o4db_new(&boot);
  void *st = malloc(o4db_state_size());
  FILE *f = fopen(getenv("STATE"), "rb");
  if (!f || fread(st, 1, o4db_state_size(), f) != o4db_state_size()) { fprintf(stderr, "no state\n"); return 1; }
  fclose(f);
  o4db_load(st);
  for (size_t k = 0; k < at && k < np; k++) { O4dbInput in = { plan[k].buttons, plan[k].mouseX, plan[k].mouseY }; o4db_run(&in, 1); }
  o4db_save(st);
  // (REGIONS=N: for each function key, N frames as the plan and with the key held the first 3; the memory's bytes that
  // ever differ at a frame end, but the fight's (the search's hash) and the pictures, merged into ranges)
  if (getenv("REGIONS"))
  {
    const size_t N = strtoull(getenv("REGIONS"), NULL, 10);
    static uint8_t mark[0x110000], base[1200][0x2000];
    static uint8_t frameMem[0x110000];
    (void)base;
    uint8_t *plainMem = malloc((size_t)N * 0x110000);
    for (int key = 0; key < 10; key++)
      for (int pass = 0; pass < 2; pass++)
      {
        o4db_load(st);
        for (size_t k = 0; k < N; k++)
        {
          const size_t i = at + k < np ? at + k : np - 1;
          O4dbInput in = { plan[i].buttons | (pass && k < 3 ? (O4dbButtons)1 << (57 + key) : 0), plan[i].mouseX, plan[i].mouseY };
          o4db_run(&in, 1);
          if (pass == 0 && key == 0) memcpy(plainMem + k * 0x110000, o4db_memory(), 0x110000);
          else if (pass == 1)
          {
            memcpy(frameMem, o4db_memory(), 0x110000);
            for (size_t b = 0; b < 0x110000; b++) if (frameMem[b] != plainMem[k * 0x110000 + b]) mark[b] = 1;
          }
        }
      }
    // the fight's (as the search hashes them, in the data segment) and the pictures left out
    const uint32_t dsb = (uint32_t)o4db_data_segment() << 4;
    static const struct { uint16_t off, n; } fight[] = {
      { 0xBA12, 0xF4 }, { 0xBB06, 0xF4 }, { 0xA488, 0x74 }, { 0xA4FC, 0x74 }, { 0x5A1C, 4 },   { 0x8472, 4 },   { 0x89F6, 10 },
      { 0x8A40, 2 },    { 0x8CC8, 16 },   { 0x8E3E, 8 },    { 0x9302, 2 },    { 0xA6B0, 16 }, { 0xAEC0, 2 },   { 0xB130, 16 },
      { 0xB6B8, 0x48 }, { 0xBD08, 12 },   { 0xCC58, 4 },    { 0xCF02, 0x60 },
    };
    for (size_t k = 0; k < sizeof fight / sizeof *fight; k++)
      for (int b = 0; b < fight[k].n; b++) if (mark[dsb + fight[k].off + b] == 1) mark[dsb + fight[k].off + b] = 2;
    for (uint32_t b = 0xA0000; b < 0xB0000; b++) mark[b] = 0;
    for (uint32_t b = 0x68000; b < 0x72000; b++) mark[b] = 0;
    for (uint32_t b = 0x88000; b < 0x89000; b++) mark[b] = 0;
    int nr = 0, fightBytes = 0;
    for (uint32_t b = 0; b < 0x110000; b++) fightBytes += mark[b] == 2;
    for (uint32_t b = 0; b < 0x110000;)
    {
      if (mark[b] != 1) { b++; continue; }
      uint32_t e = b, last = b;
      while (e < 0x110000 && e <= last + 16) { if (mark[e] == 1) last = e; e++; }
      const int inDs = b >= dsb && last < dsb + 0x10000;
      printf("%05X-%05X (%u)%s%04X\n", b, last, last - b + 1, inDs ? "  DS:" : "", inDs ? b - dsb : 0);
      nr++;
      b = last + 1;
    }
    printf("%d ranges; %d bytes of the fight's own regions differ too\n", nr, fightBytes);
    return 0;
  }
  // (SHADOW=M: M frames from AT, a frame at a time: the machine one frame as the plan, its pictures to OUTDIR/main.rgb;
  // and a second machine, the shadow, its own state kept from frame to frame (its stack, its keys, its pictures, its
  // camera): at each frame's start the fight's own regions (the search's hash's, but COSMETIC's: "OFF:LEN,..." hex)
  // given it from the machine's, then one frame at RENDER_CYCLES with CAMS's keys (pressed through its keyboard),
  // its pictures to OUTDIR/render.rgb)
  if (getenv("SHADOW"))
  {
    const size_t M = strtoull(getenv("SHADOW"), NULL, 10);
    char path[4096];
    const uint32_t dsb = (uint32_t)o4db_data_segment() << 4;
    static const struct { uint16_t off, n; } fight[] = {
      { 0xBA12, 0xF4 }, { 0xBB06, 0xF4 }, { 0xA488, 0x74 }, { 0xA4FC, 0x74 }, { 0x5A1C, 4 },   { 0x8472, 4 },   { 0x89F6, 10 },
      { 0x8A40, 2 },    { 0x8CC8, 16 },   { 0x8E3E, 8 },    { 0x9302, 2 },    { 0xA6B0, 16 }, { 0xAEC0, 2 },   { 0xB130, 16 },
      { 0xB6B8, 0x48 }, { 0xBD08, 12 },   { 0xCC58, 4 },    { 0xCF02, 0x60 },
    };
    static uint8_t isFight[0x10000];
    for (size_t k = 0; k < sizeof fight / sizeof *fight; k++)
      for (int b = 0; b < fight[k].n; b++) isFight[fight[k].off + b] = 1;
    for (const char *c = getenv("COSMETIC"); c && *c;)
    {
      char *e;
      const uint32_t off = (uint32_t)strtoul(c, &e, 16), len = *e == ':' ? (uint32_t)strtoul(e + 1, &e, 16) : 1;
      for (uint32_t x = off; x < off + len && x < 0x10000; x++) isFight[x] = 0;
      c = *e == ',' ? e + 1 : e;
      if (c == e && *c) break;
    }
    // (EXTRA="OFF:LEN,...": more of the data segment given the shadow from the machine)
    for (const char *c = getenv("EXTRA"); c && *c;)
    {
      char *e;
      const uint32_t off = (uint32_t)strtoul(c, &e, 16), len = *e == ':' ? (uint32_t)strtoul(e + 1, &e, 16) : 1;
      for (uint32_t x = off; x < off + len && x < 0x10000; x++) isFight[x] = 1;
      c = *e == ',' ? e + 1 : e;
      if (c == e && *c) break;
    }
    const uint32_t cycles = getenv("RENDER_CYCLES") ? (uint32_t)strtoul(getenv("RENDER_CYCLES"), NULL, 10) : 2200000;
    const uint32_t speed = getenv("SPEED") ? (uint32_t)strtoul(getenv("SPEED"), NULL, 10) : 220000;
    struct { size_t frame; O4dbButtons key; } cams[64];
    int nc = 0;
    for (const char *c = getenv("CAMS"); c && *c && nc < 64;)
    {
      char *e;
      cams[nc].frame = strtoull(c, &e, 10);
      if (*e != ':' || !e[1]) break;
      cams[nc++].key = (O4dbButtons)1 << (strchr(k_mnemonics + 57, e[1]) - k_mnemonics);
      c = e + 2;
      if (*c == ',') c++;
    }
    snprintf(path, sizeof path, "%s/main.rgb", argv[7]);
    FILE *mf = fopen(path, "wb");
    snprintf(path, sizeof path, "%s/render.rgb", argv[7]);
    FILE *rf = fopen(path, "wb");
    void *shadow = malloc(o4db_state_size()), *after = malloc(o4db_state_size());
    static uint8_t given[0x10000];
    o4db_load(st);
    o4db_save(shadow);
    static uint8_t rgb[320 * 200 * 3];
    for (size_t k = 0; k < M; k++)
    {
      const size_t i = at + k < np ? at + k : np - 1;
      O4dbInput in = { plan[i].buttons, plan[i].mouseX, plan[i].mouseY };
      if (getenv("VERBOSE")) fprintf(stderr, "frame %zu\n", k);
      memcpy(given, o4db_memory() + dsb, 0x10000);
      o4db_run(&in, 1);
      o4db_save(after);
      static uint8_t mainAfter[0x10000];
      memcpy(mainAfter, o4db_memory() + dsb, 0x10000);
      const uint8_t *scr = o4db_screen();
      const uint8_t(*dac)[3] = o4db_palette();
      for (int x = 0; x < 320 * 200; x++)
        for (int c = 0; c < 3; c++) rgb[3 * x + c] = (uint8_t)(dac[scr[x]][c] << 2 | dac[scr[x]][c] >> 4);
      fwrite(rgb, 1, sizeof rgb, mf);
      // the shadow: the fight given, the keys, faster
      o4db_load(shadow);
      uint8_t *d = o4db_memory() + dsb;
      for (uint32_t x = 0; x < 0x10000; x++) if (isFight[x]) d[x] = given[x];
      o4db_test_set_cycles(cycles);
      O4dbInput rin = in;
      for (int c = 0; c < nc; c++) if (k >= cams[c].frame && k < cams[c].frame + 3) rin.buttons |= cams[c].key;
      o4db_run(&rin, 1);
      // (SYNC: the frames whose fight, after them, is not the machine's, and by how many bytes)
      if (getenv("SYNC"))
      {
        const uint8_t *sd = o4db_memory() + dsb;
        int n = 0;
        char list[512];
        int len = 0;
        for (uint32_t x = 0; x < 0x10000; x++)
          if (isFight[x] && sd[x] != mainAfter[x])
          {
            n++;
            if (len < 480) len += snprintf(list + len, sizeof list - (size_t)len, " %04X", x);
          }
        if (n) printf("frame %zu: %d bytes of the fight differ:%s\n", k, n, len ? list : "");
        if (sd[0xCF02] != mainAfter[0xCF02])
          printf("  clock %u (the machine's %u), ticks %u (%u), elapsed %u (%u)\n", sd[0xCF02] | sd[0xCF03] << 8,
                 mainAfter[0xCF02] | mainAfter[0xCF03] << 8, sd[0xCF4A] | sd[0xCF4B] << 8, mainAfter[0xCF4A] | mainAfter[0xCF4B] << 8,
                 sd[0xCEF6] | sd[0xCEF7] << 8, mainAfter[0xCEF6] | mainAfter[0xCEF7] << 8);
      }
      scr = o4db_screen();
      dac = o4db_palette();
      for (int x = 0; x < 320 * 200; x++)
        for (int c = 0; c < 3; c++) rgb[3 * x + c] = (uint8_t)(dac[scr[x]][c] << 2 | dac[scr[x]][c] >> 4);
      fwrite(rgb, 1, sizeof rgb, rf);
      o4db_test_set_cycles(speed);  // (its instructions counted at the machine's speed again before it is kept)
      o4db_save(shadow);
      o4db_load(after);
    }
    fclose(mf);
    fclose(rf);
    printf("%zu frames rendered\n", M);
    return 0;
  }
  // (RENDER=M: M frames from AT, a frame at a time: the machine one frame as the plan (no function keys), its pictures
  // to OUTDIR/main.rgb; and from the frame's start again, the camera's ranges (REGFILE: REGIONS's output, its bytes
  // and the fight's own that differ too taken from the render before) put in, one frame at RENDER_CYCLES (2,200,000)
  // with CAMS ("FRAME:KEY,...", held 3 frames from FRAME) added: its picture to OUTDIR/render.rgb, its camera's bytes
  // kept for the next; the machine then back where the plain frame left it)
  if (getenv("RENDER"))
  {
    const size_t M = strtoull(getenv("RENDER"), NULL, 10);
    char path[4096];
    static uint8_t inRange[0x110000];
    FILE *rf = fopen(getenv("REGFILE"), "r");
    char line[256];
    while (rf && fgets(line, sizeof line, rf))
    {
      unsigned a, b;
      if (sscanf(line, "%x-%x", &a, &b) == 2 && b < 0x110000) for (unsigned x = a; x <= b; x++) inRange[x] = 1;
    }
    if (rf) fclose(rf);
    // (the fight's own regions' bytes that the cameras change: the data segment's CF02 block's past its clock, and
    // the pose blocks', found differing by REGIONS; taken whole here, the render's own)
    const uint32_t dsb = (uint32_t)o4db_data_segment() << 4;
    for (uint32_t x = 0xCF06; x < 0xCF62; x++) inRange[dsb + x] = 1;
    // (CARRYDS: the whole data segment but the fight's own regions (as REGIONS leaves them out), instead of its ranges)
    if (getenv("CARRYDS"))
    {
      for (uint32_t x = 0; x < 0x10000; x++) inRange[dsb + x] = 1;
      static const struct { uint16_t off, n; } fight[] = {
        { 0xBA12, 0xF4 }, { 0xBB06, 0xF4 }, { 0xA488, 0x74 }, { 0xA4FC, 0x74 }, { 0x5A1C, 4 },   { 0x8472, 4 },   { 0x89F6, 10 },
        { 0x8A40, 2 },    { 0x8CC8, 16 },   { 0x8E3E, 8 },    { 0x9302, 2 },    { 0xA6B0, 16 }, { 0xAEC0, 2 },   { 0xB130, 16 },
        { 0xB6B8, 0x48 }, { 0xBD08, 12 },   { 0xCC58, 4 },    { 0xCF02, 4 },
      };
      for (size_t k = 0; k < sizeof fight / sizeof *fight; k++)
        for (int b = 0; b < fight[k].n; b++) inRange[dsb + fight[k].off + b] = 0;
    }
    // (the pictures too: the screen and the game's pictures behind it, which REGIONS leaves out; the game redraws only
    // what changed, on the render before's; NOPICTURES: not)
    if (!getenv("NOPICTURES"))
    {
      for (uint32_t x = 0xA0000; x < 0xB0000; x++) inRange[x] = 1;
      for (uint32_t x = 0x68000; x < 0x72000; x++) inRange[x] = 1;
      for (uint32_t x = 0x88000; x < 0x89000; x++) inRange[x] = 1;
    }
    // (the limbs' drawing order (5A14-5A2B: eleven words, the camera's), though the search hashes 5A1C-5A1F of it)
    for (uint32_t x = 0x5A14; x < 0x5A2C; x++) inRange[dsb + x] = 1;
    // (STACKFROM=X: the stack from X up the main machine's, its dead part too: the fight reads a local it did not set)
    if (getenv("STACKFROM"))
      for (uint32_t x = (uint32_t)strtoul(getenv("STACKFROM"), NULL, 16); x < 0x10000; x++) inRange[dsb + x] = 0;
    // (CARRY="OFF:LEN,...": more of the data segment, hex)
    for (const char *c = getenv("CARRY"); c && *c;)
    {
      char *e;
      const uint32_t off = (uint32_t)strtoul(c, &e, 16), len = *e == ':' ? (uint32_t)strtoul(e + 1, &e, 16) : 1;
      for (uint32_t x = off; x < off + len && x < 0x10000; x++) inRange[dsb + x] = 1;
      c = *e == ',' ? e + 1 : e;
      if (c == e && *c) break;
    }
    // (not the keyboard's: the handler's ring buffer and flags, the copy's own keys)
    for (uint32_t x = 0x6C7C; x < 0x6D62; x++) inRange[dsb + x] = 0;
    static uint8_t cam[0x110000];
    int haveCam = 0;
    const uint32_t cycles = getenv("RENDER_CYCLES") ? (uint32_t)strtoul(getenv("RENDER_CYCLES"), NULL, 10) : 2200000;
    struct { size_t frame; O4dbButtons key; } cams[64];
    int nc = 0;
    for (const char *c = getenv("CAMS"); c && *c && nc < 64;)
    {
      char *e;
      cams[nc].frame = strtoull(c, &e, 10);
      if (*e != ':' || !e[1]) break;
      cams[nc++].key = (O4dbButtons)1 << (strchr(k_mnemonics + 57, e[1]) - k_mnemonics);
      c = e + 2;
      if (*c == ',') c++;
    }
    snprintf(path, sizeof path, "%s/main.rgb", argv[7]);
    FILE *mf = fopen(path, "wb");
    setvbuf(mf, NULL, _IONBF, 0);
    snprintf(path, sizeof path, "%s/render.rgb", argv[7]);
    FILE *rf2 = fopen(path, "wb");
    setvbuf(rf2, NULL, _IONBF, 0);
    void *start = malloc(o4db_state_size()), *after = malloc(o4db_state_size());
    o4db_load(st);
    for (size_t k = 0; k < M; k++)
    {
      const size_t i = at + k < np ? at + k : np - 1;
      O4dbInput in = { plan[i].buttons, plan[i].mouseX, plan[i].mouseY };
      if (getenv("VERBOSE")) fprintf(stderr, "frame %zu\n", k);
      o4db_save(start);
      o4db_run(&in, 1);
      o4db_save(after);
      static uint8_t rgb[320 * 200 * 3];
      const uint8_t *scr = o4db_screen();
      const uint8_t(*dac)[3] = o4db_palette();
      for (int x = 0; x < 320 * 200; x++)
        for (int c = 0; c < 3; c++) rgb[3 * x + c] = (uint8_t)(dac[scr[x]][c] << 2 | dac[scr[x]][c] >> 4);
      fwrite(rgb, 1, sizeof rgb, mf);
      // the render: the frame's start, the camera's bytes of the render before, the keys, faster
      o4db_load(start);
      uint8_t *mem = o4db_memory();
      // (NOOVERLAY: none; else not the stack's live part, from its pointer up: the frames' return addresses and locals)
      const uint32_t live = dsb + o4db_stack_pointer();
      if (haveCam && !getenv("NOOVERLAY"))
        for (uint32_t x = 0; x < 0x110000; x++) if (inRange[x] && !(x >= live && x < dsb + 0x10000)) mem[x] = cam[x];
      o4db_test_set_cycles(cycles);
      // (the keys not through the keyboard (the handler's interrupt comes a frame late, the copy gone by then): what
      // the game's handler (2afc:193a) does put in its memory: the press's frame, the code in its ring buffer; held,
      // its flag)
      O4dbInput rin = in;
      uint8_t *d = mem + dsb;
      for (int c = 0; c < nc; c++)
      {
        const uint16_t scan = (uint16_t)(0x3B + (__builtin_ctzll((uint64_t)(cams[c].key >> 57))));
        if (k == cams[c].frame && !getenv("KEYS"))
        {
          d[0x6d04] = (uint8_t)scan, d[0x6d05] = 0;
          const uint16_t tbl = d[0x6d40] & 1 ? (d[0x6d25] & 1 ? 0 : 0x6ece) : d[0x6d25] & 1 ? 0x6e73
                             : (d[0x6d32] & 1) || (d[0x6d3e] & 1) ? 0x6dbd : d[0x6d42] & 1 ? 0x6e18 : 0x6d62;
          if (tbl)
          {
            uint8_t al = d[tbl + scan];
            uint16_t ax = al;
            if (al & 0x80) { uint8_t ah = al; if (ah >= 0x85) ah &= 0x7f; ax = (uint16_t)(ah << 8); }
            uint16_t bx = (uint16_t)(d[0x6cfc] | d[0x6cfd] << 8);
            const uint16_t size = (uint16_t)(d[0x6d00] | d[0x6d01] << 8);
            d[0x6c7c + bx] = (uint8_t)ax, d[0x6c7d + bx] = (uint8_t)(ax >> 8);
            bx = (uint16_t)(bx + 2);
            if (bx >= size) bx = 0;
            d[0x6cfc] = (uint8_t)bx, d[0x6cfd] = (uint8_t)(bx >> 8);
            uint16_t n = (uint16_t)((d[0x6d02] | d[0x6d03] << 8) + 2);
            if (n > size) { n = size; d[0x6cfe] = d[0x6cfc], d[0x6cff] = d[0x6cfd]; }
            d[0x6d02] = (uint8_t)n, d[0x6d03] = (uint8_t)(n >> 8);
          }
        }
        if (k >= cams[c].frame && k < cams[c].frame + 3)
        {
          if (getenv("KEYS")) rin.buttons |= cams[c].key;
          else d[0x6d08 + scan] = 1;
        }
      }
      o4db_run(&rin, 1);
      mem = o4db_memory();
      // (DEBUGDIFF=K: the copy's data segment against the plain frame's at frame K, by range)
      if (getenv("DEBUGDIFF") && k <= strtoull(getenv("DEBUGDIFF"), NULL, 10))
      {
        static uint8_t copyDs[0x10000];
        memcpy(copyDs, mem + dsb, 0x10000);
        void *tmp = malloc(o4db_state_size());
        o4db_save(tmp);
        o4db_load(after);
        const uint8_t *real = o4db_memory() + dsb;
        printf("frame %zu:", k);
        for (int x = 0; x < 0x10000;)
        {
          if (copyDs[x] == real[x]) { x++; continue; }
          int j = x;
          while (j < 0x10000 && (copyDs[j] != real[j] || (j + 1 < 0x10000 && copyDs[j + 1] != real[j + 1]))) j++;
          if (x < 0x8800 || x >= 0x9800) printf(" %04X-%04X", x, j - 1);
          x = j;
        }
        printf("\n");
        o4db_load(tmp);
        free(tmp);
        mem = o4db_memory();
      }
      for (uint32_t x = 0; x < 0x110000; x++) if (inRange[x]) cam[x] = mem[x];
      haveCam = 1;
      scr = o4db_screen();
      dac = o4db_palette();
      for (int x = 0; x < 320 * 200; x++)
        for (int c = 0; c < 3; c++) rgb[3 * x + c] = (uint8_t)(dac[scr[x]][c] << 2 | dac[scr[x]][c] >> 4);
      fwrite(rgb, 1, sizeof rgb, rf2);
      o4db_test_set_cycles(getenv("SPEED") ? (uint32_t)strtoul(getenv("SPEED"), NULL, 10) : 220000);
      o4db_load(after);
    }
    fclose(mf);
    fclose(rf2);
    printf("%zu frames rendered\n", M);
    return 0;
  }
  // (STATEDIFF: the whole states after K frames as the plan and with the key: the bytes that differ, by offset)
  if (getenv("STATEDIFF"))
  {
    const size_t n = o4db_state_size();
    uint8_t *sa[2] = { malloc(n), malloc(n) };
    for (int pass = 0; pass < 2; pass++)
    {
      o4db_load(st);
      for (size_t k = 0; k < K; k++)
      {
        const size_t i = at + k < np ? at + k : np - 1;
        O4dbInput in = { plan[i].buttons | (pass && k < 3 ? key : 0), plan[i].mouseX, plan[i].mouseY };
        o4db_run(&in, 1);
      }
      o4db_save(sa[pass]);
    }
    for (size_t b = 0; b < n; b++) if (sa[0][b] != sa[1][b]) printf("state byte %zu: %02X>%02X\n", b, sa[0][b], sa[1][b]);
    return 0;
  }
  static uint8_t ds[2][0x10000];
  char path[4096];
  for (int pass = 0; pass < 2; pass++)
  {
    o4db_load(st);
    for (size_t k = 0; k < K; k++)
    {
      const size_t i = at + k < np ? at + k : np - 1;
      O4dbInput in = { plan[i].buttons | (pass && k < 3 ? key : 0), plan[i].mouseX, plan[i].mouseY };
      o4db_run(&in, 1);
    }
    memcpy(ds[pass], o4db_memory() + ((uint32_t)o4db_data_segment() << 4), 0x10000);
    snprintf(path, sizeof path, "%s/%s.ppm", argv[7], pass ? "key" : "plain");
    shot(path);
  }
  int nr = 0;
  for (int k = 0; k < 0x10000;)
  {
    if (ds[0][k] == ds[1][k]) { k++; continue; }
    int j = k;
    while (j < 0x10000 && (ds[0][j] != ds[1][j] || (j + 1 < 0x10000 && ds[0][j + 1] != ds[1][j + 1]))) j++;
    if (nr++ < 80)
    {
      printf("%04X-%04X (%d):", k, j - 1, j - k);
      for (int b = k; b < j && b < k + 8; b++) printf(" %02X>%02X", ds[0][b], ds[1][b]);
      printf("\n");
    }
    k = j;
  }
  printf("%d ranges differ\n", nr);
  return 0;
}
