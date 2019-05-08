libsysprof_c_args = [ '-DSYSPROF_COMPILATION' ]

libsysprof_public_sources = [
  'sp-callgraph-profile.c',
  'sp-capture-gobject.c',
  'sp-elf-symbol-resolver.c',
  'sp-hostinfo-source.c',
  'sp-jitmap-symbol-resolver.c',
  'sp-kallsyms.c',
  'sp-kernel-symbol.c',
  'sp-kernel-symbol-resolver.c',
  'sp-local-profiler.c',
  'sp-map-lookaside.c',
  'sp-process-model.c',
  'sp-process-model-item.c',
  'sp-profile.c',
  'sp-profiler.c',
  'sp-selection.c',
  'sp-source.c',
  'sp-symbol-dirs.c',
  'sp-symbol-resolver.c',
]

libsysprof_public_headers = [
  'sp-callgraph-profile.h',
  'sp-capture-gobject.h',
  'sp-elf-symbol-resolver.h',
  'sp-hostinfo-source.h',
  'sp-jitmap-symbol-resolver.h',
  'sp-kallsyms.h',
  'sp-kernel-symbol.h',
  'sp-kernel-symbol-resolver.h',
  'sp-local-profiler.h',
  'sp-map-lookaside.h',
  'sp-process-model.h',
  'sp-process-model-item.h',
  'sp-profile.h',
  'sp-profiler.h',
  'sp-selection.h',
  'sp-source.h',
  'sp-symbol-dirs.h',
  'sp-symbol-resolver.h',
  'sysprof.h',
]

libsysprof_private_sources = [
  '../stackstash.c',
  'binfile.c',
  'demangle.cpp',
  'elfparser.c',
  'sp-source-util.c',
  'sp-line-reader.c',
]

libsysprof_deps = [
  dependency('gio-2.0', version: glib_req_version),
  dependency('gio-unix-2.0', version: glib_req_version),
  libsysprof_capture_dep,
]

if host_machine.system() == 'linux'
  libsysprof_public_sources += [
    'sp-gjs-source.c',
    'sp-memory-source.c',
    'sp-perf-counter.c',
    'sp-perf-source.c',
    'sp-proc-source.c',
  ]

  libsysprof_public_headers += [
    'sp-gjs-source.h',
    'sp-memory-source.h',
    'sp-perf-counter.h',
    'sp-perf-source.h',
    'sp-proc-source.h',
  ]
endif

if host_machine.system() == 'darwin'
  libsysprof_deps += [ dependency('libelf') ]
  libsysprof_c_args = [ '-DNT_GNU_BUILD_ID=3', '-DELF_NOTE_GNU="GNU"', '-D__LIBELF_INTERNAL__' ]
endif

if host_machine.system() != 'darwin'
  libsysprof_deps += [cxx.find_library('stdc++')]
endif

if get_option('with_sysprofd') != 'none'
  libsysprof_deps += dependency('polkit-gobject-1')
endif

libsysprof = shared_library(
  'sysprof-@0@'.format(libsysprof_api_version),
  libsysprof_public_sources + libsysprof_private_sources,

           dependencies: libsysprof_deps,
                 c_args: libsysprof_c_args,
                install: true,
            install_dir: get_option('libdir'),
  gnu_symbol_visibility: 'hidden',
)

libsysprof_dep = declare_dependency(
            link_with: libsysprof,
         dependencies: libsysprof_deps,
  include_directories: include_directories('.'),
)

pkgconfig.generate(
      subdirs: [ sysprof_header_subdir ],
      version: meson.project_version(),
         name: 'sysprof-@0@'.format(libsysprof_api_version),
     filebase: 'sysprof-@0@'.format(libsysprof_api_version),
  description: 'The library for console applications embedding sysprof',
  install_dir: join_paths(get_option('libdir'), 'pkgconfig'),
     requires: [ 'gio-2.0' ],
)

install_headers(libsysprof_public_headers, subdir: sysprof_header_subdir)
