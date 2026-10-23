// The machine's state laid out: each visited variable's offset in o4db_save's state, its file's visitor and its
// index there (the variables in the order generated/<file>.c's o4db_state_<file> visits them):
//   o4dbStateMap [OFFSET]   (with OFFSET: only the variable holding that byte)
#include <stdio.h>
#include <stdlib.h>

#include "machine.h"
#include "state.h"

static size_t offset, target = (size_t)-1;
static int index_;
static const char *file;
static void visit(void *data, size_t size, void *c)
{
  (void)data, (void)c;
  if (target == (size_t)-1 || (target >= offset && target < offset + size))
    printf("%8zu-%8zu  %-8s #%d (%zu bytes)%s\n", offset, offset + size - 1, file, index_, size, target == (size_t)-1 ? "" : " <-");
  offset += size;
  index_++;
}

int main(int argc, char **argv)
{
  if (argc > 1) target = strtoull(argv[1], NULL, 10);
  size_t total = 0;
  static const struct { const char *name; void (*f)(O4dbVisit, void *); } files[] = {
    { "asm2c", o4db_state_asm2c }, { "game", o4db_state_game }, { "dos", o4db_state_dos }, { "dosmem", o4db_state_dosmem }, { "pit", o4db_state_pit },
    { "vga", o4db_state_vga }, { "opl", o4db_state_opl }, { "sb", o4db_state_sb }, { "tandy", o4db_state_tandy }, { "mpu401", o4db_state_mpu401 },
    { "cheats", o4db_state_cheats }, { "drv_pc15", o4db_state_drv_pc15 }, { "drv_ad15", o4db_state_drv_ad15 }, { "drv_sb15", o4db_state_drv_sb15 },
    { "drv_td15", o4db_state_drv_td15 }, { "drv_mt15", o4db_state_drv_mt15 },
  };
  // (the machine's own struct first: the state's size less the files' variables)
  for (size_t k = 0; k < sizeof files / sizeof *files; k++)
  {
    offset = 0;
    file = files[k].name;
    index_ = 0;
    size_t saved = target;
    target = (size_t)-2;
    files[k].f(visit, NULL);
    target = saved;
    total += offset;
  }
  offset = o4db_state_size() - total;
  printf("the machine's struct: 0-%zu\n", offset - 1);
  for (size_t k = 0; k < sizeof files / sizeof *files; k++)
  {
    file = files[k].name;
    index_ = 0;
    files[k].f(visit, NULL);
  }
  return 0;
}
