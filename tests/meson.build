lang_tests = [
    # test_name                               test_file           expected_fail
    ['simple example',                        'simple',                 false],
]

lib_tests = [
]

test_runner = executable('runner',
    ['runner.cc'],
    dependencies: libcubescript,
    include_directories: libcubescript_includes,
    cpp_args: extra_cxxflags,
    install: false
)

penv = environment()
penv.append('PATH', join_paths(build_root, 'src'))

foreach tcase: lang_tests
    test(tcase[0],
        test_runner,
        args: [join_paths(meson.current_source_dir(), tcase[1] + '.cube')],
        should_fail: tcase[2],
        env: penv
    )
endforeach

foreach tcase: lib_tests
    test_exe = executable(tcase[0],
        [tcase[0] + '.cc'],
        dependencies: libcubescript,
        include_directories: libcubescript_includes,
        cpp_args: extra_cxxflags,
        instalL: false
    )
    test(tcase[0], tcase[0], should_fail: tcase[1], env: penv)
endforeach
