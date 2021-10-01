if get_option('enable_gtk') and get_option('libsysprof')

sysprof_sources = [
  'egg-binding-group.c',
  'sysprof.c',
  'sysprof-application.c',
  'sysprof-window.c',
]

sysprof_resources = gnome.compile_resources('sysprof-resources', 'sysprof.gresource.xml',
  source_dir: 'resources',
      c_name: 'sysprof',
)

sysprof_deps = [
  cc.find_library('m', required: false),
  libsysprof_capture_dep,
  libsysprof_dep,
  libsysprof_ui_dep,
  dependency('pangoft2', required: false),
]

sysprof = executable('sysprof', sysprof_resources + sysprof_sources,
  dependencies: sysprof_deps,
   install_dir: get_option('bindir'),
       install: true,
)

endif
