project('quickerOpen4DBoxing', 'c', 'cpp',
  version : '1.0.0',
  license : 'GPL-3.0-or-later',
  default_options : [ 'cpp_std=c++20', 'c_std=gnu11', 'buildtype=release' ],
  subproject_dir : 'extern'
)

# The machine: Open4DBoxing's game, generated into a machine that is stepped, saved and loaded (tools/generate.py)
subdir('source')

# Do not build any targets if this is a subproject
if meson.is_subproject() == false

  # Open4DBoxing itself: the oracle the machine is compared with
  open4dboxingSubproject = subproject('Open4DBoxing', default_options : [ 'buildTests=false' ])
  open4dboxingDependency = open4dboxingSubproject.get_variable('open4dboxingDependency')

  subdir('tests')

endif
