project('sysprof', ['c', 'cpp'],
  license: ['GPL3+', 'GPL2+'],
  version: '3.30.1',
  meson_version: '>=0.40.1',
  default_options: [
    'c_std=gnu11',
    'cpp_std=c++03',
    'warning_level=2',
  ]
)

cc = meson.get_compiler('c')
config_h = configuration_data()

config_h.set_quoted('PACKAGE_NAME', 'sysprof')
config_h.set_quoted('PACKAGE_VERSION', meson.project_version())
config_h.set_quoted('PACKAGE_STRING', 'sysprof-' + meson.project_version())
config_h.set_quoted('PACKAGE_BUGREPORT', 'https://bugzilla.gnome.org/enter_bug.cgi?product=sysprof')
config_h.set('PACKAGE_TARNAME', 'PACKAGE_STRING')
config_h.set('PACKAGE', 'PACKAGE_NAME')
config_h.set('VERSION', 'PACKAGE_VERSION')
# PACKAGE_URL

debugdir = get_option('debugdir')
if debugdir == ''
  debugdir = join_paths(get_option('prefix'), get_option('libdir'), 'debug')
endif
config_h.set_quoted('DEBUGDIR', debugdir)

config_h.set_quoted('GETTEXT_PACKAGE', 'sysprof')
config_h.set10('ENABLE_NLS', true)

has_use_clockid = cc.has_member('struct perf_event_attr', 'use_clockid', prefix: '#include <linux/perf_event.h>')
has_clockid = cc.has_member('struct perf_event_attr', 'clockid', prefix: '#include <linux/perf_event.h>')
if has_use_clockid and has_clockid
  config_h.set10('HAVE_PERF_CLOCKID', true)
endif

add_global_arguments([
  '-DHAVE_CONFIG_H',
  '-I' + meson.build_root(), # config.h
], language: 'c')

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

# Detect and set symbol visibility
hidden_visibility_args = []
if get_option('default_library') != 'static'
  if host_machine.system() == 'windows'
    config_h.set('DLL_EXPORT', true)
    if cc.get_id() == 'msvc'
      config_h.set('_SP_EXTERN', '__declspec(dllexport) extern')
    elif cc.has_argument('-fvisibility=hidden')
      config_h.set('_SP_EXTERN', '__attribute__((visibility("default"))) __declspec(dllexport) extern')
      hidden_visibility_args = ['-fvisibility=hidden']
    endif
  elif cc.has_argument('-fvisibility=hidden')
    config_h.set('_SP_EXTERN', '__attribute__((visibility("default"))) extern')
    hidden_visibility_args = ['-fvisibility=hidden']
  endif
endif

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

foreach arg: test_link_args
  if cc.has_argument(arg)
    global_link_args += arg
  endif
endforeach
add_global_link_arguments(global_link_args, language: 'c')

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

configure_file(
          input: 'config.h.meson',
         output: 'config.h',
  configuration: config_h
)

gnome = import('gnome')

subdir('lib')
subdir('daemon')
subdir('src')
subdir('tools')
subdir('tests')

subdir('data')
subdir('help')
subdir('po')

meson.add_install_script('build-aux/meson/post_install.sh')
