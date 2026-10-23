# quickerOpen4DBoxing

4D Sports Boxing (Distinctive Software, 1991) as a machine a search can step, save and load: the game of
[Open4DBoxing](https://github.com/ToolAssisted-run/Open4DBoxing) (its MCGA build, recompiled whole from the
original program), generated into a form that

- keeps every global the game's run changes per thread, so that any number of machines run at once, one per thread;
- stops the program at the first backward jump of its own flow after a video frame ends (not inside an interrupt
  handler, a sound driver or the BIOS), and resumes it there from its saved state alone: each call site pushes its
  number on a shadow call stack, part of the state, and each function goes back to the site it names;
- gives its inputs as [Chimera's core](https://github.com/ToolAssisted-run/chimera-core-open4dboxing) does (57
  buttons and the mouse's place, at each frame's end), so that a run is a Chimera movie.

`tools/generate.py` writes `source/generated/` from `extern/Open4DBoxing/source`; `source/machine.h` is the machine.
The game's files are not included: the two floppies' images (disk1.img, disk2.img).

## Tests

`tests/o4dbOracle` (Open4DBoxing itself) and `tests/o4dbPlay` (the machine) play a movie and print a hash of the
memory and registers at every frame's end; `o4dbPlay`'s modes play it in one run, stopping at every frame, or moving
the machine between two threads through its saved state at every frame. On the Chimera core's exhibition scenario
(31,200 frames: the boot, the menus, the manual's question, the ring and a round fought), all three are the same as
the oracle at every frame.
