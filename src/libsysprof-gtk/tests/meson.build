libsysprof_gtk_test_env = [
  'G_DEBUG=gc-friendly',
  'GSETTINGS_BACKEND=memory',
  'MALLOC_CHECK_=2',
]

libsysprof_gtk_testsuite_c_args = [
  '-DG_LOG_DOMAIN="libdex"',
  '-DG_ENABLE_DEBUG',
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
]

libsysprof_gtk_testsuite = {
  'test-callgraph'  : {'skip': true},
  'test-charts'     : {'skip': true},
  'test-mark-chart' : {'skip': true},
  'test-mark-table' : {'skip': true},
}

libsysprof_gtk_testsuite_deps = [
  libsysprof_analyze_static_dep,
  libsysprof_gtk_static_dep,
]

tests_resources = gnome.compile_resources(
  'tests-resources',
  'tests.gresource.xml',
  c_name: 'tests',
)

foreach test, params: libsysprof_gtk_testsuite
  test_exe = executable(test,
    ['@0@.c'.format(test), tests_resources[0]],
          c_args: libsysprof_gtk_testsuite_c_args,
    dependencies: libsysprof_gtk_testsuite_deps,
  )
  if not params.get('skip', false)
    test(test, test_exe, env: libsysprof_gtk_test_env)
  endif
endforeach
