project('libcubescript', ['cpp'],
    version: '0.1.0',
    default_options: [
        'buildtype=debugoptimized', 'warning_level=3', 'cpp_rtti=false',
        'cpp_std=none'
    ],
    meson_version: '>=0.50'
)

dir_prefix  = get_option('prefix')
dir_include = join_paths(dir_prefix, get_option('includedir'))
dir_data    = join_paths(dir_prefix, get_option('datadir'))
dir_lib     = join_paths(dir_prefix, get_option('libdir'))

dir_package_include = join_paths(dir_include, 'cubescript')

libcubescript_includes = [include_directories('include')]

cxx = meson.get_compiler('cpp')

extra_cxxflags = []

if get_option('buildtype') != 'plain'
    if cxx.has_argument('-Wshadow')
        extra_cxxflags += '-Wshadow'
    endif
    if cxx.has_argument('-Wold-style-cast')
        extra_cxxflags += '-Wold-style-cast'
    endif
endif

# Meson does not support C++20 std in a portable way in this version
# unless specified explicitly, have our own logic which will guess it
if get_option('cpp_std') == 'none'
    if cxx.has_argument('-std=c++20')
        # modern gcc/clang
        extra_cxxflags += '-std=c++20'
    elif cxx.has_argument('-std=c++2a')
        # older gcc/clang
        extra_cxxflags += '-std=c++2a'
    elif cxx.has_argument('/std:c++20')
        # future msvc++? not supported anywhere yet
        extra_cxxflags += '/std:c++20'
    elif cxx.has_argument('/std:c++latest')
        # msvc++ 2019
        extra_cxxflags += '/std:c++latest'
    endif
endif

build_root = meson.current_build_dir()

subdir('include')
subdir('src')
subdir('tools')

if meson.is_cross_build() and get_option('tests')
    build_tests = get_option('tests_cross')
else
    build_tests = get_option('tests')
endif

if build_tests
    subdir('tests')
endif

pkg = import('pkgconfig')

pkg.generate(
    libraries: libcubescript_target,
    version: meson.project_version(),
    name: 'libcubescript',
    filebase: 'libcubescript',
    url: 'https://git.octaforge.org/octaforge/libcubescript',
    description: 'An embeddable version of the CubeScript language'
)
