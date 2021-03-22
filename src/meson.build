libcubescript_header_src = [
    '../include/cubescript/cubescript.hh',
    '../include/cubescript/cubescript_conf.hh'
]

libcubescript_src = [
    'cs_bcode.cc',
    'cs_gen.cc',
    'cs_util.cc',
    'cs_val.cc',
    'cs_vm.cc',
    'cubescript.cc',
    'lib_list.cc',
    'lib_math.cc',
    'lib_str.cc'
]

libcubescript_lib = both_libraries('cubescript',
    libcubescript_src,
    include_directories: libcubescript_includes + [include_directories('.')],
    cpp_args: extra_cxxflags,
    install: true,
    version: meson.project_version()
)

libcubescript = declare_dependency(
    include_directories: libcubescript_includes,
    link_with: libcubescript_lib.get_shared_lib()
)

libcubescript_static = declare_dependency(
    include_directories: libcubescript_includes,
    link_with: libcubescript_lib.get_static_lib()
)

install_headers(libcubescript_header_src, install_dir: dir_package_include)
