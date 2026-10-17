libeggbitset_sources = [
  'eggbitset.c',
]

libeggbitset_deps = [
  gio_dep,
]

libeggbitset_c_args = []

# roaring.h enables its x64 code paths based on __AVX2__/__SSE4_2__ alone,
# but they rely on intrinsics only available on 64-bit x86.
if host_machine.cpu_family() == 'x86'
  libeggbitset_c_args += ['-DDISABLE_X64']
endif

libeggbitset_static = static_library('eggbitset', libeggbitset_sources,
  gnu_symbol_visibility: 'hidden',
           dependencies: libeggbitset_deps,
                 c_args: libeggbitset_c_args,
)

libeggbitset_static_dep = declare_dependency(
  include_directories: include_directories('.'),
            link_with: libeggbitset_static,
)
