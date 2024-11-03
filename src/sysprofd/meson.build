ipc_profiler_src = gnome.gdbus_codegen('ipc-profiler',
           sources: 'org.gnome.Sysprof3.Profiler.xml',
  interface_prefix: 'org.gnome.Sysprof3.',
         namespace: 'Ipc',
)

ipc_service_src = gnome.gdbus_codegen('ipc-service',
           sources: 'org.gnome.Sysprof3.Service.xml',
  interface_prefix: 'org.gnome.Sysprof3.',
         namespace: 'Ipc',
)

ipc_unwinder_src = gnome.gdbus_codegen('ipc-unwinder',
           sources: 'org.gnome.Sysprof3.Unwinder.xml',
  interface_prefix: 'org.gnome.Sysprof3.',
         namespace: 'Ipc',
)

sysprofd_sources = [
  'sysprofd.c',
  'ipc-rapl-profiler.c',
  'ipc-service-impl.c',
  'ipc-unwinder-impl.c',
  'sysprof-turbostat.c',
  'helpers.c',
  ipc_profiler_src,
  ipc_service_src,
  ipc_unwinder_src,
]

sysprofd_deps = [
  glib_dep,
  gio_dep,
  gio_unix_dep,
  polkit_dep,

  libsysprof_capture_dep,
]

sysprofd = executable('sysprofd', sysprofd_sources,
         dependencies: sysprofd_deps,
              install: true,
          install_dir: pkglibexecdir,
                  pie: true,
  include_directories: [include_directories('.')],
)

sysprofdconf = configuration_data()
sysprofdconf.set('sysprofdprivdir', pkglibexecdir)

systemdunitdir = get_option('systemdunitdir')
if systemdunitdir == ''
  systemdunitdir = dependency('systemd').get_variable(pkgconfig: 'systemdsystemunitdir')
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

install_data(
  'org.gnome.Sysprof3.Profiler.xml',
  'org.gnome.Sysprof3.Service.xml',
  install_dir: join_paths(datadir, 'dbus-1/interfaces'),
)
