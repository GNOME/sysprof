libsysprof_memory_preload_deps = [
  cc.find_library('dl', required: false),
  libsysprof_capture_dep,
  libunwind_dep,
]

libsysprof_memory_preload_sources = [
  'sysprof-memory-collector.c',
]

libsysprof_memory_preload = shared_library('sysprof-memory-@0@'.format(libsysprof_api_version),
  libsysprof_memory_preload_sources,
  dependencies: libsysprof_memory_preload_deps,
       install: true,
   install_dir: get_option('libexecdir'),
)
