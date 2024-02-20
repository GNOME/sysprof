sysprofd_c_args = []
sysprofd_link_args = []
sysprofd_deps = [
  libsysprof_capture_dep,
]

if get_option('sysext')
  libc_compat = true
  if target_machine.cpu_family() == 'x86_64'
    sysprofd_c_args += ['-include', 'x86_64/force_link_glibc_2.17.h']
  elif target_machine.cpu_family() == 'x86'
    sysprofd_c_args += ['-include', 'x86/force_link_glibc_2.17.h']
  else
    libc_compat = false
  endif

  if libc_compat
    sysprofd_c_args += '-DLIBC_COMPAT'
    sysprofd_link_args += ['-Wl,--wrap=__libc_start_main']
  endif

  sysprofd_deps += [
    dependency('gio-unix-2.0',
               version: glib_req_version,
               static: true),
    dependency('polkit-gobject-1',
               version: polkit_req_version,
               static: true),
  ]
else
  sysprofd_deps += [
    gio_unix_dep,
    polkit_dep,
  ]
endif

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

sysprofd_sources = [
  'sysprofd.c',
  'ipc-rapl-profiler.c',
  'ipc-service-impl.c',
  'sysprof-turbostat.c',
  'helpers.c',
  ipc_profiler_src,
  ipc_service_src,
]

pkglibexecdir = join_paths(get_option('prefix'), get_option('libexecdir'))

sysprofd = executable('sysprofd', sysprofd_sources,
         dependencies: sysprofd_deps,
              install: true,
          install_dir: pkglibexecdir,
                  pie: true,
  include_directories: [include_directories('.')],
               c_args: sysprofd_c_args,
            link_args: sysprofd_link_args,
)

sysprofdconf = configuration_data()
if get_option('sysext')
  sysprofdconf.set('sysprofdprivdir', '/usr/libexec')
else
  sysprofdconf.set('sysprofdprivdir', pkglibexecdir)
endif

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
