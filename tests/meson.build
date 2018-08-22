test_env = [
  'G_TEST_SRCDIR="@0@"'.format(meson.current_source_dir()),
  'G_TEST_BUILDDIR="@0@"'.format(meson.current_build_dir()),
  'G_DEBUG=gc-friendly',
  'GSETTINGS_BACKEND=memory',
  'MALLOC_CHECK_=2',
]

test_cflags = [
  '-DTEST_DATA_DIR="@0@/data/"'.format(meson.current_source_dir()),
  '-I' + join_paths(meson.source_root(), 'lib'),
]

test_capture = executable('test-capture', 'test-capture.c',
        c_args: test_cflags,
  dependencies: libsysprof_capture_dep,
)
test('test-capture', test_capture, env: test_env)

test_capture_cursor = executable('test-capture-cursor', 'test-capture-cursor.c',
        c_args: test_cflags,
  dependencies: libsysprof_dep,
)
test('test-capture-cursor', test_capture_cursor, env: test_env)

# Use ./tests/test-kallsyms /proc/kallsyms to test (as user or root)
test_kallsyms = executable('test-kallsyms', ['test-kallsyms.c', '../lib/sp-kallsyms.c'],
        c_args: test_cflags,
  dependencies: libsysprof_dep,
)

if get_option('enable_gtk')

  test_model_filter = executable('test-model-filter', 'test-model-filter.c',
          c_args: test_cflags,
    dependencies: libsysprof_ui_dep,
  )
  test('test-model-filter', test_model_filter, env: test_env)

  test_process_model = executable('test-process-model', 'test-process-model.c',
          c_args: test_cflags,
    dependencies: libsysprof_ui_dep,
  )

  test_zoom = executable('test-zoom', 'test-zoom.c',
          c_args: test_cflags,
    dependencies: libsysprof_ui_dep,
  )
  test('test-zoom', test_zoom, env: test_env)

endif
