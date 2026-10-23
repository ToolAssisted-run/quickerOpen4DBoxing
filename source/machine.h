// quickerOpen4DBoxing's machine: 4D Sports Boxing (Open4DBoxing, its MCGA build) stepped a video frame at a time,
// saved and loaded. One machine runs on a thread at a time (its state is the thread's: tools/generate.py); a machine
// is moved between threads, or several share one, through its saved state (o4db_save, o4db_load).
//
// The inputs are Chimera's core's (chimera-core-open4dboxing's waterbox/boxing-driver.c): 57 buttons (the mouse's two,
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

#define O4DB_BUTTONS 57
#define O4DB_MOUSE_X_NEUTRAL 160
#define O4DB_MOUSE_Y_NEUTRAL 100

// The buttons (bit k: boxing-driver.h's BOX_BTN k: 0 the mouse's left, 1 its right, 2-11 the keypad's 8 2 4 6 7 9 1
// 3 5 0, 12 Enter, 13 Space, 14 Backspace, 15 Esc, 16 Tab, 17 left Shift, 18 right Shift, 19 Ctrl, 20 Alt, 21-46 the
// letters a-z, 47-56 the digits 0-9) and the mouse's place (0-319, 0-199)
typedef struct
{
  uint64_t buttons;
  int16_t mouseX, mouseY;
} O4dbInput;

// The machine's setup (the same for every machine of a search): the folder with the floppies' images, the sound
// ("mt32", "sb", "adlib", "speaker", "tandy": the driver the game loads, which changes its timing), the PC's clock at
// the start ("YYYY-MM-DD HH:MM:SS"), and where the game's writes go (a folder of the thread's own; NULL: one made in
// /dev/shm). 0 on failure, the reason in o4db_error().
int o4db_setup(const char *gameDir, const char *sound, const char *clockStart, const char *writeDir);
const char *o4db_error(void);

// A new machine, not started: its first run starts it with boot (the inputs before the program starts: Chimera's
// first step's)
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

// The machine's state: its size, a copy of it, the state taken back (on any thread with the same setup)
size_t o4db_state_size(void);
void o4db_save(void *out);
void o4db_load(const void *in);

// The same as the memory pages that differ from a reference: the machine's memory now (the first machine of the
// process to call it sets it; the others' must be the same: the count of pages that are not is returned), and the
// states as the pages of the memory (256 bytes each, at most maxPages) that differ from it, with the rest of the
// machine. o4db_save_delta answers the pages it took, or -1 when there are more than maxPages (nothing saved).
int o4db_set_reference(void);
size_t o4db_delta_size(unsigned maxPages);
int o4db_save_delta(void *out, unsigned maxPages);
void o4db_load_delta(const void *in, unsigned maxPages);

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
