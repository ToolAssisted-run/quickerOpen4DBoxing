// quickerOpen4DBoxing's machine: 4D Sports Boxing (Open4DBoxing, its MCGA build) stepped a video frame at a time,
// saved and loaded. One machine runs on a thread at a time (its state is the thread's: tools/generate.py); a machine
// is moved between threads, or several share one, through its saved state (o4db_save, o4db_load).
//
// The inputs are Chimera's core's (chimera-core-open4dboxing's waterbox/boxing-driver.c): 67 buttons (the mouse's two,
// the keyboard's keys the game reads) and the mouse's place in the picture, and they reach the game as there: at each
// frame's end, the keys that changed as their make and break codes (the modifiers that went down first, those that
// came up last), the mouse moved only when its place changed. A run of n inputs gives the k-th to the k-th frame end
// it passes and stops at the program's first backward jump after the n-th (where its call chain is its own: a frame
// end inside an interrupt handler or a driver stops it a little later, still holding the last input at a frame end
// passed meanwhile).
#ifndef QUICKER_OPEN4DBOXING_MACHINE_H
#define QUICKER_OPEN4DBOXING_MACHINE_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define O4DB_BUTTONS 67
#define O4DB_MOUSE_X_NEUTRAL 160
#define O4DB_MOUSE_Y_NEUTRAL 100

// The buttons (bit k: boxing-driver.h's BOX_BTN k: 0 the mouse's left, 1 its right, 2-11 the keypad's 8 2 4 6 7 9 1
// 3 5 0, 12 Enter, 13 Space, 14 Backspace, 15 Esc, 16 Tab, 17 left Shift, 18 right Shift, 19 Ctrl, 20 Alt, 21-46 the
// letters a-z, 47-56 the digits 0-9, 57-66 the function keys F1-F10: the ring's cameras and panels) and the mouse's
// place (0-319, 0-199). 67 buttons: wider than 64 bits
__extension__ typedef unsigned __int128 O4dbButtons;
typedef struct
{
  O4dbButtons buttons;
  int16_t mouseX, mouseY;
} O4dbInput;

// The machine's setup (the same for every machine of a search): the folder with the floppies' images, the sound
// ("mt32", "sb", "adlib", "speaker", "tandy": the driver the game loads, which changes its timing), the PC's clock at
// the start ("YYYY-MM-DD HH:MM:SS"), and where the game's writes go (a folder of the thread's own; NULL: one made in
// /dev/shm). 0 on failure, the reason in o4db_error().
int o4db_setup(const char *gameDir, const char *sound, const char *clockStart, const char *writeDir);
const char *o4db_error(void);
// The machine's speed (before o4db_setup, for every machine of the process): DOSBox's cycles, instructions a
// millisecond (22,000 by default: the oracle's 486). The game's frames all fit in a 10 ms tick of its timer at
// 220,000, where its fight goes the same whether drawn or not (o4db_set_fast)
void o4db_set_speed(uint32_t cycles);

// A new machine, not started: its first run starts it with boot (the inputs before the program starts: Chimera's
// first step's); it plays the whole game (o4db_set_fast off)
void o4db_new(const O4dbInput *boot);

// Runs the inputs (n >= 1); the frame ends passed (n or more: see above). 0 once the program has ended.
uint64_t o4db_run(const O4dbInput *inputs, size_t n);
// The same, with the inputs after the n-th (avail in all) given at the frame ends passed before the stop: a movie's
// rows played exactly however the stops fall
uint64_t o4db_run_ahead(const O4dbInput *inputs, size_t n, size_t avail);
// (tests) a function called at every frame end, before its input is given, with the frame ends passed before it
void o4db_set_frame_hook(void (*hook)(uint64_t frame, void *ctx), void *ctx);

// Steps that end where the game reads its controls: a run of n inputs (o4db_step) ends at the first stop after the
// program enters the function at fn (its Ghidra address, asm2c's FN numbering: 0x25BB0202 the boxers' controls, read
// by the timer's interrupt) once the step has passed a frame end, or after maxFrames frame ends (0: no limit) if the
// game reads none (between rounds, the menus). fn 0: steps are frames.
void o4db_set_step_function(uint32_t fn, unsigned maxFrames);
// One step with this input given at every frame end it passes; the frame ends passed (0 once the program has ended)
uint64_t o4db_step(const O4dbInput *input);
// Entries of the step function so far (a step that passes none of them is the game not reading its controls)
uint64_t o4db_step_function_entries(void);
// (tests) a function called as the game reads the boxers' controls (25bb:0202's entry), with the entries before it
void o4db_test_read_hook(void (*hook)(uint64_t entries, void *ctx), void *ctx);

