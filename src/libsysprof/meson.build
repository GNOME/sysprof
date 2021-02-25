if get_option('libsysprof')

libsysprof_c_args = [ '-DSYSPROF_COMPILATION' ]

libsysprof_public_sources = [
  'sysprof-battery-source.c',
  'sysprof-callgraph-profile.c',
  'sysprof-capture-gobject.c',
  'sysprof-capture-symbol-resolver.c',
  'sysprof-control-source.c',
  'sysprof-diskstat-source.c',
  'sysprof-elf-symbol-resolver.c',
  'sysprof-gjs-source.c',
  'sysprof-governor-source.c',
  'sysprof-hostinfo-source.c',
  'sysprof-jitmap-symbol-resolver.c',
  'sysprof-kernel-symbol.c',
  'sysprof-kernel-symbol-resolver.c',
  'sysprof-local-profiler.c',
  'sysprof-memprof-profile.c',
  'sysprof-memprof-source.c',
  'sysprof-netdev-source.c',
  'sysprof-preload-source.c',
  'sysprof-process-model.c',
  'sysprof-process-model-item.c',
  'sysprof-profile.c',
  'sysprof-profiler.c',
  'sysprof-proxy-source.c',
  'sysprof-selection.c',
  'sysprof-source.c',
  'sysprof-spawnable.c',
  'sysprof-symbol-resolver.c',
  'sysprof-symbols-source.c',
  'sysprof-tracefd-source.c',
]

libsysprof_public_headers = [
  'sysprof-battery-source.h',
  'sysprof-callgraph-profile.h',
  'sysprof-capture-autocleanups.h',
  'sysprof-capture-gobject.h',
  'sysprof-capture-symbol-resolver.h',
  'sysprof-control-source.h',
  'sysprof-diskstat-source.h',
  'sysprof-elf-symbol-resolver.h',
  'sysprof-gjs-source.h',
  'sysprof-governor-source.h',
  'sysprof-hostinfo-source.h',
  'sysprof-jitmap-symbol-resolver.h',
  'sysprof-netdev-source.h',
  'sysprof-kernel-symbol.h',
  'sysprof-kernel-symbol-resolver.h',
  'sysprof-local-profiler.h',
  'sysprof-memprof-profile.h',
  'sysprof-memprof-source.h',
  'sysprof-preload-source.h',
  'sysprof-process-model.h',
  'sysprof-process-model-item.h',
  'sysprof-profile.h',
  'sysprof-profiler.h',
  'sysprof-proxy-source.h',
  'sysprof-selection.h',
  'sysprof-source.h',
  'sysprof-spawnable.h',
  'sysprof-symbol-resolver.h',
  'sysprof-symbols-source.h',
  'sysprof-tracefd-source.h',
  'sysprof.h',
]

libsysprof_private_sources = [
  'binfile.c',
  'demangle.cpp',
  'elfparser.c',
  'mapped-ring-buffer-source.c',
  'sysprof-flatpak.c',
  'sysprof-helpers.c',
  'sysprof-kallsyms.c',
  'sysprof-line-reader.c',
  'sysprof-map-lookaside.c',
  'sysprof-mountinfo.c',
  'sysprof-podman.c',
  'sysprof-polkit.c',
  'sysprof-symbol-map.c',
  ipc_service_src,
  stackstash_sources,
  helpers_sources,
]

libsysprof_public_sources += libsysprof_capture_sources

librax = static_library('rax', ['rax.c'],
               c_args: [ '-Wno-declaration-after-statement',
                         '-Wno-format-nonliteral',
                         '-Wno-shadow' ],
)

librax_dep = declare_dependency(
           link_whole: librax,
  include_directories: include_directories('.'),
)

polkit_dep = dependency('polkit-gobject-1', version: polkit_req_version, required: false)
if polkit_dep.found()
  libsysprof_c_args += ['-DHAVE_POLKIT']
endif

if dependency('polkit-gobject-1', version: '>= 0.114', required: false).found()
  libsysprof_c_args += ['-DHAVE_POLKIT_AUTOPTR']
endif

# Subset of dependencies used in generating the pkg-config file
libsysprof_pkg_deps = [
  dependency('gio-2.0', version: glib_req_version),
  dependency('gio-unix-2.0', version: glib_req_version),
  dependency('json-glib-1.0'),
  polkit_dep,
  libsysprof_capture_deps,
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
  libsysprof_pkg_deps += [ dependency('libelf') ]
  libsysprof_c_args += [ '-DNT_GNU_BUILD_ID=3', '-DELF_NOTE_GNU="GNU"', '-D__LIBELF_INTERNAL__' ]
endif

# Meson's pkgconfig module doesn't understand this one
libsysprof_deps = libsysprof_pkg_deps + [
  librax_dep,
]

libsysprof_libs_private = []

if host_machine.system() != 'darwin'
  libsysprof_deps += [cxx.find_library('stdc++')]
  libsysprof_libs_private += '-lstdc++'
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
               libsysprof,
      subdirs: [ sysprof_header_subdir ],
  description: 'The library for console applications embedding sysprof',
  install_dir: join_paths(get_option('libdir'), 'pkgconfig'),
     requires: [ 'gio-2.0' ],
    libraries_private: [libsysprof_libs_private, libsysprof_pkg_deps],
     variables: [
       'datadir=' + datadir_for_pc_file,
     ],
)

install_headers(libsysprof_public_headers, subdir: sysprof_header_subdir)

subdir('preload')

endif
