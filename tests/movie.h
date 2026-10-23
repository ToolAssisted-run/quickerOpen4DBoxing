// A movie for the players: Chimera's rows ("|  160,  100,<57 buttons>|": the mouse's place, then a mnemonic or '.'
// per button) or the gate's lines (the mnemonics held, "@X,Y" where the mouse moves; its first line a comment)
#ifndef O4DB_TESTS_MOVIE_H
#define O4DB_TESTS_MOVIE_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char k_mnemonics[] = "<>UDLRQEZCKON_BXT^!%&abcdefghijklmnopqrstuvwxyz0123456789";

typedef struct
{
  uint64_t buttons;
  int16_t mouseX, mouseY;
} MovieInput;

// the movie's inputs (*n of them); NULL if it cannot be read
static MovieInput *movie_load(const char *path, size_t *n)
{
  FILE *f = fopen(path, "r");
  if (!f) return NULL;
  size_t cap = 1024, count = 0;
  MovieInput *in = malloc(cap * sizeof *in);
  char line[4096];
  int x = 160, y = 100, first = 1;
  while (fgets(line, sizeof line, f))
  {
    size_t len = strlen(line);
    while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
    if (first && line[0] == '#') { first = 0; continue; }
    first = 0;
    if (line[0] == '[' || !strncmp(line, "LogKey", 6)) continue;
    MovieInput m = { 0, (int16_t)x, (int16_t)y };
    if (line[0] == '|')
    {
      int mx, my;
      if (sscanf(line, "|%d,%d,", &mx, &my) != 2) continue;
      const char *b = strchr(strchr(line, ',') + 1, ',') + 1;
      for (int k = 0; k < 57 && b[k] && b[k] != '|'; k++)
        if (b[k] != '.') m.buttons |= 1ull << k;
      m.mouseX = (int16_t)mx;
      m.mouseY = (int16_t)my;
    }
    else
    {
      char *at = strchr(line, '@');
      if (at) { sscanf(at + 1, "%d,%d", &x, &y); *at = 0; m.mouseX = (int16_t)x; m.mouseY = (int16_t)y; }
      for (char *c = line; *c; c++)
      {
        const char *p = strchr(k_mnemonics, *c);
        if (p && *c) m.buttons |= 1ull << (p - k_mnemonics);
      }
    }
    if (count == cap) in = realloc(in, (cap *= 2) * sizeof *in);
    in[count++] = m;
  }
  fclose(f);
  *n = count;
  return in;
}

// FNV-1a over a block
static uint64_t hash_bytes(const uint8_t *p, size_t n, uint64_t h)
{
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
  return h;
}
#endif
