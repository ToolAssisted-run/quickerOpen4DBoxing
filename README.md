# quickerOpen4DBoxing

4D Sports Boxing (Distinctive Software, 1991) as a machine a search can step, save and load: the game of
[Open4DBoxing](https://github.com/ToolAssisted-run/Open4DBoxing) (its MCGA build, recompiled whole from the
original program), generated into a form that

- keeps every global the game's run changes per thread, so that any number of machines run at once, one per thread;
- stops the program at the first backward jump of its own flow after a video frame ends (not inside an interrupt
  handler, a sound driver or the BIOS), and resumes it there from its saved state alone: each call site pushes its
  number on a shadow call stack, part of the state, and each function goes back to the site it names;
- gives its inputs as [Chimera's core](https://github.com/ToolAssisted-run/chimera-core-open4dboxing) does (67
  buttons and the mouse's place, at each frame's end), so that a run is a Chimera movie.

`tools/generate.py` writes `source/generated/` from `extern/Open4DBoxing/source`; `source/machine.h` is the machine.
The game's files are not included: the two floppies' images (disk1.img, disk2.img).

## Steps, states and the fight's logic alone

- A step ends where the game reads the boxers' controls (25bb:0202, the timer's routine, every 100 ms of a fight):
  a search's step is the input of the game's own sampling.
- A state is the 256-byte pages of the memory that differ from the search's start, and the rest of the machine.
  Every write marks its page, so a state saved or loaded touches only the pages written; the memory the game
  only draws into (its screen and drawing buffers) can be left out (`o4db_set_unsaved`): about 60 KB a fight's state.
- The machine's speed is a setting (`o4db_set_speed`, DOSBox's cycles, 22,000 by default; Chimera's core's CPU
  Speed). At 220,000 every frame of the game fits in one of its timer's 10 ms ticks, and its fight goes the same
  whatever its frames draw. There `o4db_set_fast` runs a round's logic alone: no scene drawn, the frames with
  nothing to do and the busy waits on the timer skipped to its next tick. One bug of the game's ties its fight to
  its drawing: a punch that misses takes the defender's stamina by a local variable never set, the stack's garbage
  of the last frame drawn; the fast machine notices the read and runs that step again with whole frames drawn before
  its tick (all but about one miss in 200 then read the game's garbage; the rest, a timer interrupt's in the middle
  of the game's drawing, cannot be known without it). A round's end, its break and the next round's start go as
  the game's (the time the game's drawing takes there is lost to its clock).

## Tests

`tests/o4dbOracle` (Open4DBoxing itself) and `tests/o4dbPlay` (the machine) play a movie and print a hash of the
memory and registers at every frame's end; `o4dbPlay`'s modes play it in one run, stopping at every frame, or moving
the machine between two threads through its saved state at every frame. On the Chimera core's exhibition scenario
(31,200 frames: the boot, the menus, the manual's question, the ring and a round fought), all three are the same as
the oracle at every frame.

`tests/o4dbFastCheck` runs the same steps as the game and as the fast fight and compares every boxer's step (their
structures, statistics, the random number, the controls' replay) and every step's frames; `o4dbSteps2Rows` turns a
JaffarPlus solution into Chimera rows by playing it in the whole game, and with `FASTCHECK` checks it against the fast
fight. `o4dbScramble` (memory the game never reads back), `o4dbFrameFree` (what the fight's logic depends on, with a
tracing build of the machine: every read and write and the instruction's address), `o4dbThreads`, `o4dbBench`.
