libdl_dep = cc.find_library('dl', required: false)

preload_deps = [
  dependency('glib-2.0'),
  libsysprof_capture_dep,
  libdl_dep,
]

if get_option('libunwind')
  preload_deps += [libunwind_dep]
endif


libsysprof_memory_preload = shared_library('sysprof-memory-@0@'.format(libsysprof_api_version),
  ['sysprof-memory-collector.c'],
  dependencies: preload_deps,
       install: true,
   install_dir: get_option('libdir'),
)

libsysprof_speedtrack_preload = shared_library('sysprof-speedtrack-@0@'.format(libsysprof_api_version),
  ['sysprof-speedtrack-collector.c'],
  dependencies: preload_deps,
       install: true,
   install_dir: get_option('libdir'),
)
