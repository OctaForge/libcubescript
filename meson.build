project('libcubescript', ['cpp'],
    version: '0.1.0',
    default_options: ['buildtype=debugoptimized', 'cpp_std=c++2a'],
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

subdir('src')
subdir('tools')

pkg = import('pkgconfig')

pkg.generate(
    libraries: libcubescript_target,
    version: meson.project_version(),
    name: 'libcubescript',
    filebase: 'libcubescript',
    url: 'https://git.octaforge.org/octaforge/libcubescript',
    description: 'An embeddable version of the CubeScript language'
)
