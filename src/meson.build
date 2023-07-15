sysprof_header_subdir = 'sysprof-@0@'.format(libsysprof_api_version)

sysprof_version_conf = configuration_data()
sysprof_version = meson.project_version().split('.')
if sysprof_version[1] in ['alpha', 'beta', 'rc']
  sysprof_version = [sysprof_version[0], 0, 0]
endif
sysprof_version_conf.set('MAJOR_VERSION', sysprof_version[0])
sysprof_version_conf.set('MINOR_VERSION', sysprof_version[1])
sysprof_version_conf.set('MICRO_VERSION', sysprof_version[2])
sysprof_version_conf.set('VERSION', meson.project_version())

if needs_service_access
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

  ipc_agent_src = gnome.gdbus_codegen('ipc-agent',
             sources: 'org.gnome.Sysprof.Agent.xml',
    interface_prefix: 'org.gnome.Sysprof.',
           namespace: 'Ipc',
  )
endif

if install_service_files
  install_data([
      'org.gnome.Sysprof2.xml',
      'org.gnome.Sysprof3.Profiler.xml',
      'org.gnome.Sysprof3.Service.xml',
      'org.gnome.Sysprof.Agent.xml'
    ],
    install_dir: join_paths(datadir, 'dbus-1/interfaces'),
  )
endif

ipc_include_dirs = include_directories('.')

stackstash_sources = files('stackstash.c')

helpers_sources = files('helpers.c')

if need_libsysprof_capture
  subdir('libsysprof-capture')
endif

if need_libsysprof_analyze
  subdir('libsysprof-analyze')
endif

if need_libsysprof_profile
  subdir('libsysprof-profile')
  subdir('preload')
endif

if get_option('sysprofd') == 'bundled'
  subdir('sysprofd')
endif
if get_option('libsysprof') or get_option('agent')
  subdir('libsysprof')
endif
if get_option('gtk') and get_option('libsysprof')
  subdir('sysprof')
endif
if get_option('tools')
  subdir('tools')
endif
if get_option('tests')
  subdir('tests')
endif
