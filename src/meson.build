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

if install_service_files
  install_data([
      'org.gnome.Sysprof3.Profiler.xml',
      'org.gnome.Sysprof3.Service.xml',
    ],
    install_dir: join_paths(datadir, 'dbus-1/interfaces'),
  )
endif

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

if get_option('gtk')
  subdir('sysprof')
endif

if get_option('tools')
  subdir('sysprof-agent')
  subdir('sysprof-cli')
endif
