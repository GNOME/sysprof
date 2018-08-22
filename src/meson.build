if get_option('enable_gtk')

sysprof_sources = [
  'sysprof.c',
  'sp-application.c',
  'sp-application.h',
  'sp-credits.h',
  'sp-window.c',
  'sp-window.h',
  'sp-window-settings.c',
  'sp-window-settings.h',
]

sysprof_resources = gnome.compile_resources('sp-resources', 'resources/sysprof.gresource.xml',
  source_dir: 'resources',
      c_name: 'sysprof',
)

sysprof = executable('sysprof', sysprof_resources + sysprof_sources,
       gui_app: true,
       install: true,
        c_args: release_flags + exe_c_args,
  dependencies: [libsysprof_ui_dep, cc.find_library('m', required: false)],
     link_args: exe_link_args,
)

endif
