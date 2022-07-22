icondir = join_paths(datadir, 'icons/hicolor')

foreach kind: ['scalable', 'symbolic']
  install_subdir(kind, install_dir: icondir)
endforeach

install_data('@0@.svg'.format(app_id), install_dir: join_paths(icondir, 'scalable/apps'))