// The fight's logic alone (the thread's machine; not part of its state): no scene drawn, and time going to the
// timer's next tick when the game's frames have nothing to do; a step that meets the game's bug (a missed punch's
// uninitialized local: the stack's garbage of the game's last whole frame) is run again from its start with whole
// frames before that tick (the step's start: the state last loaded, else the machine's own copy). Its steps are the
// game's at a speed where every frame fits in a tick (o4db_set_speed: 220,000), but for what a timer or key
// interrupt in the middle of the game's drawing leaves on the stack; replays: the steps run again so far
void o4db_set_fast(int on);
uint64_t o4db_fast_replays(void);

// The machine's state: its size, a copy of it, the state taken back (on any thread with the same setup)
size_t o4db_state_size(void);
void o4db_save(void *out);
void o4db_load(const void *in);

// The same as the memory pages that differ from a reference: the machine's memory now (the first machine of the
// process to call it sets it; the others' must be the same: the count of pages that are not is returned), and the
// states as the pages of the memory (256 bytes each, at most maxPages) that differ from it, with the rest of the
// machine. o4db_save_delta answers the pages it took, or -1 when there are more than maxPages (nothing saved).
int o4db_set_reference(void);
// Memory no delta holds, process-wide, set before the first delta: the pages from start's to end's (linear addresses),
// which the game only writes before it reads them (its pictures: the screen, its drawing buffers). A delta loaded
// leaves there what the thread's memory had.
void o4db_set_unsaved(uint32_t start, uint32_t end);
size_t o4db_delta_size(unsigned maxPages);
int o4db_save_delta(void *out, unsigned maxPages);
void o4db_load_delta(const void *in, unsigned maxPages);

// (tests) a function called at the entry of each of the fight's events (a boxer's step, 1eb6:3354: fn 0x1EB63354)
// and of each frame's fight (1eb6:3624: fn 0x1EB63624), and parts of the game skipped (bit 0: the fight's scene not drawn, 1c47:229c), for every machine of the process
void o4db_test_hooks(void (*eventHook)(uint32_t fn), unsigned skip);
// (tests) the machine's speed changed where it is (DOSBox's cycles: instructions a millisecond; the setup's 22,000),
// for every machine of the process: its instruction count rescaled so that its time goes on from here
void o4db_test_set_cycles(uint32_t cycles);
// (tests) the MT-32 driver's MIDI given to the hook as the game writes it, each byte with the PIT's time (1,193,182 Hz:
// a video's sound; the game's run does not depend on it), for the thread's machine; and the PIT's time now
void o4db_test_midi_hook(void (*hook)(uint8_t byte, uint64_t t, void *ctx), void *ctx);
uint64_t o4db_test_now(void);
// (tests, the trace variant only: the library built with O4DB_TRACE) every read of the program's given to the hook
// (the address, the value, 1 or 2 bytes), and the reading instruction's CS:IP (CS the program's at its load, 0823)
void o4db_test_trace_reads(void (*hook)(uint32_t addr, uint16_t value, int size));
void o4db_test_trace_writes(void (*hook)(uint32_t addr, uint16_t value, int size));  // (the program's writes: string ops too)
void o4db_test_trace_pre_reads(void (*hook)(uint32_t addr, int size));  // (before each read: the hook may change the memory)
uint32_t o4db_test_trace_ip(void);
// (tests) the program's call chain now, wherever it is (o4db_chain's elements: its call sites, 8 bytes each)
const uint32_t *o4db_test_chain(int *depth);

// What the machine shows and holds
uint8_t *o4db_memory(void);                // the megabyte (and the 64 KB past it)
uint16_t o4db_data_segment(void);           // the program's data segment (DS 3173h)
const uint8_t *o4db_registers(void);        // the processor's registers (AX BX CX DX SI DI BP SP ES DS SS CS, 16 bits each)
const uint8_t *o4db_screen(void);          // 320x200 colour indexes
const uint8_t (*o4db_palette(void))[3];    // the DAC: 256 colours of 6-bit components
uint64_t o4db_frames(void);                 // the frame ends passed since the start
int o4db_exited(void);                      // the program ended
int o4db_open_files(void);                  // the game's files open on the host (a state with some does not move)
// Where the program stopped: its call chain (the shadow stack's call sites, from the program's entry, the last its
// backward jump) and its stack pointer (SS = DS: the stack from it to the stack's top is the program's locals)
const uint32_t *o4db_chain(int *length);   // (an element a uint32_t, then 4 bytes of an indirect call's target)
uint16_t o4db_stack_pointer(void);

#ifdef __cplusplus
}
#endif
#endif
