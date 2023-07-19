libsysprof_profile_public_sources = [
  'sysprof-battery-charge.c',
  'sysprof-cpu-usage.c',
  'sysprof-diagnostic.c',
  'sysprof-disk-usage.c',
  'sysprof-energy-usage.c',
  'sysprof-instrument.c',
  'sysprof-memory-usage.c',
  'sysprof-malloc-tracing.c',
  'sysprof-network-usage.c',
  'sysprof-power-profile.c',
  'sysprof-profiler.c',
  'sysprof-proxied-instrument.c',
  'sysprof-recording.c',
  'sysprof-sampler.c',
  'sysprof-spawnable.c',
  'sysprof-symbols-bundle.c',
  'sysprof-system-logs.c',
  'sysprof-tracer.c',
]

libsysprof_profile_private_sources = [
  'mapped-ring-buffer-source.c',
  'sysprof-controlfd-instrument.c',
  'sysprof-maps-parser.c',
  'sysprof-perf-event-stream.c',
  'sysprof-journald-source.c',
  'sysprof-podman.c',
]

if polkit_dep.found()
  libsysprof_profile_private_sources += ['sysprof-polkit.c']
endif

libsysprof_profile_public_headers = [
  'sysprof-profile.h',

  'sysprof-battery-charge.h',
  'sysprof-cpu-usage.h',
  'sysprof-diagnostic.h',
  'sysprof-disk-usage.h',
  'sysprof-energy-usage.h',
  'sysprof-instrument.h',
  'sysprof-memory-usage.h',
  'sysprof-malloc-tracing.h',
  'sysprof-network-usage.h',
  'sysprof-power-profile.h',
  'sysprof-profiler.h',
  'sysprof-proxied-instrument.h',
  'sysprof-recording.h',
  'sysprof-sampler.h',
  'sysprof-spawnable.h',
  'sysprof-symbols-bundle.h',
  'sysprof-system-logs.h',
  'sysprof-tracer.h',
]

if host_machine.system() == 'linux'
  libsysprof_profile_private_sources += [
    'sysprof-linux-instrument.c',
  ]
endif

libsysprof_profile_deps = [
  dependency('gio-2.0', version: glib_req_version),
  dependency('gio-unix-2.0',
     version: glib_req_version,
    required: host_machine.system() != 'windows'),
  dependency('json-glib-1.0'),
  dependency('libdex-1', version: dex_req_version),
  libsystemd_dep,
  polkit_dep,

  liblinereader_static_dep,
  libsysprof_analyze_dep,
]

libsysprof_profile_static = static_library(
  'sysprof-profile-@0@'.format(soname_major_version),
  (libsysprof_profile_public_sources +
   libsysprof_profile_private_sources +
   mapped_ring_buffer_sources),

    include_directories: [include_directories('.'),
                          libsysprof_capture_include_dirs],
           dependencies: libsysprof_profile_deps,
  gnu_symbol_visibility: 'hidden',
)

libsysprof_profile_static_dep = declare_dependency(
            link_with: libsysprof_profile_static,
         dependencies: libsysprof_profile_deps,
  include_directories: [include_directories('.'),
                        libsysprof_capture_include_dirs],
)

libsysprof_profile = library('sysprof-profile-@0@'.format(soname_major_version),
           dependencies: [libsysprof_profile_static_dep],
  gnu_symbol_visibility: 'hidden',
                version: '@0@.0.0'.format(soname_major_version),
        darwin_versions: '@0@.0'.format(soname_major_version),
                install: true,
)

libsysprof_profile_dep = declare_dependency(
            link_with: libsysprof_profile,
         dependencies: libsysprof_profile_deps,
  include_directories: [include_directories('.'), libsysprof_capture_include_dirs],
)
meson.override_dependency('sysprof-profile-@0@'.format(soname_major_version), libsysprof_profile_dep)

pkgconfig.generate(libsysprof_profile,
            subdirs: [sysprof_header_subdir],
        description: 'A library for recording profiles using various instruments',
        install_dir: join_paths(get_option('libdir'), 'pkgconfig'),
           requires: ['gio-2.0'],
          variables: ['datadir=' + datadir_for_pc_file],
)

install_headers(libsysprof_profile_public_headers, subdir: sysprof_header_subdir)

subdir('tests')
