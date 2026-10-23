#!/usr/bin/env python3
"""quickerOpen4DBoxing's sources, generated from Open4DBoxing's (extern/Open4DBoxing/source).

  generate.py [OPEN4DBOXING_SOURCE_DIR] [OUT_DIR]

Open4DBoxing is the game recompiled whole: one blocking call (game_run) whose machine state is in globals and whose
call chain is the C stack. quickerOpen4DBoxing is the same code made into a machine a search can step, save and load,
any number of times on any number of threads:

- every global the game's run changes is thread-local (O4DB_TLS, tls.h): a thread holds one running machine, and
  machine.c moves a machine's state in and out of it;
- the recompiled program (box_core.c, the MCGA build) can be stopped where its call chain holds nothing but its own
  functions, and resumed there from the saved state alone: each call site pushes its number on a shadow call stack
  (asm_shadow, part of the state) before the call and pops it after; each backward jump may stop the run there
  (ASM_YIELD_CHECK: a frame has passed and no interrupt handler, driver or BIOS call is in progress) by recording
  its own number and leaving through longjmp; each function's entry, when the machine is resuming, jumps to the
  call site or the backward jump the shadow stack names. Indirect calls keep their target in the shadow stack.
- each source file gets a visitor of its machine state (o4db_state_FILE): the variables a saved machine needs, not
  the configuration a machine is set up with (its host, hooks, the disks' files) nor the sound's rendering queues.

The EGA, CGA and Tandy builds are left out (game.c's table names the MCGA build only).
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "extern", "Open4DBoxing", "source")
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "source", "generated")

RUNTIME_C = ["asm2c.c", "game.c", "dos.c", "dosmem.c", "pit.c", "vga.c", "opl.c", "sb.c", "tandy.c", "mpu401.c",
             "disks.c", "cheats.c", "exe.c", "dosboot.c", "replay.c", "version.c",
             "drv_pc15.c", "drv_ad15.c", "drv_sb15.c", "drv_td15.c", "drv_mt15.c", "box_core.c"]
HEADERS = ["types.h", "asm2c.h", "vga.h", "game.h", "dos.h", "dosmem.h", "pit.h", "opl.h", "sb.h", "tandy.h",
           "mpu401.h", "disks.h", "cheats.h", "exe.h", "dosboot.h", "dosboot_machines.h", "replay.h", "version.h",
           "drivers.h", "box_core.h"]

# ---------------------------------------------------------------------------------------------- thread-local globals

# The definitions made thread-local, by the start of their line in each file (every mutable file-scope variable and
# function-local static of the runtime; a definition found nowhere is an error: Open4DBoxing changed)
TLS_DEFS = {
    "asm2c.c": ["u8 g_mem[MEM_SIZE];", "AsmMouse asm_mouse;", "AsmJoystick asm_joystick;", "void (*asm_sti_hook)(void);",
                "void (*asm_block_hook)(u16 off);", "void (*asm_interrupt_hook)(u8 n);", "u16 asm_ip;", "u16 g_dsSeg;",
                "u16 g_sp = 0xFFFE;", "Regs R;", "bool g_asmCall;", "void (*asm_fn_hook)(u32 addr);",
                "int asm_backBudget = 1;", "u64 asm_icount;", "u32 asm_watchAddr = 0xFFFFFFFF;",
                "void (*asm_watch_hook)(u32 addr, u16 value);",
                "static u16 programFirstSeg = 0x0823, programEndSeg = 0x3A00;", "u8 asm_dac[256][3];",
                "static u8 dacIndex, dacPart;", "u32 asm_tandyBase;", "u16 asm_tandyMask = 0x7FFF;",
                "u16 (*asm_port_hook)(u16 port);", "bool (*asm_port_out_hook)(u16 port, u8 value);",
                "u32 asm_ioCostIn, asm_ioCostOut, asm_ioCostIn16, asm_ioCostOut16, asm_vramCost;",
                "u32 (*asm_vram_cost_hook)(void);", "u64 asm_waits;", "void (*asm_idle_hook)(void);",
                "bool (*asm_int_hook)(u8 n);", "static u8 videoMode = 3;", "CgaState cga;", "TandyState tandyVideo;",
                "int asm_machine;", "u16 asm_psp_seg = 0x0813;", "u16 asm_dos_sp;",
                "bool (*asm_dispatch_hook)(u16 seg, u16 off);", "void (*asm_outside_hook)(u16 seg, u16 off);",
                "void (*asm_fatal_hook)(void);", "    static int dbg = -1;"],
    "game.c": ["static const GameHost *host;", "static jmp_buf exitJump;", "static int exitCode;",
               "static uint64_t now, lastIrq, frames, nextFrame;", "static uint8_t keyQueue[64];",
               "static int keyHead, keyTail;", "static uint8_t port60;", "static uint8_t picMask = 0xB8;",
               "static uint8_t picIrr, picIsr;", "static int sbIrq = 7;", "static int programIndex;",
               "static DosHost dosHost = ",
               "static char startError[1200];", "static uint64_t joyStart;", "  static u8 shown[320 * 200];"],
    "dos.c": ["static const DosHost *host;", "static SftEntry sft[SFT_MAX];",
              "void (*dos_read_hook)(const char *name, u16 seg, u16 off, u16 len);", "static int debugFiles = -1;"],
    "dosmem.c": ["u16 dos_owner = 0x0813;", "static Mcb arena[256];", "static int nArena;", "static char programName[8];"],
    "pit.c": ["static Channel ch[3];", "static uint8_t port61, picMask;", "static Speaker ev[EVENTS];",
              "static unsigned evHead, evTail;", "static Speaker cur;", "static Speaker at;", "static uint64_t lastT;",
              "static double samplePos;", "static double lastIn, lastOut;"],
    "vga.c": ["VgaState vga;"],
    "opl.c": ["static Op op[18];", "static Voice voice[9];", "static uint8_t reg01, reg08, regBD, address;",
              "static uint32_t egCnt, noise;", "static int tremPos, vibPos, sampleCount;",
              "static uint8_t timerVal[2], timerCtl, status;", "static uint64_t timerStart[2];",
              "static struct { uint64_t t; uint8_t r, v; } ev[EVENTS];", "static unsigned evHead, evTail;", "static uint64_t lastT;",
              "static int prevOut, curOut;", "static double samplePos;"],
    "sb.c": ["static bool resetting, speaker;", "static uint8_t outQueue[8];", "static int outCount, outHead;",
             "static uint8_t cmd, args[4];", "static int argsWanted, argsGot;", "static int timeConstant = 0xA6;",
             "static uint8_t dacLevel = 0x80;", "static uint64_t dacTime;",
             "static uint16_t dmaAddr[4], dmaCount[4], dmaBaseAddr[4], dmaBaseCount[4];",
             "static uint8_t dmaPage[4], dmaMode[4], dmaMask = 0x0F;", "static bool flipFlop;",
             "static struct { bool active, autoInit; uint64_t start; uint32_t rate, addr, count, block; } play;",
             "static bool irqPending;", "static uint64_t irqAt;",
             "static struct { uint64_t t; uint8_t v; } dacEvents[4096];", "static int dacHead, dacTail;",
             "static uint64_t renderedTo;", "static uint8_t lastLevel = 0x80;", "    static uint32_t blocksSignalled;"],
    "tandy.c": ["static Chan ch[4];", "static uint16_t lfsr;", "static struct { uint64_t t; uint8_t v; } ev[EVENTS];", "static unsigned evHead, evTail;",
                "static uint64_t lastT;", "static double samplePos;", "static double chipPos;",
                "static double lastIn, lastOut;"],
    "mpu401.c": ["static bool uart;", "static uint8_t queue[16], last;", "static int head, count;"],
    "cheats.c": ["GameCheats game_cheats;", "static int undoCount;", "static int debug = -1;"],
    "drv_pc15.c": ["u16 pc15_seg;"], "drv_ad15.c": ["u16 ad15_seg;"], "drv_sb15.c": ["u16 sb15_seg;"],
    "drv_td15.c": ["u16 td15_seg;"], "drv_mt15.c": ["u16 mt15_seg;"],
}

# The declarations made thread-local in the headers (every extern variable but the replay variant's, which this build
# never runs: its globals stay process-wide and untouched)
TLS_EXTERN_SKIP = {"replay_on", "asm_pos", "asm_replay_next", "asm_cur_off", "asm_cov", "replay_progLo",
                   "replay_progHi", "replay_input_regs", "replay_quiet", "game_drivers"}

TLS_MACRO = "O4DB_TLS"


def tls_line(line):
    """The definition or declaration with the thread-local qualifier after its storage class (or first)."""
    m = re.match(r"^(\s*)(static |extern )?(.*)$", line)
    ind, sc, rest = m.group(1), m.group(2) or "", m.group(3)
    return f"{ind}{sc}{TLS_MACRO} {rest}"


def apply_tls(name, text):
    defs = TLS_DEFS.get(name, [])
    lines = text.split("\n")
    for d in defs:
        hits = [i for i, l in enumerate(lines) if l.startswith(d)]
        if len(hits) != 1:
            sys.exit(f"generate: {name}: the definition '{d}' found {len(hits)} times (Open4DBoxing changed?)")
        lines[hits[0]] = tls_line(lines[hits[0]])
    if name.endswith(".h"):
        for i, l in enumerate(lines):
            m = re.match(r"^extern (?!const )(?!\"C\")(.*?);", l)
            if not m:
                continue
            decl = m.group(1)
            # a function (not a pointer to one) stays as it is: "T name(args)" has its first '(' after the name
            fp = re.match(r"^[\w\s\*]+\(\s*\*\s*(\w+)\s*\)", decl)
            if fp:
                names = [fp.group(1)]
            elif "(" in decl:
                continue
            else:
                names = re.findall(r"(\w+)\s*(?:\[[^\]]*\])*\s*(?:,|$)", decl)
            if any(n in TLS_EXTERN_SKIP for n in names):
                continue
            lines[i] = tls_line(l)
    return "\n".join(lines)


# ------------------------------------------------------------------------------------------------- patches by file

def must_sub(name, text, old, new, count=1):
    n = text.count(old)
    if n != count:
        sys.exit(f"generate: {name}: '{old[:60]}...' found {n} times, expected {count} (Open4DBoxing changed?)")
    return text.replace(old, new)


def patch(name, text):
    if name in ("pit.c", "opl.c", "tandy.c"):
        # the sound's queues of chip writes waiting to be rendered: never rendered here (a search plays no sound), so a
        # small ring that fills and drops what comes after (what the game does is not changed by them)
        text = re.sub(r"#define EVENTS \d+", "#define EVENTS 16", text)
    if name == "drivers.h":
        # a driver's segment is thread-local, so its address is not a constant for the table: the table names it
        # through a function
        text = must_sub(name, text, "#define DRIVER(name) extern u16 name##_seg; void name##_slot(int slot); bool name##_run(u16 off);",
                        "#define DRIVER(name) extern O4DB_TLS u16 name##_seg; u16 *name##_segp(void); void name##_slot(int slot); bool name##_run(u16 off);")
        text = must_sub(name, text, "typedef struct { const char *file; u16 *seg; bool (*run)(u16 off); } GameDriver;",
                        "typedef struct { const char *file; u16 *(*segp)(void); bool (*run)(u16 off); } GameDriver;")
    if name.startswith("drv_") and name.endswith(".c"):
        drv = name[4:-2]
        text = must_sub(name, text, f"O4DB_TLS u16 {drv}_seg;\n", f"O4DB_TLS u16 {drv}_seg;\nu16 *{drv}_segp(void) {{ return &{drv}_seg; }}\n")
    if name == "game.c":
        text = must_sub(name, text, '{ "PC15.DRV", &pc15_seg, pc15_run }, { "AD15.DRV", &ad15_seg, ad15_run }, { "SB15.DRV", &sb15_seg, sb15_run },',
                        '{ "PC15.DRV", pc15_segp, pc15_run }, { "AD15.DRV", ad15_segp, ad15_run }, { "SB15.DRV", sb15_segp, sb15_run },')
        text = must_sub(name, text, '{ "TD15.DRV", &td15_seg, td15_run }, { "MT15.DRV", &mt15_seg, mt15_run },',
                        '{ "TD15.DRV", td15_segp, td15_run }, { "MT15.DRV", mt15_segp, mt15_run },')
        text = text.replace("*game_drivers[k].seg", "*game_drivers[k].segp()")
        # the MCGA build only (the other cores are not part of this one)
        text = must_sub(name, text, '#include "cga_core.h"\n#include "ega_core.h"\n#include "tdy_core.h"\n', "")
        text = must_sub(name, text, '  { "4DBEGA.EXE", 0x2E7B + GAME_LOAD_SEG, ega_crt0, ega_dispatch },\n'
                                    '  { "4DBCGA.EXE", 0x2AE5 + GAME_LOAD_SEG, cga_crt0, cga_dispatch },\n'
                                    '  { "4DBTDY.EXE", 0x2A8C + GAME_LOAD_SEG, tdy_crt0, tdy_dispatch },\n',
                        '  { "4DBEGA.EXE", 0, NULL, NULL },\n  { "4DBCGA.EXE", 0, NULL, NULL },\n  { "4DBTDY.EXE", 0, NULL, NULL },\n')
        # the floppies' files: read once for the process (every machine's are the same, and a saved machine's open files
        # point into them), never freed
        text = must_sub(name, text, """  disks_free(diskFiles, diskCount);
  diskFiles = NULL, diskCount = 0;
  if (disks_present(host->gameDir) && (diskCount = disks_load(host->gameDir, &diskFiles, startError, sizeof startError)) < 0)
  {
    diskCount = 0;
    fprintf(stderr, "game: %s\\n", startError);
    return -1;
  }
