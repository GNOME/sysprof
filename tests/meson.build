test_env = [
  'G_TEST_SRCDIR="@0@"'.format(meson.current_source_dir()),
  'G_TEST_BUILDDIR="@0@"'.format(meson.current_build_dir()),
  'G_DEBUG=gc-friendly',
  'GSETTINGS_BACKEND=memory',
  'MALLOC_CHECK_=2',
]

test_capture = executable('test-capture',
  'test-capture.c',
  dependencies: libsysprof_dep,
)
test('test-capture', test_capture, env: test_env)

test_capture_cursor = executable('test-capture-cursor',
  'test-capture-cursor.c',
  dependencies: libsysprof_dep,
)
test('test-capture-cursor', test_capture_cursor, env: test_env)

if get_option('enable_gtk')

  test_model_filter = executable('test-model_filter',
    'test-model-filter.c',
    dependencies: libsysprof_ui_dep,
  )
  test('test-model-filter', test_model_filter, env: test_env)

  test_process_model = executable('test-process-model',
    'test-process-model.c',
    dependencies: libsysprof_ui_dep,
  )
  test('test-process-model', test_process_model, env: test_env)

  test_zoom = executable('test-zoom',
    'test-zoom.c',
    dependencies: libsysprof_ui_dep,
  )
  test('test-zoom', test_zoom, env: test_env)

endif
