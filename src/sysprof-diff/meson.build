sysprof_diff_deps = [
  libsysprof_static_dep,
]

# Do not install for now, until we have a better idea of
# how we want to use this.
sysprof_diff = executable('sysprof-diff', 'sysprof-diff.c',
  dependencies: sysprof_diff_deps,
        c_args: release_flags,
       install: false,
)
