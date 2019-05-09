if get_option('with_sysprofd') == 'bundled'

ipc_service_src = gnome.gdbus_codegen('ipc-service',
           sources: 'org.gnome.Sysprof3.Service.xml',
  interface_prefix: 'org.gnome.Sysprof3.',
         namespace: 'Ipc',
)

sysprofd_sources = [
  'sysprofd.c',
  'ipc-service-impl.c',
  ipc_service_src,
]

pkglibexecdir = join_paths(get_option('prefix'), get_option('libexecdir'))

sysprofd_deps = [
  dependency('libsystemd', version: '>=222'),
  dependency('glib-2.0', version: glib_req_version),
  dependency('gio-2.0', version: glib_req_version),
  dependency('gio-unix-2.0', version: glib_req_version),
]

sysprofd = executable('sysprofd', sysprofd_sources,
         dependencies: sysprofd_deps,
              install: true,
          install_dir: pkglibexecdir,
                  pie: true,
)

sysprofdconf = configuration_data()
sysprofdconf.set('sysprofdprivdir', pkglibexecdir)

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

systemdunitdir = get_option('systemdunitdir')
if systemdunitdir == ''
  systemdunitdir = dependency('systemd').get_pkgconfig_variable('systemdsystemunitdir')
endif
configure_file(
          input: 'sysprof2.service.in',
         output: 'sysprof2.service',
  configuration: sysprofdconf,
    install_dir: systemdunitdir,
)

i18n.merge_file(
        input: 'org.gnome.sysprof2.policy.in',
       output: 'org.gnome.sysprof2.policy',
       po_dir: podir,
      install: true,
  install_dir: join_paths(datadir, 'polkit-1/actions'),
)

endif
