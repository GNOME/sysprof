project('sysprof-capture', ['c', 'cpp'],
          license: ['BSD-2-Clause-Patent'],
          version: '3.35.1',
    meson_version: '>=0.50.0',
  default_options: [ 'c_std=gnu11',
                     'warning_level=2',
  ]
)

pkgconfig = import('pkgconfig')

libsysprof_api_version = 3
version_split = meson.project_version().split('.')

glib_req_version = '>= 2.61.3'
gtk_req_version = '>= 3.22'
polkit_req_version = '>= 0.105'
dazzle_req_version = '>= 3.30.0'

cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')

config_h = configuration_data()

# Detect and set symbol visibility
if get_option('default_library') != 'static'
  if host_machine.system() == 'windows'
    config_h.set('DLL_EXPORT', true)
    if cc.get_id() == 'msvc'
      config_h.set('_SYSPROF_EXTERN', '__declspec(dllexport) extern')
    elif cc.has_argument('-fvisibility=hidden')
      config_h.set('_SYSPROF_EXTERN', '__attribute__((visibility("default"))) __declspec(dllexport) extern')
    endif
  elif cc.has_argument('-fvisibility=hidden')
    config_h.set('_SYSPROF_EXTERN', '__attribute__((visibility("default"))) extern')
  endif
endif

glib_dep = dependency('glib-2.0', version: glib_req_version)

global_c_args = []
test_c_args = [
  '-Wcast-align',
  '-Wdeclaration-after-statement',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Wmissing-include-dirs',
  '-Wnested-externs',
  '-Wno-missing-field-initializers',
  '-Wno-sign-compare',
  '-Wno-unused-parameter',
  '-Wno-cast-function-type',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wswitch-default',
  '-Wswitch-enum',
  '-Wuninitialized',
  ['-Werror=format-security', '-Werror=format=2' ],
  '-Werror=empty-body',
  '-Werror=implicit-function-declaration',
  '-Werror=incompatible-pointer-types',
  '-Werror=pointer-arith',
  '-Werror=init-self',
  '-Werror=int-conversion',
  '-Werror=misleading-indentation',
  '-Werror=missing-include-dirs',
  '-Werror=overflow',
  '-Werror=parenthesis',
  '-Werror=return-type',
  '-Werror=shadow',
  '-Werror=strict-prototypes',
  '-Werror=undef',
]
if get_option('buildtype') != 'plain'
  test_c_args += '-fstack-protector-strong'
endif

foreach arg: test_c_args
  if cc.has_multi_arguments(arg)
    global_c_args += arg
  endif
endforeach

add_project_arguments(global_c_args, language: 'c')

release_flags = []
global_link_args = []
test_link_args = [
  '-Wl,-z,relro',
  '-Wl,-z,now',
]
if not get_option('buildtype').startswith('debug')
  release_flags += [
    '-DG_DISABLE_CAST_CHECKS',
    '-DG_DISABLE_ASSERT',
  ]
  test_link_args += [
    '-Wl,-Bsymbolic',
    '-fno-plt',
  ]
endif

foreach link_arg: test_link_args
  if cc.has_link_argument(link_arg)
    global_link_args += link_arg
  endif
endforeach
add_project_link_arguments(global_link_args, language: 'c')

if not cc.links('''
#include <stdatomic.h>
int main(void) {
  atomic_thread_fence(memory_order_acquire);
  atomic_thread_fence(memory_order_seq_cst);
  return 0;
}
''')
  error('Sysprof requires a C compiler with stdatomic support such as GCC 4.9 or newer')
endif

conf_include_dirs = include_directories('.')

configure_file(
         output: 'capture-config.h',
  configuration: config_h
)

subdir('src')
