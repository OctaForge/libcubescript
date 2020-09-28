# libcubescript

![CubeScript REPL](https://ftp.octaforge.org/q66/random/libcs_repl.gif)

## Overview

Libcubescript is an embeddable implementation of the CubeScript scripting
language. CubeScript is the console/config language of the Cube engines/games
(and derived engines/games). It's a simplistic language defined around the
idea of everything being a string, with Lisp-like syntax (allowing various
control structures to be defined as commands).

## Benefits and use cases

CubeScript is suitable for any use that calls for a simple scripting language
that is easy to embed. It's particularly strong at macro processing, so it can
be used as a preprocessor, or for any string-heavy use. Since it has descended
from a console language for a video game, it can still be used for that very
purpose, as well as a configuration file language.

Its thread-friendliness allows for usage in any context that requires parallel
processing and involvement of the scripting system in it.

As far as benefits over the original implementation go, while it is based on
the original implementation, it's largely rewritten; thus, it's gained many
advantages, including:

* Independent implementation (can be embedded in any project)
* No global state (multiple CubeScripts in a single program)
* Modern C++17 API (no macros, use of strongly typed enums, lambdas, ranges etc.)
* C++17 lambdas can be used as commands (including captures and type inference)
* Error handling including recovery (protected call system similar to Lua)
* Stricter parsing (strings cannot be left unfinished etc.)
* Loop control statements (`break` and `continue`)
* No manual memory mangement, values manage themselves
* Clean codebase that is easy to read and contribute to
* Support for arbitrary size integers and floats (can be set at compile time)
* Allows building into a static or shared library, supports `-fvisibility=hidden`

There are some features that are a work in progress and will come later:

* More helpful debug information (proper line infos at both parse and run time)
* A degree of thread safety (see below)
* Custom allocator support (control over how heap memory is allocated)
* Coroutines

The API is currently very unstable, as is the actual codebase. Therefore you
should not use the project in production environments just yet, but you're
also free to experiment - feedback is welcome.

**The project is also open for contributions.** You can use pull requests on
GitHub and there is also a discussion channel `#octaforge` on FreeNode; this
project is a part of the larger OctaForge umbrella.

## Threads and coroutines

*(In progress)*

Libcubescript supports integration with coroutines and threads by providing a
concept of threads itself. You can create a thread (child state) using the
main state and it will share global data with the main state, but it also
has its own call stack.

The "global" state is thread safe, allowing concurrent access from multiple
threads. The "local" state can be yielded as a part of the coroutine without
affecting any other threads.

This functionality is not exposed into the language itself, but it can be
utilized in the outside native code.

## Building and usage

The only dependency is libostd:

https://git.octaforge.org/OctaForge/libostd

https://github.com/OctaForge/libostd

If libostd can work on your system, so can libcubescript.

Libostd is built using Meson. Therefore, you need to install Meson and then
you can compile it as usual. Typically, this will be something like

~~~
mkdir build && cd build
meson ..
meson compile
~~~

Link the libcubescript library together with your application and everything should just work.
It also builds the REPL.

The project also bundles the linenoise line editing library which has been modified
to compile cleanly as C++ (with the same flags as libcubescript). It's used strictly
for the REPL only (you don't need it to build libcubescript itself). The version
in the repository tracks Git revision https://github.com/antirez/linenoise/commit/c894b9e59f02203dbe4e2be657572cf88c4230c3.

## Licensing

See COPYING.md for licensing information.
