libcubescript_header_src = [
    '../include/cubescript/cubescript.hh',
    '../include/cubescript/cubescript_conf.hh',
    '../include/cubescript/cubescript/callable.hh',
    '../include/cubescript/cubescript/error.hh',
    '../include/cubescript/cubescript/ident.hh',
    '../include/cubescript/cubescript/platform.hh',
    '../include/cubescript/cubescript/state.hh',
    '../include/cubescript/cubescript/util.hh',
    '../include/cubescript/cubescript/value.hh',
]

libcubescript_src = [
    'cs_bcode.cc',
    'cs_error.cc',
    'cs_gen.cc',
    'cs_ident.cc',
    'cs_parser.cc',
    'cs_state.cc',
    'cs_std.cc',
    'cs_strman.cc',
    'cs_thread.cc',
    'cs_val.cc',
    'cs_vm.cc',
    'lib_base.cc',
    'lib_list.cc',
    'lib_math.cc',
    'lib_str.cc'
]

lib_cxxflags = extra_cxxflags + [ '-DLIBCUBESCRIPT_BUILD' ]
dyn_cxxflags = lib_cxxflags

lib_incdirs = libcubescript_includes + [include_directories('.')]

host_system = host_machine.system()
os_uses_dlls = (host_system == 'windows' or host_system == 'cygwin')

if os_uses_dlls
    dyn_cxxflags += '-DLIBCUBESCRIPT_DLL'
endif

if os_uses_dlls and get_option('default_library') == 'both'
    # we need a bunch of this nonsense on windows as both_libraries()
    # does not work reliably there; since DLLs work like they do, we
    # need to compile one set of object files with dllexport and the
    # other set without that, and there is no way to tell meson to
    # do that other than making two different targets...
    libcubescript_static = static_library('cubescript',
        libcubescript_src, include_directories: lib_incdirs,
        cpp_args: lib_cxxflags,
        install: true
    )
    libcubescript_dynamic = shared_library('cubescript',
        libcubescript_src, include_directories: lib_incdirs,
        cpp_args: dyn_cxxflags,
        install: true,
        version: meson.project_version()
    )
    libcubescript_target = libcubescript_dynamic
else
    libcubescript_target = library('cubescript',
        libcubescript_src, include_directories: lib_incdirs,
        cpp_args: dyn_cxxflags,
        install: true,
        version: meson.project_version()
    )
endif

libcubescript = declare_dependency(
    include_directories: libcubescript_includes,
    link_with: libcubescript_target
)

install_headers(libcubescript_header_src, install_dir: dir_package_include)
