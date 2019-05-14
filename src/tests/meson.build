test_env = [
  'G_TEST_SRCDIR="@0@"'.format(meson.current_source_dir()),
  'G_TEST_BUILDDIR="@0@"'.format(meson.current_build_dir()),
  'G_DEBUG=gc-friendly',
  'GSETTINGS_BACKEND=memory',
  'MALLOC_CHECK_=2',
]

test_deps = [
  libsysprof_dep,
]

test_cflags = [
  '-DTEST_DATA_DIR="@0@/data/"'.format(meson.current_source_dir()),
]

test_capture = executable('test-capture', 'test-capture.c',
        c_args: test_cflags,
  dependencies: test_deps,
)

test_capture_cursor = executable('test-capture-cursor', 'test-capture-cursor.c',
        c_args: test_cflags,
  dependencies: test_deps,
)

test('test-capture', test_capture, env: test_env)
test('test-capture-cursor', test_capture_cursor, env: test_env)

# Use ./tests/test-kallsyms /proc/kallsyms to test (as user or root)
test_kallsyms = executable('test-kallsyms', 'test-kallsyms.c',
        c_args: test_cflags,
  dependencies: test_deps,
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

  test_zoom = executable('test-zoom', 'test-zoom.c',
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
