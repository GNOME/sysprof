libsysprof_c_args = [ '-DSYSPROF_COMPILATION' ]

libsysprof_public_sources = [
  'sysprof-callgraph-profile.c',
  'sysprof-capture-gobject.c',
  'sysprof-elf-symbol-resolver.c',
  'sysprof-hostinfo-source.c',
  'sysprof-jitmap-symbol-resolver.c',
  'sysprof-kallsyms.c',
  'sysprof-kernel-symbol.c',
  'sysprof-kernel-symbol-resolver.c',
  'sysprof-local-profiler.c',
  'sysprof-map-lookaside.c',
  'sysprof-process-model.c',
  'sysprof-process-model-item.c',
  'sysprof-profile.c',
  'sysprof-profiler.c',
  'sysprof-proxy-source.c',
  'sysprof-selection.c',
  'sysprof-source.c',
  'sysprof-symbol-dirs.c',
  'sysprof-symbol-resolver.c',
  'sysprof-tracefd-source.c',
]

libsysprof_public_headers = [
  'sysprof-callgraph-profile.h',
  'sysprof-capture-gobject.h',
  'sysprof-elf-symbol-resolver.h',
  'sysprof-hostinfo-source.h',
  'sysprof-jitmap-symbol-resolver.h',
  'sysprof-kallsyms.h',
  'sysprof-kernel-symbol.h',
  'sysprof-kernel-symbol-resolver.h',
  'sysprof-local-profiler.h',
  'sysprof-map-lookaside.h',
  'sysprof-process-model.h',
  'sysprof-process-model-item.h',
  'sysprof-profile.h',
  'sysprof-profiler.h',
  'sysprof-proxy-source.h',
  'sysprof-selection.h',
  'sysprof-source.h',
  'sysprof-symbol-dirs.h',
  'sysprof-symbol-resolver.h',
  'sysprof-tracefd-source.h',
  'sysprof.h',
]

libsysprof_private_sources = [
  'binfile.c',
  'demangle.cpp',
  'elfparser.c',
  'sysprof-helpers.c',
  'sysprof-line-reader.c',
  ipc_service_src,
  stackstash_sources,
  helpers_sources,
]

libsysprof_public_sources += libsysprof_capture_sources
libsysprof_public_headers += libsysprof_capture_headers

libsysprof_deps = [
  gio_dep,
  gio_unix_dep,
  polkit_dep,
]

if host_machine.system() == 'linux'
  libsysprof_public_sources += [
    'sysprof-memory-source.c',
    'sysprof-perf-counter.c',
    'sysprof-perf-source.c',
    'sysprof-proc-source.c',
  ]

  libsysprof_public_headers += [
    'sysprof-memory-source.h',
    'sysprof-perf-counter.h',
    'sysprof-perf-source.h',
    'sysprof-proc-source.h',
  ]
endif

if host_machine.system() == 'darwin'
  libsysprof_deps += [ dependency('libelf') ]
  libsysprof_c_args = [ '-DNT_GNU_BUILD_ID=3', '-DELF_NOTE_GNU="GNU"', '-D__LIBELF_INTERNAL__' ]
endif

if host_machine.system() != 'darwin'
  libsysprof_deps += [cxx.find_library('stdc++')]
endif

libsysprof = shared_library(
  'sysprof-@0@'.format(libsysprof_api_version),
  libsysprof_public_sources + libsysprof_private_sources,

    include_directories: [include_directories('.'),
                          ipc_include_dirs,
                          libsysprof_capture_include_dirs],
           dependencies: libsysprof_deps,
                 c_args: libsysprof_c_args,
                install: true,
            install_dir: get_option('libdir'),
  gnu_symbol_visibility: 'hidden',
)

libsysprof_dep = declare_dependency(
            link_with: libsysprof,
         dependencies: libsysprof_deps,
  include_directories: [include_directories('.'), libsysprof_capture_include_dirs],
)

pkgconfig.generate(
    libraries: [libsysprof],
      subdirs: [ sysprof_header_subdir ],
      version: meson.project_version(),
         name: 'sysprof-@0@'.format(libsysprof_api_version),
     filebase: 'sysprof-@0@'.format(libsysprof_api_version),
  description: 'The library for console applications embedding sysprof',
  install_dir: join_paths(get_option('libdir'), 'pkgconfig'),
     requires: [ 'gio-2.0' ],
)

install_headers(libsysprof_public_headers, subdir: sysprof_header_subdir)