""", """  pthread_mutex_lock(&disksLock);
  if (!diskFiles && disks_present(host->gameDir) && (diskCount = disks_load(host->gameDir, &diskFiles, startError, sizeof startError)) < 0)
  {
    diskCount = 0;
    diskFiles = NULL;
    pthread_mutex_unlock(&disksLock);
    fprintf(stderr, "game: %s\\n", startError);
    return -1;
  }
  pthread_mutex_unlock(&disksLock);
""")
        text = must_sub(name, text, "static DosFile *diskFiles;", "static DosFile *diskFiles;\nstatic pthread_mutex_t disksLock = PTHREAD_MUTEX_INITIALIZER;")
        # (the BIOS's tick, its data written straight: their page marked for the machine's deltas)
        text = must_sub(name, text, "static void bios_int8(void)\n{\n", "static void bios_int8(void)\n{\n  asm_dirty[4] = 1;\n")
        # (machine.c's fast fight: the time of the next thing that happens, the timer's interrupt or a frame's end, in
        # PIT ticks, and the machine's speed, for time to go to it)
        text = must_sub(name, text, "// time passes: the timer's interrupts, the keys, the frames\n", """uint64_t game_next_event(void)
{
  const uint64_t t = pit_next_irq(lastIrq);
  return t < nextFrame ? t : nextFrame;
}
uint32_t game_cycles(void) { return host->cycles; }

