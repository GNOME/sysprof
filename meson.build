project('sysprof', 'c',
          license: ['GPL3+', 'GPL2+'],
          version: '3.40.1',
    meson_version: '>=0.51.0',
  default_options: [ 'c_std=gnu11',
                     'cpp_std=c++11',
                     'warning_level=2',
  ]
)

symbolic_version = '40.0'

gnome = import('gnome')
pkgconfig = import('pkgconfig')
i18n = import('i18n')

libsysprof_api_version = 4
version_split = meson.project_version().split('.')
datadir = get_option('datadir')
datadir_for_pc_file = join_paths('${prefix}', datadir)
podir = join_paths(meson.source_root(), 'po')

glib_req_version = '>= 2.67.4'
gtk_req_version = '>= 3.22'
polkit_req_version = '>= 0.105'
dazzle_req_version = '>= 3.30.0'

cc = meson.get_compiler('c')

if get_option('libsysprof')
  add_languages('cpp')
  cxx = meson.get_compiler('cpp')
endif

config_h = configuration_data()
config_h.set_quoted('SYMBOLIC_VERSION', symbolic_version)
config_h.set_quoted('API_VERSION_S', '@0@'.format(libsysprof_api_version))
config_h.set_quoted('PACKAGE_NAME', 'sysprof')
config_h.set_quoted('PACKAGE_VERSION', meson.project_version())
config_h.set_quoted('PACKAGE_STRING', 'sysprof-' + meson.project_version())
config_h.set_quoted('PACKAGE_BUGREPORT', 'https://gitlab.gnome.org/GNOME/sysprof/-/issues/new')
config_h.set_quoted('PACKAGE_LIBEXECDIR', join_paths(get_option('prefix'), get_option('libexecdir')))
config_h.set_quoted('PACKAGE_LIBDIR', join_paths(get_option('prefix'), get_option('libdir')))
config_h.set('PACKAGE_TARNAME', 'PACKAGE_STRING')
config_h.set('PACKAGE', 'PACKAGE_NAME')
config_h.set('VERSION', 'PACKAGE_VERSION')

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

debugdir = get_option('debugdir')
if debugdir == ''
  debugdir = join_paths(get_option('prefix'), get_option('libdir'), 'debug')
endif
config_h.set_quoted('DEBUGDIR', debugdir)

config_h.set_quoted('GETTEXT_PACKAGE', 'sysprof')
config_h.set10('ENABLE_NLS', true)
config_h.set_quoted('PACKAGE_LOCALE_DIR', join_paths(get_option('prefix'), get_option('datadir'), 'locale'))
config_h.set('LOCALEDIR', 'PACKAGE_LOCALE_DIR')
config_h.set('HAVE_EXECINFO_H', cc.has_header('execinfo.h'))

config_h.set('HAVE_STRLCPY', cc.has_function('strlcpy'))
config_h.set('HAVE_REALLOCARRAY', cc.has_function('reallocarray'))

polkit_agent_dep = dependency('polkit-agent-1', required: false)
polkit_dep = dependency('polkit-gobject-1', version: polkit_req_version, required: false)

config_h.set10('HAVE_POLKIT_AGENT', polkit_agent_dep.found())
config_h.set10('HAVE_POLKIT', polkit_dep.found())

if get_option('libunwind')
  libunwind_dep = dependency('libunwind-generic', required: false)
  config_h.set('ENABLE_LIBUNWIND', libunwind_dep.found())
  config_h.set('HAVE_UNW_SET_CACHE_SIZE', libunwind_dep.found() and cc.has_header_symbol('libunwind.h', 'unw_set_cache_size', dependencies: [libunwind_dep]))
endif

# Development build setup
config_h.set('DEVELOPMENT_BUILD', version_split[1].to_int().is_odd())

has_use_clockid = cc.has_member('struct perf_event_attr', 'use_clockid', prefix: '#include <linux/perf_event.h>')
has_clockid = cc.has_member('struct perf_event_attr', 'clockid', prefix: '#include <linux/perf_event.h>')
config_h.set('HAVE_PERF_CLOCKID', has_use_clockid and has_clockid)

add_project_arguments([
  '-I' + meson.current_build_dir(), # config.h
], language: 'c')

global_c_args = [
  '-D_GNU_SOURCE',
  '-D_POSIX_C_SOURCE=200809L',
]
if host_machine.system() == 'darwin'
  global_c_args += ['-D_DARWIN_C_SOURCE']
endif
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

# Until GLib is fixed with regards to volatile type registration
if cc.get_id() == 'clang'
  test_c_args += ['-Wno-incompatible-pointer-types']
else
  test_c_args += ['-Werror=incompatible-pointer-types']
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

if get_option('buildtype') != 'plain'
  test_link_args += '-fstack-protector-strong'
endif

if not get_option('buildtype').startswith('debug')
  release_flags += [
    '-DG_DISABLE_CAST_CHECKS',
    '-DG_DISABLE_ASSERT',
  ]
  test_link_args += [
    '-Wl,-Bsymbolic',
    '-fno-plt',
    '-Wl,-z,relro',
    '-Wl,-z,defs',
    '-Wl,-z,now',
  ]
endif

foreach link_arg: test_link_args
  if cc.has_link_argument(link_arg)
    global_link_args += link_arg
  endif
endforeach
add_project_link_arguments(global_link_args, language: 'c')

if cc.links('''
#include <stdatomic.h>
int main(void) {
  atomic_thread_fence(memory_order_acquire);
  atomic_thread_fence(memory_order_seq_cst);
  return 0;
}
''')
  config_h.set10('HAVE_STDATOMIC_H', 1)
endif

subdir('src')
subdir('data')
subdir('examples')
subdir('help')
subdir('po')

configure_file(
          input: 'config.h.meson',
         output: 'config.h',
  configuration: config_h
)

if get_option('enable_gtk')
  meson.add_install_script('build-aux/meson/post_install.sh')
endif
