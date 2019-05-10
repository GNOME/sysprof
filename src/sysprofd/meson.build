if get_option('with_sysprofd') == 'bundled'

sysprofd_sources = [
  'sysprofd.c',
  'ipc-service-impl.c',
  helpers_sources,
  ipc_service_src,
]

pkglibexecdir = join_paths(get_option('prefix'), get_option('libexecdir'))

sysprofd_deps = [
  glib_dep,
  gio_dep,
  gio_unix_dep,
  polkit_dep,
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

systemdunitdir = get_option('systemdunitdir')
if systemdunitdir == ''
  systemdunitdir = dependency('systemd').get_pkgconfig_variable('systemdsystemunitdir')
endif
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

endif
