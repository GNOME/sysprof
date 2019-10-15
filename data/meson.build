if get_option('enable_gtk') and get_option('libsysprof')

  install_data('sysprof-mime.xml',
    install_dir: join_paths(datadir, 'mime/packages')
  )

  install_data('org.gnome.sysprof3.gschema.xml',
    install_dir: join_paths(datadir, 'glib-2.0/schemas')
  )

  i18n.merge_file(
            input: 'org.gnome.Sysprof3.appdata.xml.in',
           output: 'org.gnome.Sysprof3.appdata.xml',
           po_dir: podir,
          install: true,
      install_dir: join_paths(datadir, 'metainfo')
  )

  i18n.merge_file(
            input: 'org.gnome.Sysprof3.desktop.in',
           output: 'org.gnome.Sysprof3.desktop',
             type: 'desktop',
           po_dir: podir,
          install: true,
      install_dir: join_paths(datadir, 'applications')
  )

  icon_sizes = ['scalable', 'symbolic']
  foreach size: icon_sizes
    install_subdir('icons/' + size,
      install_dir: join_paths(datadir, 'icons/hicolor')
    )
  endforeach

endif
