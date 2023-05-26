libsysprof_profile_public_sources = [
  'sysprof-instrument.c',
  'sysprof-profiler.c',
  'sysprof-recording.c',
]

libsysprof_profile_private_sources = [
  'sysprof-controlfd-instrument.c',
  'sysprof-polkit.c',
]

libsysprof_profile_public_headers = [
  'sysprof-profile.h',

  'sysprof-instrument.h',
  'sysprof-profiler.h',
  'sysprof-recording.h',
]

libsysprof_profile_deps = [
  dependency('gio-2.0', version: glib_req_version),
  dependency('libdex-1', version: dex_req_version),
  dependency('polkit-gobject-1', version: polkit_req_version),

  libsysprof_capture_dep,
]

libsysprof_profile_static = static_library(
  'sysprof-profile-@0@'.format(soname_major_version),
  libsysprof_profile_public_sources +
  libsysprof_profile_private_sources,

    include_directories: [include_directories('.'),
                          ipc_include_dirs,
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
