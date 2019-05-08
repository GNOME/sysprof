if get_option('enable_gtk')

sysprof_sources = [
  'sysprof.c',
  'sp-application.c',
  'sp-window.c',
  'sp-window-settings.c',
]

sysprof_resources = gnome.compile_resources('sysprof-resources', 'sysprof.gresource.xml',
  source_dir: 'resources',
      c_name: 'sysprof',
)

sysprof_deps = [
  cc.find_library('m', required: false),
  libsysprof_ui_dep,
]

sysprof = executable('sysprof', sysprof_resources + sysprof_sources,
  dependencies: sysprof_deps,
       gui_app: true,
   install_dir: get_option('bindir'),
       install: true,
          pie: true,
)

endif
