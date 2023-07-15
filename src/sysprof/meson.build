sysprof_sources = [
  'main.c',
  'sysprof-application.c',
  'sysprof-counters-section.c',
  'sysprof-cpu-info-dialog.c',
  'sysprof-cpu-section.c',
  'sysprof-files-section.c',
  'sysprof-frame-utility.c',
  'sysprof-greeter.c',
  'sysprof-logs-section.c',
  'sysprof-marks-section.c',
  'sysprof-memory-section.c',
  'sysprof-metadata-section.c',
  'sysprof-process-dialog.c',
  'sysprof-processes-section.c',
  'sysprof-recording-pad.c',
  'sysprof-samples-section.c',
  'sysprof-section.c',
  'sysprof-sidebar.c',
  'sysprof-single-model.c',
  'sysprof-time-scrubber.c',
  'sysprof-traceables-utility.c',
  'sysprof-window.c',
]

sysprof_resources = gnome.compile_resources('sysprof-resources', 'sysprof.gresource.xml',
  source_dir: 'resources',
      c_name: 'sysprof',
)

sysprof_deps = [
  cc.find_library('m', required: false),
  dependency('libadwaita-1', version: '>= 1.4.alpha'),
  dependency('libpanel-1', version: '>= 1.3.0'),
  libsysprof_analyze_static_dep,
  libsysprof_profile_static_dep,
  libsysprof_gtk_static_dep,
]

sysprof = executable('sysprof', sysprof_resources + sysprof_sources,
  dependencies: sysprof_deps,
   install_dir: get_option('bindir'),
       install: true,
)
