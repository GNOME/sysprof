libsysprof_analyze_public_sources = [
  'sysprof-document.c',
  'sysprof-document-frame.c',
  'sysprof-document-log.c',
  'sysprof-document-mmap.c',
  'sysprof-document-sample.c',
]

libsysprof_analyze_public_headers = [
  'sysprof-analyze.h',
  'sysprof-document.h',
  'sysprof-document-frame.h',
  'sysprof-document-log.h',
  'sysprof-document-mmap.h',
  'sysprof-document-sample.h',
]

libsysprof_analyze_deps = [
  dependency('gio-2.0', version: glib_req_version),
  libsysprof_capture_deps,
]

libsysprof_analyze = library('sysprof-analyze',
                             libsysprof_analyze_public_sources,
    include_directories: [include_directories('.'),
                          ipc_include_dirs,
                          libsysprof_capture_include_dirs],
           dependencies: libsysprof_analyze_deps,
  gnu_symbol_visibility: 'hidden',
                version: '@0@.0.0'.format(soname_major_version),
        darwin_versions: '@0@.0'.format(soname_major_version),
)

libsysprof_analyze_dep = declare_dependency(
            link_with: libsysprof_analyze,
         dependencies: libsysprof_analyze_deps,
  include_directories: [include_directories('.'), libsysprof_capture_include_dirs],
)
meson.override_dependency('sysprof-analyze-@0@'.format(soname_major_version), libsysprof_analyze_dep)

pkgconfig.generate(libsysprof_analyze,
            subdirs: [sysprof_header_subdir],
        description: 'A library for processing and analyzing Sysprof captures',
        install_dir: join_paths(get_option('libdir'), 'pkgconfig'),
           requires: ['gio-2.0'],
          variables: ['datadir=' + datadir_for_pc_file],
)

install_headers(libsysprof_analyze_public_headers, subdir: sysprof_header_subdir)

subdir('tests')