// time passes: the timer's interrupts, the keys, the frames
""")
        text = must_sub(name, text, "#include <setjmp.h>\n", "#include <pthread.h>\n#include <setjmp.h>\n")
        # game_run in two: the setup (the thread's machine made ready: its hooks, DOS, the program loaded) and the start
        text = must_sub(name, text, "int game_run(const GameHost *h)\n{\n  host = h;", "int game_setup(const GameHost *h)\n{\n  host = h;")
        text = must_sub(name, text, """  asm_backBudget = 1;
  exitCode = 0;
  if (!setjmp(exitJump)) program->crt0();
  dos_close_all();
  return exitCode;
}""", """  asm_backBudget = 1;
  exitCode = 0;
  return 0;
}

int game_run(const GameHost *h)
{
  if (game_setup(h) < 0) return -1;
  const GameProgram *program = &programs[programIndex];
  if (!setjmp(exitJump)) program->crt0();
  dos_close_all();
  return exitCode;
}""")
        # resuming a machine stopped in its program: the program's entry again, its functions going to where they were
        # (the setup game_run did before it is in the machine's state)
        text += ("\n// quickerOpen4DBoxing: the program entered again to resume where it stopped (asm_resuming set: each\n"
                 "// function on the way goes to the call site the shadow stack names); its exit as game_run's\n"
                 "int game_resume(void)\n{\n  const GameProgram *program = &programs[programIndex];\n"
                 "  exitCode = 0;\n  if (!setjmp(exitJump)) program->crt0();\n  dos_close_all();\n  return exitCode;\n}\n"
                 "// the frame count (the frames shown so far) and the clock, as the machine's own\n"
                 "uint64_t game_frames(void) { return frames; }\n")
    if name == "game.h":
        text = must_sub(name, text, "int game_run(const GameHost *host);",
                        "int game_run(const GameHost *host);\n// quickerOpen4DBoxing: the setup alone (the machine made ready on the thread, the program loaded, not\n"
                        "// started); the program resumed where it stopped (game.c)\nint game_setup(const GameHost *host);\n"
                        "int game_resume(void);\nuint64_t game_frames(void);")
    if name == "dos.c":
        # a machine whose game has a file of the host's open cannot be moved to another thread (its FILE is this one's)
        text += ("\n// quickerOpen4DBoxing: the game's files open on the host (a state with some cannot move)\n"
                 "int dos_open_host_files(void)\n{\n  int n = 0;\n"
                 "  for (int k = 0; k < SFT_MAX; k++) if (sft[k].refs > 0 && sft[k].kind == SFT_FILE && sft[k].f) n++;\n"
                 "  return n;\n}\n")
    if name == "dos.h":
        text = must_sub(name, text, "#endif", "int dos_open_host_files(void);  // quickerOpen4DBoxing (dos.c)\n#endif", count=text.count("#endif"))
    if name == "asm2c.c":
        # (the services' writes for the machine's deltas: the BIOS's data for the keyboard, the clock, the mouse and
        # the system services; DOS, the video and the rest may write anywhere)
        text = must_sub(name, text, "void asm_bios_service(u8 n)\n{\n",
                        "void asm_bios_service(u8 n)\n{\n"
                        "  if (n == 0x09 || n == 0x16 || n == 0x1A || n == 0x33 || n == 0x15 || n == 0x74) asm_dirty[4] = 1;\n"
                        "  else asm_dirty_all = 1;\n")
        # the program's interrupt handlers, the drivers and the BIOS's code run in calls the machine cannot stop in:
        # asm_unclean counts them (the stops wait for the next backward jump of the program's own flow)
        text = must_sub(name, text, "  R.if_ = false;  // (the handler runs with interrupts off until it allows them or returns)\n  asm_far_call(seg, off);\n",
                        "  R.if_ = false;  // (the handler runs with interrupts off until it allows them or returns)\n  asm_unclean++;\n  asm_far_call(seg, off);\n  asm_unclean--;\n")
        text = must_sub(name, text, "void asm_outside(u16 seg, u16 off)\n{\n",
                        "static void asm_outside_body(u16 seg, u16 off);\nvoid asm_outside(u16 seg, u16 off)\n{\n  asm_unclean++;\n  asm_outside_body(seg, off);\n  asm_unclean--;\n}\nstatic void asm_outside_body(u16 seg, u16 off)\n{\n")
        text += RESUME_RUNTIME
    if name == "asm2c.h":
        text = must_sub(name, text, "#define ASM_BACK(t) (asm_ip = (t), asm_back())", "#define ASM_BACK(t) (asm_ip = (t), asm_back())\n" + RESUME_HEADER)
        # the MCGA build's memory: none of the other cards' paths (the CGA's wait states, the EGA's planes, the Tandy's
        # banks) nor the debugging's watched address apply (machine.c checks the cards' state stays so): the
        # program's accesses are the megabyte's, inlined
        start = text.index("static inline u8 asm_rd8(u16 seg, u16 a)")
        end = text.index("#define W8(s, a, v)")
        text = text[:start] + MCGA_ACCESS + text[end:]
        # the hooks of the blocks and of the functions' entries: none in a machine (its step function is marked by
        # the generated code itself: ASM_STEP_MARK), so not even their tests
        text = must_sub(name, text, "#define ASM_BLOCK(n, off) do { asm_icount += (n); if (asm_block_hook) asm_block_hook(off); } while (0)",
                        "#define ASM_BLOCK(n, off) (asm_icount += (n))")
        text = must_sub(name, text, "#define FN(x) do { if (asm_fn_hook) asm_fn_hook(x); } while (0)", "#define FN(x) ((void)0)")
        # (the trace variant: each instruction's address, CS:IP, for asm_trace_read)
        text = must_sub(name, text, "#else\n#define ASM_I(o) ((void)0)\n", "#elif defined(O4DB_TRACE)\n#define ASM_I(o) (asm_trace_ip = ((u32)R.cs << 16) | (u16)(o))\n#define ASM_REP_CHECK() ((void)0)\n#else\n#define ASM_I(o) ((void)0)\n")
    if name == "types.h":
        text = must_sub(name, text, "#include <stdint.h>\n", "#include <stdint.h>\n#include \"tls.h\"\n")
        # (the pages written since the machine's last delta: machine.c; the runtime's word writes mark theirs)
        text = must_sub(name, text, "static inline void mem_w16(u32 a, u16 v) { g_mem[a] = (u8)v; g_mem[a + 1] = (u8)(v >> 8); }",
                        "extern O4DB_TLS u8 asm_dirty[];\nextern O4DB_TLS int asm_dirty_all;\n"
                        "static inline void mem_w16(u32 a, u16 v) { g_mem[a] = (u8)v; g_mem[a + 1] = (u8)(v >> 8); asm_dirty[a >> 8] = 1; asm_dirty[(a + 1) >> 8] = 1; }")
    return text


RESUME_HEADER = r"""
// quickerOpen4DBoxing: the machine stopped and resumed in the program's own call chain (tools/generate.py).
// asm_shadow: the call sites of the calls in progress, from the program's entry (one per level, the indirect calls
// with their targets), and at the stop the backward jump it stopped at; asm_sdepth the calls in progress;
// asm_resuming while the program's entry is going back down that chain; asm_unclean the calls of handlers, drivers
// and the BIOS in progress (no stop inside them); asm_yield_req a frame has passed (a stop is due).
typedef struct { u32 site; u16 seg, off; } AsmShadow;
#define ASM_SHADOW_MAX 96
extern O4DB_TLS AsmShadow asm_shadow[ASM_SHADOW_MAX];
extern O4DB_TLS int asm_sdepth, asm_chain, asm_resuming, asm_unclean, asm_yield_req;
void asm_yield(u32 site);       // the stop: the site recorded, out to the machine (machine.c's longjmp)
void asm_resume_fail(u32 site);  // a saved chain this program does not have
static inline void ASM_CS(u32 site)
{
  if (asm_sdepth >= ASM_SHADOW_MAX) asm_resume_fail(site);
  asm_shadow[asm_sdepth++].site = site;
}
static inline void ASM_ICS(u32 site, u16 seg, u16 off)
{
  if (asm_sdepth >= ASM_SHADOW_MAX) asm_resume_fail(site);
  asm_shadow[asm_sdepth].seg = seg;
  asm_shadow[asm_sdepth].off = off;
  asm_shadow[asm_sdepth++].site = site;
}
#define ASM_CE() (asm_sdepth--)
#define ASM_TS() (asm_shadow[asm_sdepth - 1].seg)
#define ASM_TO() (asm_shadow[asm_sdepth - 1].off)
#define ASM_YIELD_CHECK(site) do { if (asm_yield_req && !asm_unclean) asm_yield(site); } while (0)
#define ASM_RSITE() (asm_shadow[asm_sdepth].site)
#define ASM_RCALL() (asm_sdepth++)
#define ASM_RDONE() (asm_resuming = 0)
// the machine's step function marked where the generated code enters it (25bb:0202, the boxers' controls read)
void asm_step_mark(void);  // (machine.c)
#define ASM_STEP_MARK() asm_step_mark()
// (tests: machine.c's o4db_test_hooks)
extern void (*asm_test_hook)(u32 fn);
extern unsigned asm_test_skip;
// the fight's logic alone (machine.c's o4db_set_fast): the frames' driver's entry, the scene's drawing skipped, the
// miss path's read of its uninitialized local
#define ASM_FAST_SITE 999999
extern O4DB_TLS int asm_fast, asm_skip_draw;
void asm_fast_frame(void);
void asm_fast_tick(void);
void asm_fast_miss(void);
void asm_fast_wait(void);
"""

MCGA_ACCESS = r"""// (quickerOpen4DBoxing: the MCGA build's memory, every access the megabyte's; the tests' trace variant, O4DB_TRACE,
// gives each read of the program and the instruction's address to asm_trace_read)
#ifdef O4DB_TRACE
extern u32 asm_trace_ip;
extern void (*asm_trace_read)(u32 addr, u16 value, int size);
extern void (*asm_trace_pre_read)(u32 addr, int size);  // (before the read: the hook may change the memory read)
static inline u8 asm_rd8(u16 seg, u16 a)
{
  if (asm_trace_pre_read) asm_trace_pre_read(((u32)seg << 4) + a, 1);
  u8 v = g_mem[((u32)seg << 4) + a];
  if (asm_trace_read) asm_trace_read(((u32)seg << 4) + a, v, 1);
  return v;
}
static inline u16 asm_rd16(u16 seg, u16 a)
{
  if (asm_trace_pre_read) asm_trace_pre_read(((u32)seg << 4) + a, 2);
  u16 v = *(u16a *)far_ptr(seg, a);
  if (asm_trace_read) asm_trace_read(((u32)seg << 4) + a, v, 2);
  return v;
}
#else
static inline u8 asm_rd8(u16 seg, u16 a) { return g_mem[((u32)seg << 4) + a]; }
static inline u16 asm_rd16(u16 seg, u16 a) { return *(u16a *)far_ptr(seg, a); }
#endif
#define M8(s, a) asm_rd8(SEGV_##s, (u16)(a))
#define M16(s, a) asm_rd16(SEGV_##s, (u16)(a))
extern O4DB_TLS u32 asm_watchAddr;
void asm_watch_hit(u32 addr, u16 value);
extern O4DB_TLS void (*asm_watch_hook)(u32 addr, u16 value);
// (every write marks its 256-byte page for the machine's deltas, asm_dirty: machine.c)
#ifdef O4DB_TRACE
extern void (*asm_trace_write)(u32 addr, u16 value, int size);
static inline void asm_wr8(u16 seg, u16 off, u8 v)
{
  const u32 a = ((u32)seg << 4) + off;
  g_mem[a] = v;
  asm_dirty[a >> 8] = 1;
  if (asm_trace_write) asm_trace_write(a, v, 1);
}
static inline void asm_wr16(u16 seg, u16 off, u16 v)
{
  const u32 a = ((u32)seg << 4) + off;
  *(u16a *)(g_mem + a) = v;
  asm_dirty[a >> 8] = 1;
  asm_dirty[(a + 1) >> 8] = 1;
  if (asm_trace_write) asm_trace_write(a, v, 2);
}
#else
static inline void asm_wr8(u16 seg, u16 off, u8 v)
{
  const u32 a = ((u32)seg << 4) + off;
  g_mem[a] = v;
  asm_dirty[a >> 8] = 1;
}
static inline void asm_wr16(u16 seg, u16 off, u16 v)
{
  const u32 a = ((u32)seg << 4) + off;
  *(u16a *)(g_mem + a) = v;
  asm_dirty[a >> 8] = 1;
  asm_dirty[(a + 1) >> 8] = 1;
}
#endif
"""

RESUME_RUNTIME = r"""
// quickerOpen4DBoxing: the shadow call stack (asm2c.h)
O4DB_TLS AsmShadow asm_shadow[ASM_SHADOW_MAX];
O4DB_TLS int asm_sdepth, asm_chain, asm_resuming, asm_unclean, asm_yield_req;
void asm_resume_fail(u32 site)
{
  fprintf(stderr, "asm2c: the machine's call chain is not this program's (site %u, depth %d, resuming %d)\n", site, asm_sdepth, asm_resuming);
  asm_fatal();
}
"""

# ------------------------------------------------------------------------------------------------- the resume transform

FUNC_RE = re.compile(r"^static void (a_[0-9a-f]{4}_[0-9a-f]{4})\(void\)\n\{\n(.*?)\n\}\n", re.S | re.M)
DIRECT_RE = re.compile(r"(?<![\w.>])(a_[0-9a-f]{4}_[0-9a-f]{4})\(\);")
INDIRECT_RE = re.compile(r"asm_(far|near)_(call|jump)\(([^,()]+(?:\([^()]*\))?), ([^,()]+(?:\([^()]*\))?)\);")
BACK_RE = re.compile(r"ASM_BACK\((0x[0-9a-f]+)\); goto (L_[0-9a-f]+);")


def resume_transform(text):
    site = [0]
    stats = {"functions": 0, "direct": 0, "indirect": 0, "back": 0}

    def one(m):
        fname, body = m.group(1), m.group(2)
        cases = []

        def direct(mm):
            site[0] += 1
            s = site[0]
            cases.append(f"case {s}: ASM_RCALL(); goto R_{s};")
            stats["direct"] += 1
            return f"ASM_CS({s}); R_{s}: {mm.group(1)}(); ASM_CE();"

        def indirect(mm):
            site[0] += 1
            s = site[0]
            cases.append(f"case {s}: ASM_RCALL(); goto R_{s};")
            stats["indirect"] += 1
            return f"ASM_ICS({s}, {mm.group(3)}, {mm.group(4)}); R_{s}: asm_{mm.group(1)}_{mm.group(2)}(ASM_TS(), ASM_TO()); ASM_CE();"

        def back(mm):
            site[0] += 1
            s = site[0]
            cases.append(f"case {s}: ASM_RDONE(); goto {mm.group(2)};")
            stats["back"] += 1
            return f"ASM_BACK({mm.group(1)}); ASM_YIELD_CHECK({s}); goto {mm.group(2)};"

        new = []
        for line in body.split("\n"):
            # the code, not its comment (the disassembly after //)
            code, sep, comment = line.partition(" // ")
            code = DIRECT_RE.sub(direct, code)
            code = INDIRECT_RE.sub(indirect, code)
            code = BACK_RE.sub(back, code)
            new.append(code + sep + comment)
        stats["functions"] += 1
        entry = ""
        if cases:
            entry = "  if (asm_resuming) switch (ASM_RSITE()) { " + " ".join(cases) + f" default: asm_resume_fail(ASM_RSITE()); }}\n"
        else:
            entry = "  if (asm_resuming) asm_resume_fail(ASM_RSITE());\n"
        return f"static void {fname}(void)\n{{\n{entry}" + "\n".join(new) + "\n}\n"

    text = FUNC_RE.sub(one, text)
    # the step function's entry (machine.c: a step ends where the game reads the boxers' controls)
    text = must_sub("box_core.c", text, "static void a_25bb_0202(void)\n{\n", "static void a_25bb_0202(void)\n{\n  ASM_STEP_MARK();\n")
    # (tests, machine.c's o4db_test_hooks: a hook at each of the fight's events, a boxer's step (1eb6:3354), and at
    # each frame's (1eb6:3624); the fight's scene not drawn (1c47:229c: a far return at once), so that the frames
    # come at other times)
    text = must_sub("box_core.c", text, "  FN(0x1EB63354);\n", "  FN(0x1EB63354);\n  if (asm_test_hook) asm_test_hook(0x1EB63354);\n")
    text = must_sub("box_core.c", text, "  FN(0x1EB63624);\n", "  FN(0x1EB63624);\n  if (asm_test_hook) asm_test_hook(0x1EB63624);\n")
    # (and at each frame's time taken, 25bb:063e, and the game's time paused and resumed, 25bb:018e and 25bb:0164)
    text = must_sub("box_core.c", text, "  FN(0x25BB063E);\n", "  FN(0x25BB063E);\n  if (asm_test_hook) asm_test_hook(0x25BB063E);\n")
    text = must_sub("box_core.c", text, "  FN(0x25BB018E);\n", "  FN(0x25BB018E);\n  if (asm_test_hook) asm_test_hook(0x25BB018E);\n")
    text = must_sub("box_core.c", text, "  FN(0x25BB0164);\n", "  FN(0x25BB0164);\n  if (asm_test_hook) asm_test_hook(0x25BB0164);\n")
    text = must_sub("box_core.c", text, "  FN(0x1C47229C);\n", "  FN(0x1C47229C);\n  if (asm_test_skip & 1) { R.sp += 4; return; }\n")
    # the fight's logic alone (machine.c's o4db_set_fast): at the frames' driver's entry (2550:02ac) the tick's start,
    # at its end time to the timer's next tick when none is due, and the step's stop there if that tick read the
    # controls (a resume site of its own); the scene not drawn (1c47:229c); the miss path's read of its uninitialized
    # local checked (1eb6:19fa at 1e84, [bp - 0x12])
    text = must_sub("box_core.c", text, "static void a_2550_02ac(void)\n{\n  if (asm_resuming) switch (ASM_RSITE()) { ",
                    "static void a_2550_02ac(void)\n{\n  if (asm_resuming) switch (ASM_RSITE()) { case ASM_FAST_SITE: ASM_RDONE(); goto L_fast_exit; ")
    text = must_sub("box_core.c", text, "  FN(0x255002AC);\n", "  FN(0x255002AC);\n  if (asm_test_hook) asm_test_hook(0x255002AC);\n  if (asm_fast) asm_fast_tick();\n")
    text = must_sub("box_core.c", text, "L_0527:   ASM_I(0x0527); ASM_BLOCK(5, 0x0527); R.si = POP();",
                    "L_0527:   if (asm_fast) { asm_fast_frame(); ASM_YIELD_CHECK(ASM_FAST_SITE); }\nL_fast_exit:   ASM_I(0x0527); ASM_BLOCK(5, 0x0527); R.si = POP();")
    text = must_sub("box_core.c", text, "  if (asm_test_skip & 1) { R.sp += 4; return; }\n",
                    "  if (asm_test_skip & 1) { R.sp += 4; return; }\n  if (asm_skip_draw) { R.sp += 4; return; }\n")
    text = must_sub("box_core.c", text, "ASM_I(0x1e84); ASM_BLOCK(4, 0x1e84); PUSH(M16(SS, (u16)(R.bp + 0xffee)));",
                    "ASM_I(0x1e84); ASM_BLOCK(4, 0x1e84); if (asm_test_hook) asm_test_hook(0x1EB61E84); if (asm_fast) asm_fast_miss(); "
                    "PUSH(M16(SS, (u16)(R.bp + 0xffee)));")
    # (and the busy waits on the timer's count, 2afc:385d's and 2afc:388b's loops: time to the timer's next interrupt)
    for fn, back in (("a_2afc_385d", "0x385d"), ("a_2afc_388b", "0x38a2")):
        start = text.index(f"static void {fn}(void)\n{{\n")
        end = text.index("\n}\n", start)
        body = text[start:end]
        if body.count(f"ASM_BACK({back}); ") != 2:
            raise SystemExit(f"generate: {fn}'s loop is not the expected one")
        text = text[:start] + body.replace(f"ASM_BACK({back}); ", f"ASM_BACK({back}); if (asm_fast) asm_fast_wait(); ") + text[end:]
    return text, stats


# --------------------------------------------------------------------------------------------------- state visitors

# Each file's machine state: the variables a saved machine carries (expressions with their sizes); the rest of its
# thread-local variables are the machine's configuration (set up alike in every machine) or the sound's rendering
STATE = {
    "asm2c.c": ["g_mem", "&asm_mouse", "&asm_joystick", "&asm_ip", "&g_dsSeg", "&g_sp", "&R", "&g_asmCall",
                "&asm_backBudget", "&asm_icount", "&asm_waits", "asm_dac", "&dacIndex", "&dacPart", "&asm_tandyBase",
                "&asm_tandyMask", "&videoMode", "&cga", "&tandyVideo", "&asm_dos_sp", "asm_shadow", "&asm_chain"],
    "game.c": ["&exitCode", "&now", "&lastIrq", "&frames", "&nextFrame", "keyQueue", "&keyHead", "&keyTail", "&port60",
               "&picMask", "&picIrr", "&picIsr", "&sbIrq", "&joyStart"],
    "dos.c": ["sft"],
    "dosmem.c": ["&dos_owner", "arena", "&nArena", "programName"],
    "pit.c": ["ch", "&port61", "&picMask", "&cur", "&lastT"],
    "vga.c": [("&vga", "offsetof(VgaState, plane)"), ("&vga.latch", "sizeof(VgaState) - offsetof(VgaState, latch)")],
    "opl.c": ["op", "voice", "&reg01", "&reg08", "&regBD", "&address", "&egCnt", "&noise", "&tremPos", "&vibPos",
              "&sampleCount", "timerVal", "&timerCtl", "&status", "timerStart", "&lastT"],
    "sb.c": ["&resetting", "&speaker", "outQueue", "&outCount", "&outHead", "&cmd", "args", "&argsWanted", "&argsGot",
             "&timeConstant", "&dacLevel", "&dacTime", "dmaAddr", "dmaCount", "dmaBaseAddr", "dmaBaseCount", "dmaPage",
             "dmaMode", "&dmaMask", "&flipFlop", "&play", "&irqPending", "&irqAt"],
    "tandy.c": ["ch", "&lfsr", "&lastT"],
    "mpu401.c": ["&uart", "queue", "&last", "&head", "&count"],
    "cheats.c": ["&game_cheats", "&undoCount"],
    "drv_pc15.c": ["&pc15_seg"], "drv_ad15.c": ["&ad15_seg"], "drv_sb15.c": ["&sb15_seg"], "drv_td15.c": ["&td15_seg"],
    "drv_mt15.c": ["&mt15_seg"],
}


def state_visitor(name, text):
    items = STATE.get(name)
    if items is None:
        return text
    fn = "o4db_state_" + re.sub(r"\W", "_", name[:-2])
    body = []
    for it in items:
        if isinstance(it, tuple):
            expr, size = it
        else:
            expr = it
            size = f"sizeof({it[1:] if it.startswith('&') else it})"
        body.append(f"  v((void *)({expr}), {size}, c);")
    vis = (f"\n// quickerOpen4DBoxing: this file's part of a machine's state (tools/generate.py)\n#include <stddef.h>\n"
           f"void {fn}(void (*v)(void *data, size_t size, void *c), void *c)\n{{\n" + "\n".join(body) + "\n}\n")
    return text + vis


# ---------------------------------------------------------------------------------------------------------- main

def main():
    os.makedirs(OUT, exist_ok=True)
    visitors = []
    for name in HEADERS + RUNTIME_C:
        with open(os.path.join(SRC, name)) as f:
            text = f.read()
        text = apply_tls(name, text)
        text = patch(name, text)
        if name == "box_core.c":
            text, stats = resume_transform(text)
            print(f"generate: box_core.c: {stats['functions']} functions, {stats['direct']} direct calls, "
                  f"{stats['indirect']} indirect calls and jumps, {stats['back']} backward jumps")
        if name in STATE:
            text = state_visitor(name, text)
            visitors.append("o4db_state_" + re.sub(r"\W", "_", name[:-2]))
        head = f"// GENERATED by quickerOpen4DBoxing's tools/generate.py from Open4DBoxing's source/{name}: do not edit.\n"
        if TLS_MACRO in text:
            head += '#include "tls.h"\n'
        with open(os.path.join(OUT, name), "w") as f:
            f.write(head + text)
    with open(os.path.join(OUT, "tls.h"), "w") as f:
        f.write("// GENERATED by quickerOpen4DBoxing's tools/generate.py: do not edit.\n"
                "// The machine's globals, one set a thread (the model the program's executable resolves at its start)\n"
                "#ifndef O4DB_TLS_H\n#define O4DB_TLS_H\n"
                "#define O4DB_TLS __thread __attribute__((tls_model(\"initial-exec\")))\n#endif\n")
    with open(os.path.join(OUT, "state.h"), "w") as f:
        f.write("// GENERATED by quickerOpen4DBoxing's tools/generate.py: do not edit.\n"
                "// The visitors of the machine's state, file by file (each calls v(data, size, c) for its variables)\n"
                "#ifndef O4DB_STATE_H\n#define O4DB_STATE_H\n#include <stddef.h>\n"
                "typedef void (*O4dbVisit)(void *data, size_t size, void *c);\n")
        for v in visitors:
            f.write(f"void {v}(O4dbVisit v, void *c);\n")
        f.write("static inline void o4db_state_all(O4dbVisit v, void *c)\n{\n")
        for v in visitors:
            f.write(f"  {v}(v, c);\n")
        f.write("}\n#endif\n")
    print(f"generate: {len(HEADERS) + len(RUNTIME_C)} files into {OUT}")


if __name__ == "__main__":
    main()
