project('sysprof', ['c', 'cpp'],
          license: ['GPL3+', 'GPL2+'],
          version: '45.alpha',
    meson_version: '>=0.59.0',
  default_options: [ 'c_std=gnu17',
                     'cpp_std=gnu++17',
                     'warning_level=2',
  ]
)

symbolic_version = '45.0'

gnome = import('gnome', required: get_option('gtk'))
pkgconfig = import('pkgconfig')
i18n = import('i18n')

if get_option('development')
  app_id = 'org.gnome.Sysprof.Devel'
else
  app_id = 'org.gnome.Sysprof'
endif

# All libraries share an ABI revision as they are expected
# to be updated as a set. However, we keep libsysprof-capture
# at an older version since it's used as a static library
# from various platform tooling
soname_major_version = 6
libsysprof_capture_api_version = 4
libsysprof_api_version = soname_major_version

version_split = meson.project_version().split('.')
datadir = get_option('datadir')
datadir_for_pc_file = join_paths('${prefix}', datadir)
podir = join_paths(meson.current_source_dir(), 'po')

# Predetermine some features based on meson_options.txt
need_gtk = get_option('gtk')
need_glib = (need_gtk or
             get_option('examples') or
             get_option('libsysprof') or
             get_option('sysprofd') != 'none' or
             get_option('agent'))

# Determine what libraries to build
need_libsysprof_capture = true
need_libsysprof_profile = get_option('libsysprof') or get_option('agent')
need_libsysprof_analyze = get_option('libsysprof') or get_option('agent')

dex_req = '0.3'
glib_req = '2.76.0'
gtk_req = '4.10'
polkit_req = '0.105'

dex_req_version = '>= @0@'.format(dex_req)
glib_req_version = '>= @0@'.format(glib_req)
gtk_req_version = '>= @0@'.format(gtk_req)
polkit_req_version = '>= @0@'.format(polkit_req)

cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')

glib_dep = dependency('glib-2.0', version: glib_req_version, required: need_glib)
gtk_dep = dependency('gtk4', version: gtk_req_version, required: need_gtk)
libsystemd_dep = dependency('libsystemd', required: false)

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
config_h.set('APP_ID', app_id)
config_h.set_quoted('APP_ID_S', app_id)

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

config_h.set('HAVE_EXECINFO_H', cc.has_header('execinfo.h'))
config_h.set('HAVE_REALLOCARRAY', cc.has_function('reallocarray'))
config_h.set('HAVE_STRLCPY', cc.has_function('strlcpy'))
config_h.set('LOCALEDIR', 'PACKAGE_LOCALE_DIR')
config_h.set10('ENABLE_NLS', true)
config_h.set_quoted('GETTEXT_PACKAGE', 'sysprof')
config_h.set_quoted('PACKAGE_LOCALE_DIR', join_paths(get_option('prefix'), get_option('datadir'), 'locale'))
config_h.set10('HAVE_LIBSYSTEMD', libsystemd_dep.found())

polkit_agent_dep = dependency('polkit-agent-1', required: false)
config_h.set10('HAVE_POLKIT_AGENT', polkit_agent_dep.found())

polkit_dep = dependency('polkit-gobject-1', version: polkit_req_version, required: false)
config_h.set10('HAVE_POLKIT', polkit_dep.found())

if get_option('libunwind')
  # Force libunwind usage if it's specified to avoid back compiles
  # and backtrace() showing up in builds
  libunwind_dep = dependency('libunwind-generic', required: true)
  config_h.set('ENABLE_LIBUNWIND', libunwind_dep.found())
  config_h.set('HAVE_UNW_SET_CACHE_SIZE',
               (libunwind_dep.found() and
                 cc.has_header_symbol('libunwind.h',
                                      'unw_set_cache_size',
                                      dependencies: [libunwind_dep])))
endif

# Development build setup
if get_option('development')
  config_h.set10('DEVELOPMENT_BUILD', true)
endif

has_use_clockid = cc.has_member('struct perf_event_attr',
                                'use_clockid',
                                prefix: '#include <linux/perf_event.h>')
has_clockid = cc.has_member('struct perf_event_attr',
                            'clockid', prefix:
                            '#include <linux/perf_event.h>')
config_h.set('HAVE_PERF_CLOCKID', has_use_clockid and has_clockid)

# For config.h
add_project_arguments(['-I'+meson.current_build_dir()], language: 'c')

global_c_args = [
  '-DSYSPROF_COMPILATION',
  '-D_GNU_SOURCE',
  '-D_POSIX_C_SOURCE=200809L',
]

# Enforce GLib symbol access by required version
if need_glib
  glib_major = glib_req.split('.')[0].to_int()
  glib_minor = glib_req.split('.')[1].to_int()
  if glib_minor % 2 == 1
    glib_minor = glib_minor + 1
  endif
  global_c_args += [
    '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_@0@_@1@'.format(glib_major, glib_minor),
    '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_@0@_@1@'.format(glib_major, glib_minor),
  ]
endif

# Enforce GTK symbol access by required version
if need_gtk
  gtk_major = gtk_req.split('.')[0].to_int()
  gtk_minor = gtk_req.split('.')[1].to_int()
  if gtk_minor % 2 == 1
    gtk_minor = gtk_minor + 1
  endif
  global_c_args += [
    '-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_@0@_@1@'.format(gtk_major, gtk_minor),
    '-DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_@0@_@1@'.format(gtk_major, gtk_minor),
  ]
endif

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
  ['-Werror=format-security', '-Werror=format=2'],
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
  config_h.set10('HAVE_STDATOMIC_H', true)
endif

install_service_files = get_option('sysprofd') == 'bundled'

if need_glib
  subdir('contrib')
endif

subdir('src')
subdir('data')
subdir('po')

if get_option('help')
  subdir('help')
endif

if get_option('examples')
  subdir('examples')
endif

configure_file(
          input: 'config.h.meson',
         output: 'config.h',
  configuration: config_h
)

if get_option('gtk') and gnome.found()
  gnome.post_install(
      gtk_update_icon_cache: true,
    update_desktop_database: true
  )
endif
