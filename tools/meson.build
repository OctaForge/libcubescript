repl_src = [
    'repl.cc'
]

repl_deps = [libostd_dep, libcubescript]
repl_flags = []

if get_option('readline')
    repl_deps += [dependency('readline')]
    repl_flags = ['-DCS_REPL_USE_READLINE']
elif get_option('linenoise')
    repl_src += ['linenoise.cc']
    repl_flags = ['-DCS_REPL_USE_LINENOISE']
endif

executable('cubescript',
    repl_src,
    dependencies: repl_deps,
    include_directories: libcubescript_includes + [include_directories('.')],
    cpp_args: extra_cxxflags + repl_flags,
    install: true
)
