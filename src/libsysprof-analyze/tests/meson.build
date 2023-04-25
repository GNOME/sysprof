libsysprof_analyze_test_env = [
  'G_DEBUG=gc-friendly',
  'GSETTINGS_BACKEND=memory',
  'MALLOC_CHECK_=2',
]

libsysprof_analyze_testsuite_c_args = [
  '-DG_LOG_DOMAIN="libdex"',
  '-DG_ENABLE_DEBUG',
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
]

libsysprof_analyze_testsuite = {
  'test-capture-model': {'skip': true},
}

libsysprof_analyze_testsuite_deps = [
  libsysprof_analyze_dep,
  libsysprof_capture_dep,
]

foreach test, params: libsysprof_analyze_testsuite
  test_exe = executable(test, '@0@.c'.format(test),
          c_args: libsysprof_analyze_testsuite_c_args,
    dependencies: libsysprof_analyze_testsuite_deps,
  )
  if not params.get('skip', false)
    test(test, test_exe, env: libsysprof_analyze_test_env)
  endif
endforeach
