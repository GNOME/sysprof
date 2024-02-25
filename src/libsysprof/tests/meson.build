libsysprof_test_env = [
  'G_DEBUG=gc-friendly',
  'GSETTINGS_BACKEND=memory',
  'MALLOC_CHECK_=2',
]

libsysprof_testsuite_c_args = [
  '-DG_ENABLE_DEBUG',
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
]

libsysprof_testsuite = {
  'read-build-id'                 : {'skip': true},
  'ninja-to-marks'                : {'skip': true},
  'test-allocs-by-func'           : {'skip': true},
  'test-callgraph'                : {'skip': true},
  'test-capture-model'            : {'skip': true},
  'test-cplusplus'                : {'cpp': true},
  'test-elf-loader'               : {'skip': true},
  'test-leak-detector'            : {'skip': true},
  'test-list-counters'            : {'skip': true},
  'test-list-cpu'                 : {'skip': true},
  'test-list-files'               : {'skip': true},
  'test-list-functions-by-weight' : {'skip': true},
  'test-list-jitmap'              : {'skip': true},
  'test-list-overlays'            : {'skip': true},
  'test-maps-parser'              : {'skip': true},
  'test-mark-catalog'             : {'skip': true},
  'test-print-file'               : {'skip': true},
  'test-profiler'                 : {'skip': true},
  'test-list-processes'           : {'skip': true},
  'test-list-address-layout'      : {'skip': true},
  'test-symbolize'                : {'skip': true},
  'test-strings'                  : {},
  'test-symbol-cache'             : {},
}

libsysprof_testsuite_deps = [
  libsysprof_static_dep,
  liblinereader_static_dep,
]

foreach test, params: libsysprof_testsuite
  if params.get('cpp', false)
    test_exe = executable(test, '@0@.cpp'.format(test),
          cpp_args: libsysprof_testsuite_c_args,
      dependencies: libsysprof_testsuite_deps,
    )
  else
    test_exe = executable(test, '@0@.c'.format(test),
            c_args: libsysprof_testsuite_c_args,
      dependencies: libsysprof_testsuite_deps,
    )
  endif

  if not params.get('skip', false)
    test(test, test_exe, env: libsysprof_test_env)
  endif
endforeach
