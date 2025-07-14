if get_option('gtk') and get_option('libsysprof')
  install_data('sysprof-mime.xml',
    install_dir: join_paths(datadir, 'mime/packages')
  )

  metainfo_in = configure_file(
            input: 'org.gnome.Sysprof.metainfo.xml.in.in',
           output: 'org.gnome.Sysprof.metainfo.xml.in',
    configuration: config_h,
  )
  metainfo_file = i18n.merge_file(
            input: metainfo_in,
           output: '@0@.metainfo.xml'.format(app_id),
           po_dir: podir,
          install: true,
      install_dir: join_paths(datadir, 'metainfo')
  )
  # Validate Appdata
  appstreamcli = find_program('appstreamcli', required: false)
  if appstreamcli.found()
    test(
      'validate-metainfo', appstreamcli,
      args: [
        'validate', '--no-net', '--explain', metainfo_file
      ],
      depends: metainfo_file,
    )
  endif

  desktop_in = configure_file(
            input: 'org.gnome.Sysprof.desktop.in.in',
           output: 'org.gnome.Sysprof.desktop.in',
    configuration: config_h,
  )
  i18n.merge_file(
            input: desktop_in,
           output: '@0@.desktop'.format(app_id),
             type: 'desktop',
           po_dir: podir,
          install: true,
      install_dir: join_paths(datadir, 'applications')
  )

  # D-Bus service file.
  dbusconf = configuration_data()
  dbusconf.set('app_id', app_id)
  dbusconf.set('bindir', join_paths(get_option('prefix'), get_option('bindir')))
  configure_file(
            input: 'org.gnome.Sysprof.service.in',
           output: '@0@.service'.format(app_id),
    configuration: dbusconf,
          install: true,
      install_dir: join_paths(get_option('datadir'), 'dbus-1', 'services'),
  )

  subdir('icons')
endif
