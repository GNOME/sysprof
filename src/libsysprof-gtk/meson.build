libsysprof_gtk_public_sources = [
  'sysprof-callgraph-view.c',
  'sysprof-weighted-callgraph-view.c',
]

libsysprof_gtk_private_sources = [
]

libsysprof_gtk_public_headers = [
  'sysprof-gtk.h',

  'sysprof-callgraph-view.h',
  'sysprof-weighted-callgraph-view.h',
]

libsysprof_gtk_deps = [
  dependency('gtk4', version: gtk_req_version),

  libsysprof_analyze_dep,
]

libsysprof_gtk_resources = gnome.compile_resources(
  'libsysprof-gtk-resources',
  'libsysprof-gtk.gresource.xml',
  c_name: 'libsysprof_gtk',
)

libsysprof_gtk_static = static_library(
  'sysprof-gtk-@0@'.format(soname_major_version),
  (libsysprof_gtk_public_sources +
   libsysprof_gtk_private_sources +
   libsysprof_gtk_resources +
   mapped_ring_buffer_sources),

    include_directories: [include_directories('.'),
                          ipc_include_dirs,
                          libsysprof_capture_include_dirs],
           dependencies: libsysprof_gtk_deps,
  gnu_symbol_visibility: 'hidden',
)

libsysprof_gtk_static_dep = declare_dependency(
            link_with: libsysprof_gtk_static,
         dependencies: libsysprof_gtk_deps,
  include_directories: [include_directories('.'),
                        libsysprof_capture_include_dirs],
)

libsysprof_gtk = library('sysprof-gtk-@0@'.format(soname_major_version),
           dependencies: [libsysprof_gtk_static_dep],
  gnu_symbol_visibility: 'hidden',
                version: '@0@.0.0'.format(soname_major_version),
        darwin_versions: '@0@.0'.format(soname_major_version),
                install: true,
)

libsysprof_gtk_dep = declare_dependency(
            link_with: libsysprof_gtk,
         dependencies: libsysprof_gtk_deps,
  include_directories: [include_directories('.'), libsysprof_capture_include_dirs],
)
meson.override_dependency('sysprof-gtk-@0@'.format(soname_major_version), libsysprof_gtk_dep)

pkgconfig.generate(libsysprof_gtk,
            subdirs: [sysprof_header_subdir],
        description: 'Provides GTK widgets for sysprof visualizion',
        install_dir: join_paths(get_option('libdir'), 'pkgconfig'),
           requires: ['gtk4'],
          variables: ['datadir=' + datadir_for_pc_file],
)

install_headers(libsysprof_gtk_public_headers, subdir: sysprof_header_subdir)

subdir('tests')
