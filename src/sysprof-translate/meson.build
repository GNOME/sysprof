sysprof_translate_deps = [
  libsysprof_static_dep,
]

# Do not install for now, until we have a better idea of
# how we want to use this.
sysprof_translate = executable('sysprof-translate', 'sysprof-translate.c',
  dependencies: sysprof_translate_deps,
        c_args: release_flags,
       install: false,
)
