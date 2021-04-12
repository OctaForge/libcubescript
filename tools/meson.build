repl_src = [
    'repl.cc'
]

repl_deps = [libcubescript]
repl_flags = []

if not get_option('repl').disabled()
    if not get_option('linenoise').disabled()
        repl_flags = ['-DCS_REPL_USE_LINENOISE']
    endif
    executable('cubescript',
        repl_src,
        dependencies: repl_deps,
        include_directories: libcubescript_includes + [include_directories('.')],
        cpp_args: extra_cxxflags + repl_flags,
        install: true
    )
endif
