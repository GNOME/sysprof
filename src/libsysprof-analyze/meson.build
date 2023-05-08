libsysprof_analyze_public_sources = [
  'sysprof-bundled-symbolizer.c',
  'sysprof-document.c',
  'sysprof-document-allocation.c',
  'sysprof-document-exit.c',
  'sysprof-document-file-chunk.c',
  'sysprof-document-fork.c',
  'sysprof-document-frame.c',
  'sysprof-document-log.c',
  'sysprof-document-mark.c',
  'sysprof-document-metadata.c',
  'sysprof-document-mmap.c',
  'sysprof-document-process.c',
  'sysprof-document-process-list.c',
  'sysprof-document-sample.c',
  'sysprof-document-symbols.c',
  'sysprof-document-traceable.c',
  'sysprof-multi-symbolizer.c',
  'sysprof-symbol.c',
  'sysprof-symbolizer.c',
]

libsysprof_analyze_private_sources = [
  'sysprof-document-bitset-index.c',
  'sysprof-mapped-file.c',
  'sysprof-memory-map.c',
  'sysprof-mount-device.c',
  'sysprof-mount-namespace.c',
]

libsysprof_analyze_public_headers = [
  'sysprof-analyze.h',
  'sysprof-bundled-symbolizer.h',
  'sysprof-document.h',
  'sysprof-document-allocation.h',
  'sysprof-document-exit.h',
  'sysprof-document-file-chunk.h',
  'sysprof-document-fork.h',
  'sysprof-document-frame.h',
  'sysprof-document-log.h',
  'sysprof-document-mark.h',
  'sysprof-document-metadata.h',
  'sysprof-document-mmap.h',
  'sysprof-document-process.h',
  'sysprof-document-process-list.h',
  'sysprof-document-sample.h',
  'sysprof-document-symbols.h',
  'sysprof-document-traceable.h',
  'sysprof-multi-symbolizer.h',
  'sysprof-symbol.h',
  'sysprof-symbolizer.h',
]

libsysprof_analyze_deps = [
  dependency('gio-2.0', version: glib_req_version),
  dependency('gtk4', version: gtk_req_version),

  libsysprof_capture_dep,
]

libsysprof_analyze = library('sysprof-analyze-@0@'.format(soname_major_version),
                             libsysprof_analyze_public_sources +
                             libsysprof_analyze_private_sources,
    include_directories: [include_directories('.'),
                          ipc_include_dirs,
                          libsysprof_capture_include_dirs],
           dependencies: libsysprof_analyze_deps,
  gnu_symbol_visibility: 'hidden',
                version: '@0@.0.0'.format(soname_major_version),
        darwin_versions: '@0@.0'.format(soname_major_version),
                install: true,
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
