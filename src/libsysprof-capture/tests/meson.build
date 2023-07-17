libsysprof_capture_test_env = [
  'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
  'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
  'G_DEBUG=gc-friendly',
  'GSETTINGS_BACKEND=memory',
  'MALLOC_CHECK_=2',
  'NO_AT_BRIDGE=1',
]

libsysprof_capture_testsuite_c_args = [
  '-DSYSPROF_COMPILATION',
  '-DG_ENABLE_DEBUG',
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
]

libsysprof_capture_testsuite = {
  'test-mapped-ring-buffer' : {},
}

libsysprof_capture_testsuite_deps = [
  dependency('glib-2.0'),
  libsysprof_capture_dep,
]

foreach test, params: libsysprof_capture_testsuite
  test_exe = executable(test, '@0@.c'.format(test),
          c_args: libsysprof_capture_testsuite_c_args,
    dependencies: libsysprof_capture_testsuite_deps,
  )
  if not params.get('skip', false)
    test(test, test_exe, env: libsysprof_capture_test_env)
  endif
endforeach
