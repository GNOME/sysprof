
# We split up the library code into two libraries.  One containing the
# minimum necessary to do profiling on a particular host (that might not
# have GTK+ installed), and the library containing reusable GTK
# components (for IDE integration).

libsysprof_api_version = '2'
libsysprof_header_subdir = 'sysprof-' + libsysprof_api_version

libsysprof_version_conf = configuration_data()
libsysprof_version = meson.project_version().split('.')
libsysprof_version_conf.set('MAJOR_VERSION', libsysprof_version[0])
libsysprof_version_conf.set('MINOR_VERSION', libsysprof_version[1])
libsysprof_version_conf.set('MICRO_VERSION', libsysprof_version[2])
libsysprof_version_conf.set('VERSION', meson.project_version())

configure_file(
          input: 'sysprof-version.h.in',
         output: 'sysprof-version.h',
  configuration: libsysprof_version_conf,
        install: true,
    install_dir: join_paths(get_option('includedir'), 'sysprof-' + libsysprof_api_version)
)

libsysprof_base_headers = [
  'sysprof.h',
  'sysprof-capture.h',
  'sp-address.h',
  'sp-clock.h',
  'sp-error.h',
  'sp-kallsyms.h',
]

libsysprof_base_sources = [
  'sp-address.c',
  'sp-clock.c',
  'sp-error.c',
  'sp-kallsyms.c',
]

libsysprof_headers = libsysprof_base_headers
libsysprof_sources = libsysprof_base_sources

libsysprof_ui_base_headers = ['sysprof-ui.h']
libsysprof_ui_headers = libsysprof_ui_base_headers
libsysprof_ui_sources = []

libsysprof_capture_sources = libsysprof_base_sources

subdir('callgraph')
subdir('capture')
subdir('profiler')
subdir('sources')
subdir('symbols')
subdir('util')
subdir('visualizers')
subdir('widgets')

cxx = meson.get_compiler('cpp')

libsysprof_capture_deps = [
  dependency('gobject-2.0'),
]

libsysprof_deps = [
  cxx.find_library('stdc++'),
  dependency('gio-unix-2.0'),
]

version_link_arg = '-Wl,--version-script,' + join_paths(meson.current_source_dir(), 'sysprof.map')
libsysprof_c_args = []

if get_option('with_sysprofd') != 'none'
  libsysprof_deps += dependency('polkit-gobject-1')
  libsysprof_c_args += '-DENABLE_POLKIT'
endif

libsysprof_capture = static_library('sysprof-capture-' + libsysprof_api_version,
  libsysprof_capture_sources,
  dependencies: libsysprof_capture_deps,
        c_args: libsysprof_c_args,
       install: true,
)

libsysprof = shared_library('sysprof-' + libsysprof_api_version,
  libsysprof_sources,
  dependencies: libsysprof_deps,
        c_args: libsysprof_c_args,
     link_args: version_link_arg,
  link_depends: 'sysprof.map',
       install: true,
)

libsysprof_dep = declare_dependency(
  include_directories: include_directories('.'),
     link_with: libsysprof,
  dependencies: libsysprof_deps,
)

libsysprof_capture_dep = declare_dependency(
  include_directories: include_directories('.'),
     link_with: libsysprof_capture,
  dependencies: libsysprof_capture_deps,
)

install_headers(libsysprof_base_headers, subdir: libsysprof_header_subdir)

if get_option('enable_gtk')
  # This is our GTK library containing the widgets suitable for viewing
  # and manipulating the various profiler API in libsysprof.  This is
  # meant to be used by IDEs and the sysprof gui.

  libsysprof_ui_resources = gnome.compile_resources(
    'sp-ui-resources', 'resources/libsysprof.gresource.xml',
    source_dir: 'resources',
        c_name: 'sp',
  )

  libsysprof_ui_deps = [
    libsysprof_dep,
    dependency('gtk+-3.0', version: '>=3.22.0'),
  ]

  libsysprof_ui = shared_library('sysprof-ui-' + libsysprof_api_version,
    libsysprof_ui_resources + libsysprof_ui_sources,
    dependencies: libsysprof_ui_deps,
       link_args: version_link_arg,
    link_depends: 'sysprof.map',
         install: true,
  )

  libsysprof_ui_dep = declare_dependency(
           dependencies: libsysprof_ui_deps,
              link_with: libsysprof_ui,
    include_directories: include_directories('.'),
  )

  install_headers(libsysprof_ui_base_headers, subdir: libsysprof_header_subdir)

endif
