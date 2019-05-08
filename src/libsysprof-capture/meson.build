libsysprof_capture_headers = [
  'sp-capture-condition.h',
  'sp-capture-cursor.h',
  'sp-capture-reader.h',
  'sp-capture-types.h',
  'sp-capture-writer.h',
  'sysprof-capture.h',
]

libsysprof_capture_sources = [
  'sp-capture-condition.c',
  'sp-capture-cursor.c',
  'sp-capture-reader.c',
  'sp-capture-writer.c',
  'sp-capture-util.c',
]

configure_file(
          input: 'sysprof-version.h.in',
         output: 'sysprof-version.h',
  configuration: sysprof_version_conf,
    install_dir: join_paths(get_option('includedir'), sysprof_header_subdir),
)

libsysprof_capture_deps = [
  dependency('glib-2.0', version: glib_req_version),
  libshared_dep,
]

install_headers(libsysprof_capture_headers, subdir: sysprof_header_subdir)

libsysprof_capture = static_library('sysprof-capture-@0@'.format(libsysprof_api_version),
  libsysprof_capture_sources,
  dependencies: libsysprof_capture_deps,
        c_args: [ '-DSYSPROF_CAPTURE_COMPILATION' ],
   install_dir: get_option('libdir'),
       install: true,
)

libsysprof_capture_dep = declare_dependency(
           link_whole: libsysprof_capture,
         dependencies: libsysprof_capture_deps,
  include_directories: include_directories('.'),
)

pkgconfig.generate(
      subdirs: [ sysprof_header_subdir ],
      version: meson.project_version(),
         name: 'sysprof-capture-@0@'.format(libsysprof_api_version),
     filebase: 'sysprof-capture-@0@'.format(libsysprof_api_version),
  description: 'The static capture library for tools that generate profiling capture data',
  install_dir: join_paths(get_option('libdir'), 'pkgconfig'),
     requires: [ 'glib-2.0' ],
)
