libsysprof_capture_headers = files([
  'sysprof-address.h',
  'sysprof-clock.h',
  'sysprof-capture-condition.h',
  'sysprof-capture-cursor.h',
  'sysprof-capture-reader.h',
  'sysprof-capture-types.h',
  'sysprof-capture-writer.h',
  'sysprof-collector.h',
  'sysprof-macros.h',
  'sysprof-platform.h',
  'sysprof-capture.h',
  'sysprof-version-macros.h',
])

install_headers(libsysprof_capture_headers, subdir: sysprof_header_subdir)

libsysprof_capture_sources = files([
  'mapped-ring-buffer.c',
  'sysprof-address.c',
  'sysprof-capture-condition.c',
  'sysprof-capture-cursor.c',
  'sysprof-capture-reader.c',
  'sysprof-capture-util.c',
  'sysprof-capture-writer.c',
  'sysprof-capture-writer-cat.c',
  'sysprof-collector.c',
  'sysprof-clock.c',
  'sysprof-platform.c',
])

configure_file(
          input: 'sysprof-version.h.in',
         output: 'sysprof-version.h',
  configuration: sysprof_version_conf,
    install_dir: join_paths(get_option('includedir'), sysprof_header_subdir),
)

libsysprof_capture_deps = [
  glib_dep,
  gio_dep,
  gio_unix_dep,
]

libsysprof_capture = static_library(
  'sysprof-capture-@0@'.format(libsysprof_api_version),
  libsysprof_capture_sources,

           dependencies: libsysprof_capture_deps,
                 c_args: [ '-DSYSPROF_CAPTURE_COMPILATION' ],
            install_dir: get_option('libdir'),
                install: true,
  gnu_symbol_visibility: 'hidden',
)

libsysprof_capture_include_dirs = include_directories('.')

libsysprof_capture_dep = declare_dependency(
           link_whole: libsysprof_capture,
         dependencies: libsysprof_capture_deps,
  include_directories: libsysprof_capture_include_dirs,
)

pkgconfig.generate(
    libraries: [libsysprof_capture],
      subdirs: [ sysprof_header_subdir ],
      version: meson.project_version(),
         name: 'sysprof-capture-@0@'.format(libsysprof_api_version),
     filebase: 'sysprof-capture-@0@'.format(libsysprof_api_version),
  description: 'The static capture library for tools that generate profiling capture data',
  install_dir: join_paths(get_option('libdir'), 'pkgconfig'),
     requires: [ 'glib-2.0' ],
     variables: [
       'datadir=' + datadir_for_pc_file,
     ],
)
