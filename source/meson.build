# The machine's sources: the generated game (tools/generate.py) and the machine around it
quickerOpen4DBoxingSources = files(
  'machine.c',
  'generated/asm2c.c', 'generated/box_core.c', 'generated/vga.c', 'generated/cheats.c', 'generated/dos.c',
  'generated/drv_ad15.c', 'generated/drv_mt15.c', 'generated/drv_pc15.c', 'generated/drv_sb15.c', 'generated/drv_td15.c',
  'generated/mpu401.c', 'generated/opl.c', 'generated/sb.c', 'generated/tandy.c', 'generated/dosmem.c',
  'generated/disks.c', 'generated/exe.c', 'generated/game.c', 'generated/pit.c', 'generated/replay.c',
  'generated/dosboot.c', 'generated/version.c',
)

quickerOpen4DBoxingIncludes = include_directories('.', 'generated')

# (the machine's globals are thread-local, resolved where the program starts: the initial-exec model even in a
# position-independent library)
quickerOpen4DBoxingLib = static_library('quickerOpen4DBoxing', quickerOpen4DBoxingSources,
  c_args : [ '-Wfatal-errors', '-Wall', '-Wno-unused-label', '-ftls-model=initial-exec' ],
  include_directories : quickerOpen4DBoxingIncludes,
)

quickerOpen4DBoxingDependency = declare_dependency(
  link_whole : quickerOpen4DBoxingLib,
  include_directories : include_directories('.'),
  dependencies : [ dependency('threads') ],
)
