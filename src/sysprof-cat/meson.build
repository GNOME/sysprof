sysprof_cat_deps = [
  libsysprof_static_dep,
]

sysprof_cat = executable('sysprof-cat', 'sysprof-cat.c',
  dependencies: sysprof_cat_deps,
        c_args: release_flags,
       install: true,
)
