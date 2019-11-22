sysprof_header_subdir = 'sysprof-@0@'.format(libsysprof_api_version)

sysprof_version_conf = configuration_data()
sysprof_version = meson.project_version().split('.')
sysprof_version_conf.set('MAJOR_VERSION', sysprof_version[0])
sysprof_version_conf.set('MINOR_VERSION', sysprof_version[1])
sysprof_version_conf.set('MICRO_VERSION', sysprof_version[2])
sysprof_version_conf.set('VERSION', meson.project_version())

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

ipc_legacy_src = gnome.gdbus_codegen('ipc-legacy',
           sources: 'org.gnome.Sysprof2.xml',
  interface_prefix: 'org.gnome.',
         namespace: 'IpcLegacy',
)

if get_option('with_sysprofd') == 'bundled'
  install_data(['org.gnome.Sysprof3.Service.xml',
                'org.gnome.Sysprof2.xml'],
    install_dir: join_paths(datadir, 'dbus-1/interfaces'),
  )
endif

if get_option('libsysprof')
  install_data(['org.gnome.Sysprof3.Profiler.xml'],
    install_dir: join_paths(datadir, 'dbus-1/interfaces'),
  )
endif

ipc_include_dirs = include_directories('.')

stackstash_sources = files([
  'stackstash.c',
])

helpers_sources = files([
  'helpers.c',
])

subdir('sysprofd')
subdir('libsysprof')
subdir('libsysprof-ui')
subdir('sysprof')
subdir('tools')
subdir('tests')
