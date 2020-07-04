if get_option('with_sysprofd') == 'bundled'

sysprofd_sources = [
  '../libsysprof/sysprof-kallsyms.c',
  'sysprofd.c',
  'ipc-legacy-impl.c',
  'ipc-rapl-profiler.c',
  'ipc-service-impl.c',
  'sysprof-turbostat.c',
  helpers_sources,
  ipc_legacy_src,
  ipc_profiler_src,
  ipc_service_src,
]

pkglibexecdir = join_paths(get_option('prefix'), get_option('libexecdir'))

sysprofd_deps = [
  dependency('glib-2.0', version: glib_req_version),
  dependency('gio-2.0', version: glib_req_version),
  dependency('gio-unix-2.0', version: glib_req_version),
  dependency('polkit-gobject-1', version: polkit_req_version),
  libsysprof_capture_dep,
]

sysprofd = executable('sysprofd', sysprofd_sources,
         dependencies: sysprofd_deps,
              install: true,
          install_dir: pkglibexecdir,
                  pie: true,
  include_directories: [include_directories('.'), ipc_include_dirs],
)

sysprofdconf = configuration_data()
sysprofdconf.set('sysprofdprivdir', pkglibexecdir)

systemdunitdir = get_option('systemdunitdir')
if systemdunitdir == ''
  systemdunitdir = dependency('systemd').get_pkgconfig_variable('systemdsystemunitdir')
endif

#
# For org.gnome.Sysprof3
#

configure_file(
          input: 'org.gnome.Sysprof3.service.in',
         output: 'org.gnome.Sysprof3.service',
  configuration: sysprofdconf,
    install_dir: join_paths(datadir, 'dbus-1/system-services'),
)

configure_file(
          input: 'org.gnome.Sysprof3.conf.in',
         output: 'org.gnome.Sysprof3.conf',
  configuration: sysprofdconf,
    install_dir: join_paths(datadir, 'dbus-1/system.d'),
)

configure_file(
          input: 'sysprof3.service.in',
         output: 'sysprof3.service',
  configuration: sysprofdconf,
    install_dir: systemdunitdir,
)

i18n.merge_file(
        input: 'org.gnome.sysprof3.policy.in',
       output: 'org.gnome.sysprof3.policy',
       po_dir: podir,
      install: true,
  install_dir: join_paths(datadir, 'polkit-1/actions'),
)

#
# For org.gnome.Sysprof2 Compatibility
#

configure_file(
          input: 'org.gnome.Sysprof2.service.in',
         output: 'org.gnome.Sysprof2.service',
  configuration: sysprofdconf,
    install_dir: join_paths(datadir, 'dbus-1/system-services'),
)

configure_file(
          input: 'org.gnome.Sysprof2.conf.in',
         output: 'org.gnome.Sysprof2.conf',
  configuration: sysprofdconf,
    install_dir: join_paths(datadir, 'dbus-1/system.d'),
)

configure_file(
          input: 'sysprof2.service.in',
         output: 'sysprof2.service',
  configuration: sysprofdconf,
    install_dir: systemdunitdir,
)

endif
