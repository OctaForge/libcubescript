# libcubescript 1.0.0 alpha1

[![Build Status](https://github.com/octaforge/libcubescript/actions/workflows/build.yaml/badge.svg)](https://github.com/octaforge/libcubescript/actions)

![CubeScript REPL](https://ftp.octaforge.org/q66/random/libcs_repl.gif)

## Overview

Cubescript is a minimal scripting language first introduced in the Cube FPS
and carried over into derived games and game engines such as Sauerbraten.
Originally being little more than a few hundred lines of code, serving
primarily as the console and configuration file format of the game, it
grew more advanced features as well as a bytecode VM.

Nowadays, it is a minimal but relatively fully featured scripting language
based around the concept that everything can be interpreted as a string.
It excels at its original purpose as well as things like text preprocessing.
It comes with a Lisp-like syntax and a variety of standard library functions.

Libcubescript is a project that aims to provide an independent, improved,
separate implementation of the language, available as a library, intended to
satisfy the needs of the OctaForge project. It was originally forked from
Cubescript as present in the Tesseract game/engine and gradually rewritten;
right now, very little of the original code remains. At language level it is
mostly compatible with the other implementations (although with a stricter
parser and extra features), while the standard library does not aim to be
fully compatible. Some features are also left up to the user to customize,
so that it is not tied to game engines feature-wise.

Like the codebase it is derived from, it is available under the permissive
zlib license, and therefore compatible with just about anything.

## Benefits and differences

There's a variety of things that set this implementation apart:

* It's independent and can be embedded in any project
* There is no global state, so you can have as many Cubescripts as you want,
  in one program
* Written in C++20, following modern language conventions, both internally
  and at API level
* That means the ability to use lambdas as commands, including captures,
  type inference and so on
* There is a robust allocator system in place, and all memory the library
  uses is allocated through it; that gives you complete control over its
  memory (for tracking, sandboxing, limits, etc.)
* A large degree of memory safety, with no manual management
* Thread-safe by default
* Strings are interned, with a single reference counted instance of any
  string existing at a time, which lowers memory usage and simplifies its
  management
* Minimal stack memory usage, which means no artificial limits on recursion
  depth as well as safe usage from threads and coroutines with small stacks
* Errors will no longer cause the interpreter to march on, instead acting
  like real errors
* Protected calls allow you to catch errors in a similar way to exceptions,
  and nearly every error can be caught
* Stricter parsing, with things like unfinished strings being caught
* Loops now have `break` and `continue` statements
* Customizable integer and floating point types
* Full support for symbol visibility in API
* Highly portable and cross-platform, no dependencies other than a compiler
* Clean codebase that is easy to pick up and contribute to

More features and enhancements are planned, such as:

* Improved support for debugging information (line information tracking
  at runtime rather than just compile-time)

Right now, the codebase is unstable, but quickly approaching production
readiness. You are encouraged to test things and report bugs; contributions
of any kind are also welcome (you can use pull requests in our Gitea instance
as well as the GitHub mirror).

Our primary means of communication is the `#octaforge` IRC channel on OFTC.

### Threads

The API provides a concept of threads. The first created thread is the main
thread, which owns all variables and most state. Based on the main thread
you can create side threads, which share a lot of state with the main thread
but have their own call stack.

These threads are not thread-safe by themselves, but as long as you ensure
that concurrent access to them is protected, libcubescript is thread-safe,
i.e. any internal shared state between libcubescript threads is synchronized.
That means you can simply create several libcubescript threads, use them from
different OS threads, and you won't run into any trouble.

Additionally, this means libcubescript threads are coroutine-safe; if you
call into one from a coroutine and yield somewhere mid-command, you can still
access and run code on other libcubescript threads completely safely. Once
you resume the coroutine, it will continue where it left off (though global
variables may have changed).

Since strings are interned and reference counted, string references returned
by libcubescript are also safe, as you get your own reference and nothing
can touch it. Things taking strings will have their own references, and
the code will ensure that strings live for as long as a reference exists
somewhere. Thanks to C++'s scoped value handling, this is mostly seamless
for the user.

## Building and usage

The library has absolutely no dependencies other than a C++20 compiler,
similarly there are no dependencies on system or architecture specific
things, so it should work on any OS and any CPU.

The C++20 support does not have to be complete. These are the baselines
(which are ensured by the CI):

* GCC 10
* Clang 10 (with libstdc++ or libc++)
* Microsoft Visual C++ 2019

Older compilers generally do not work out of box (but for example, GCC 9
may work if you provide an `std::span` implementation; see the docs for
how, but keep in mind that the resulting library will have incompatible
ABI with newer standard library versions that do provide it).

You will need [Meson](https://mesonbuild.com/) to build the project. Most
Unix-like systems have it in their package management, on Windows there is
an installer available on their website. Being written in Python, you can
also use `pip` to get an up to date version on any OS.

Once you have it, compiling is simple, e.g. on Unix-likes you can do:

~~~
mkdir build && cd build
meson ..
ninja all
~~~

Refer to Meson's manual for how to customize whether you want a shared or
static library and so on. By default, you will get a shared library plus
a REPL (interactive interpreter). The REPL also serves as an example of
how to use the API.

If you don't want the REPL, use `-Drepl=disabled`. When compiled, it can
have support for line editing and command history. This is provided through
`linenoise` (which is a minimal single-file line editing library bundled
with the project, and is the default). In case you're on a platform that
`linenoise` does not support (highly unlikely), there is a fallback without
any line editing as well. Pass `-Dlinenoise=disabled` to use the fallback.

The version of `linenoise` bundled with the project is `cpp-linenoise`, available
at https://github.com/yhirose/cpp-linenoise. Our version is modified, so that
it builds cleanly with our flags, and so that it supports the "hints" feature
available in original `linenoise`. Other than the modifications, it is baseed
on upstream git revision `a927043cdd5bfe203560802e56a7e7ed43156ed3`. The reason
we use this instead of upstream `linenoise` is Windows support.
