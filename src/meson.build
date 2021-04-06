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

lib_cxxflags = [ '-DLIBCUBESCRIPT_BUILD' ]
host_system = host_machine.system()

if host_system == 'windows' or host_system == 'cygwin'
    if get_option('default_library') != 'static'
        lib_cxxflags += '-DLIBCUBESCRIPT_DLL'
    endif
endif

libcubescript_target = library('cubescript',
    libcubescript_src,
    include_directories: libcubescript_includes + [include_directories('.')],
    cpp_args: extra_cxxflags + lib_cxxflags,
    install: true,
    pic: true,
    version: meson.project_version()
)

libcubescript = declare_dependency(
    include_directories: libcubescript_includes,
    link_with: libcubescript_target
)

install_headers(libcubescript_header_src, install_dir: dir_package_include)
