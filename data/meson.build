datadir = get_option('datadir')
podir = join_paths(meson.source_root(), 'po')

install_data('sysprof-mime.xml',
  install_dir: join_paths(datadir, 'mime/packages')
)

install_data('org.gnome.sysprof2.gschema.xml',
  install_dir: join_paths(datadir, 'glib-2.0/schemas')
)

i18n.merge_file(
    input: 'org.gnome.Sysprof2.appdata.xml.in',
    output: 'org.gnome.Sysprof2.appdata.xml',
    po_dir: podir,
    install: true,
    install_dir: join_paths(datadir, 'metainfo')
)

i18n.merge_file(
    input: 'org.gnome.Sysprof2.desktop.in',
    output: 'org.gnome.Sysprof2.desktop',
    type: 'desktop',
    po_dir: podir,
    install: true,
    install_dir: join_paths(datadir, 'applications')
)

pkgconf = configuration_data()
pkgconf.set('VERSION', meson.project_version())
pkgconf.set('API_VERSION', libsysprof_api_version)
pkgconf.set('prefix', get_option('prefix'))
pkgconf.set('libdir', join_paths('${exec_prefix}', get_option('libdir')))
pkgconf.set('includedir', join_paths('${prefix}', get_option('includedir')))

pkgconfigdir = join_paths(get_option('libdir'), 'pkgconfig')
configure_file(
  input: 'sysprof.pc.in',
  output: 'sysprof-@0@.pc'.format(libsysprof_api_version),
  configuration: pkgconf,
  install: true,
  install_dir: pkgconfigdir,
)
configure_file(
  input: 'sysprof-capture.pc.in',
  output: 'sysprof-capture-@0@.pc'.format(libsysprof_api_version),
  configuration: pkgconf,
  install: true,
  install_dir: pkgconfigdir,
)
if get_option('enable_gtk')
  configure_file(
    input: 'sysprof-ui.pc.in',
    output: 'sysprof-ui-@0@.pc'.format(libsysprof_api_version),
    configuration: pkgconf,
    install: true,
    install_dir: pkgconfigdir,
  )

  icon_sizes = ['16x16', '24x24', '32x32', '48x48', '256x256', 'scalable']
  foreach size: icon_sizes
    install_subdir('icons/' + size,
      install_dir: join_paths(datadir, 'icons/hicolor')
    )
  endforeach
endif

if get_option('with_sysprofd') == 'bundled'

  sysprofdconf = configuration_data()
  sysprofdconf.set('sysprofdprivdir', pkglibexecdir)

  configure_file(
    input: 'org.gnome.Sysprof2.service.in',
    output: 'org.gnome.Sysprof2.service',
    configuration: sysprofdconf,
    install: true,
    install_dir: join_paths(datadir, 'dbus-1/system-services'),
  )
  configure_file(
    input: 'org.gnome.Sysprof2.conf.in',
    output: 'org.gnome.Sysprof2.conf',
    configuration: sysprofdconf,
    install: true,
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
    install: true,
    install_dir: systemdunitdir,
  )

  i18n.merge_file(
    input: 'org.gnome.sysprof2.policy.in',
    output: 'org.gnome.sysprof2.policy',
    configuration: sysprofdconf,
    po_dir: podir,
    install: true,
    install_dir: join_paths(datadir, 'polkit-1/actions'),
  )

endif
