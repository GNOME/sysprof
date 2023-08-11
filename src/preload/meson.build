libdl_dep = dependency('dl', required: false)

preload_deps = [
  dependency('glib-2.0'),
  libsysprof_capture_dep,
  libunwind_dep,
  libdl_dep,
]

libsysprof_memory_preload = shared_library('sysprof-memory-@0@'.format(libsysprof_api_version),
  ['sysprof-memory-collector.c'],
  dependencies: preload_deps,
       install: true,
)

libsysprof_speedtrack_preload = shared_library('sysprof-speedtrack-@0@'.format(libsysprof_api_version),
  ['sysprof-speedtrack-collector.c'],
  dependencies: preload_deps,
       install: true,
)

libsysprof_tracer_preload = shared_library('sysprof-tracer-@0@'.format(libsysprof_api_version),
  ['sysprof-tracer-collector.c'],
  dependencies: preload_deps,
       install: true,
)
