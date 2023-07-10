libsysprof_gtk_public_sources = [
  'sysprof-axis.c',
  'sysprof-callgraph-view.c',
  'sysprof-chart.c',
  'sysprof-chart-layer.c',
  'sysprof-column-layer.c',
  'sysprof-duplex-layer.c',
  'sysprof-line-layer.c',
  'sysprof-mark-chart.c',
  'sysprof-mark-table.c',
  'sysprof-normalized-series.c',
  'sysprof-normalized-series-item.c',
  'sysprof-series.c',
  'sysprof-session.c',
  'sysprof-session-model.c',
  'sysprof-session-model-item.c',
  'sysprof-split-layer.c',
  'sysprof-time-label.c',
  'sysprof-time-ruler.c',
  'sysprof-time-span-layer.c',
  'sysprof-time-series.c',
  'sysprof-time-series-item.c',
  'sysprof-track.c',
  'sysprof-track-view.c',
  'sysprof-tracks-view.c',
  'sysprof-value-axis.c',
  'sysprof-weighted-callgraph-view.c',
  'sysprof-xy-layer.c',
  'sysprof-xy-series.c',
  'sysprof-xy-series-item.c',
]

libsysprof_gtk_public_headers = [
  'sysprof-gtk.h',

  'sysprof-axis.h',
  'sysprof-callgraph-view.h',
  'sysprof-chart.h',
  'sysprof-chart-layer.h',
  'sysprof-column-layer.h',
  'sysprof-duplex-layer.h',
  'sysprof-line-layer.h',
  'sysprof-mark-chart.h',
  'sysprof-mark-table.h',
  'sysprof-normalized-series.h',
  'sysprof-normalized-series-item.h',
  'sysprof-series.h',
  'sysprof-session.h',
  'sysprof-session-model.h',
  'sysprof-session-model-item.h',
  'sysprof-split-layer.h',
  'sysprof-time-label.h',
  'sysprof-time-ruler.h',
  'sysprof-time-series.h',
  'sysprof-time-series-item.h',
  'sysprof-time-span-layer.h',
  'sysprof-track.h',
  'sysprof-track-view.h',
  'sysprof-tracks-view.h',
  'sysprof-weighted-callgraph-view.h',
  'sysprof-value-axis.h',
  'sysprof-xy-layer.h',
  'sysprof-xy-series.h',
  'sysprof-xy-series-item.h',
]

libsysprof_gtk_private_sources = [
  'sysprof-color-iter.c',
  'sysprof-css.c',
  'sysprof-mark-chart-item.c',
  'sysprof-mark-chart-row.c',
  'sysprof-progress-cell.c',
  'sysprof-scheduler.c',
  'sysprof-session-discover.c',
  'sysprof-symbol-label.c',
]

libsysprof_gtk_deps = [
  dependency('libadwaita-1'),
  dependency('libpanel-1'),
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
