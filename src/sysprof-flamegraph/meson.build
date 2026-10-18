sysprof_flamegraph_deps = [
  libsysprof_static_dep,
]

sysprof_flamegraph = executable('sysprof-flamegraph', 'sysprof-flamegraph.c',
  dependencies: sysprof_flamegraph_deps,
        c_args: release_flags,
       install: false,
)
