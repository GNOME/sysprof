project('sysprof', ['c', 'cpp'],
  license: ['GPL3+', 'GPL2+'],
  version: '3.22.3',
  meson_version: '>=0.36.0',
  default_options: [
    'c_std=gnu11',
    'cpp_std=c++03',
    'warning_level=2',
  ]
)

cc = meson.get_compiler('c')
config = configuration_data()

config.set_quoted('PACKAGE_NAME', 'sysprof')
config.set_quoted('PACKAGE_VERSION', meson.project_version())
config.set_quoted('PACKAGE_STRING', 'sysprof-' + meson.project_version())
config.set_quoted('PACKAGE_BUGREPORT', 'https://bugzilla.gnome.org/enter_bug.cgi?product=sysprof')
config.set('PACKAGE_TARNAME', 'PACKAGE_STRING')
config.set('PACKAGE', 'PACKAGE_NAME')
config.set('VERSION', 'PACKAGE_VERSION')
# PACKAGE_URL

debugdir = get_option('debugdir')
if debugdir == ''
  debugdir = join_paths(get_option('prefix'), get_option('libdir'), 'debug')
endif
config.set_quoted('DEBUGDIR', debugdir)

config.set_quoted('GETTEXT_PACKAGE', 'sysprof')
config.set10('ENABLE_NLS', true)

has_use_clockid = cc.has_member('struct perf_event_attr', 'use_clockid', prefix: '#include <linux/perf_event.h>')
has_clockid = cc.has_member('struct perf_event_attr', 'clockid', prefix: '#include <linux/perf_event.h>')
if has_use_clockid and has_clockid
  config.set10('HAVE_PERF_CLOCKID', true)
endif

configure_file(
  input: 'config.h.meson',
  output: 'config.h',
  configuration: config
)

add_global_arguments([
  '-DHAVE_CONFIG_H',
  '-I' + meson.build_root(), # config.h
], language: 'c')


c_args_tests = [
  '-Wno-unused-parameter',
  '-Wno-missing-field-initializers',
]
foreach arg: c_args_tests
  if cc.has_argument(arg)
    add_global_arguments(arg, language: 'c')
  endif
endforeach

global_link_args = []
test_link_args = [
  '-Wl,-z,relro',
  '-Wl,-z,now',
]
if not get_option('buildtype').startswith('debug')

  # TODO: Maybe reuse 'b_ndebug' option
  add_global_arguments([
    '-DG_DISABLE_CAST_CHECKS',
    '-DG_DISABLE_ASSERT',
    '-DG_DISABLE_CHECKS',
  ], language: 'c')

  test_link_args += [
    '-Wl,-Bsymbolic',
    '-fno-plt',
  ]

endif

foreach arg: test_link_args
  if cc.has_argument(arg)
    global_link_args += arg
  endif
endforeach
add_global_link_arguments(
  global_link_args,
  language: 'c'
)

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

exe_c_args = []
exe_link_args = []
if cc.has_argument('-fPIE')
  exe_c_args += '-fPIE'
  exe_link_args += '-fpie'
endif

gnome = import('gnome')

subdir('lib')
subdir('daemon')
subdir('src')
subdir('tools')
subdir('tests')

subdir('data')
subdir('help')
subdir('po')

meson.add_install_script('build-aux/meson_post_install.sh')
