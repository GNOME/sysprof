test_env = [
  'G_TEST_SRCDIR="@0@"'.format(meson.current_source_dir()),
  'G_TEST_BUILDDIR="@0@"'.format(meson.current_build_dir()),
  'G_DEBUG=gc-friendly',
  'GSETTINGS_BACKEND=memory',
  'MALLOC_CHECK_=2',
  'NO_AT_BRIDGE=1',
]

test_cflags = [
  '-DTEST_DATA_DIR="@0@/data/"'.format(meson.current_source_dir()),
  '-DSYSPROF_COMPILATION=1',
]

test_capture = executable('test-capture', 'test-capture.c',
        c_args: test_cflags,
  dependencies: [libsysprof_capture_dep],
)

test_capture_cursor = executable('test-capture-cursor', 'test-capture-cursor.c',
        c_args: test_cflags,
  dependencies: [libsysprof_capture_dep],
)

test('test-capture', test_capture, env: test_env)
test('test-capture-cursor', test_capture_cursor, env: test_env)

if get_option('libsysprof')

test_deps = [
  libsysprof_dep,
]

test_addr_map = executable('test-addr-map',
  ['test-addr-map.c',
   '../libsysprof/sysprof-map-lookaside.c',
   '../libsysprof/sysprof-symbol-map.c'],
        c_args: test_cflags,
  dependencies: test_deps,
)

test_addr_decode = executable('test-addr-decode', 'test-addr-decode.c',
        c_args: test_cflags,
  dependencies: test_deps,
)

test_mountinfo = executable('test-mountinfo',
  [ 'test-mountinfo.c',
    '../libsysprof/sysprof-mountinfo.c'],
        c_args: test_cflags,
  dependencies: test_deps,
)

test_flatpak = executable('test-flatpak',
  [ 'test-flatpak.c',
    '../libsysprof/sysprof-flatpak.c'],
        c_args: test_cflags,
  dependencies: test_deps,
)

test_resolvers = executable('test-resolvers',
  [ 'test-resolvers.c' ],
        c_args: test_cflags,
  dependencies: test_deps,
)

allocs_by_size = executable('allocs-by-size',
  ['allocs-by-size.c'],
        c_args: test_cflags,
  dependencies: test_deps,
)

cross_thread_frees = executable('cross-thread-frees',
  ['cross-thread-frees.c'],
        c_args: test_cflags,
  dependencies: test_deps,
)

memory_stack_stash = executable('memory-stack-stash',
  ['memory-stack-stash.c'],
        c_args: test_cflags,
  dependencies: test_deps,
)

show_page_usage = executable('show-page-usage',
  [ 'show-page-usage.c' ],
        c_args: test_cflags,
  dependencies: test_deps + [ librax_dep,
                              dependency('cairo') ],
)

if get_option('enable_gtk')

  test_ui_deps = [
    libsysprof_dep,
    libsysprof_ui_dep,
  ]

  test_model_filter = executable('test-model-filter', 'test-model-filter.c',
          c_args: test_cflags,
    dependencies: test_ui_deps,
  )

  test_process_model = executable('test-process-model', 'test-process-model.c',
          c_args: test_cflags,
    dependencies: test_ui_deps,
  )

  test_zoom = executable('test-zoom',
    ['test-zoom.c', '../libsysprof-ui/sysprof-zoom-manager.c'],
          c_args: test_cflags,
    dependencies: test_ui_deps,
  )

  test_capture_view = executable('test-capture-view', 'test-capture-view.c',
          c_args: test_cflags,
    dependencies: test_ui_deps,
  )

  test('test-model-filter', test_model_filter, env: test_env)
  test('test-zoom', test_zoom, env: test_env)

endif
endif
