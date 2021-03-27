repl_src = [
    'repl.cc'
]

repl_deps = [libcubescript]
repl_flags = []

if get_option('readline').enabled()
    use_readline = true
    use_linenoise = false
elif get_option('linenoise').enabled()
    use_readline = false
    use_linenoise = true
elif get_option('linenoise').auto()
    use_readline = false
    use_linenoise = (host_machine.system() != 'windows')
endif

if not get_option('repl').disabled()
    if use_readline
        repl_deps += [dependency('readline')]
        repl_flags = ['-DCS_REPL_USE_READLINE']
    elif use_linenoise
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
endif
