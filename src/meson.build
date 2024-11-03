sysprof_header_subdir = 'sysprof-@0@'.format(libsysprof_api_version)
sysprof_header_dir = join_paths(get_option('includedir'), sysprof_header_subdir)

sysprof_version_conf = configuration_data()
sysprof_version = meson.project_version().split('.')
if sysprof_version[1] in ['alpha', 'beta', 'rc']
  sysprof_version = [sysprof_version[0], 0, 0]
endif
sysprof_version_conf.set('MAJOR_VERSION', sysprof_version[0])
sysprof_version_conf.set('MINOR_VERSION', sysprof_version[1])
sysprof_version_conf.set('MICRO_VERSION', 0)
sysprof_version_conf.set('VERSION', meson.project_version())

pkglibexecdir = join_paths(get_option('prefix'), get_option('libexecdir'))

subdir('libsysprof-capture')

if need_libsysprof
  subdir('libsysprof')
  subdir('preload')
endif

if get_option('sysprofd') == 'bundled'
  subdir('sysprofd')
  subdir('sysprof-live-unwinder')
endif

if get_option('gtk')
  subdir('sysprof')
endif

if get_option('tools')
  subdir('sysprof-agent')
  subdir('sysprof-cat')
  subdir('sysprof-cli')
  subdir('sysprof-diff')
endif
