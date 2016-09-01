# libcubescript

Libcubescript is an embeddable implementation of the Cubescript scripting
language. Cubescript is the console/config language of the Cube engines/games
(and derived engines/games). It's a simplistic language defined around the
idea of everything being a string, with Lisp-like syntax (allowing various
control structures to be defined as commands).

Libcubescript is originally based on the implementation from the Cube 2 engine,
but it's largely rewritten. Here are some of the benefits over the original
implementation:

* Independent implementation that can be embedded in any project
* No global state - multiple Cubescripts can be present within a single program
* Modern C++14 API (no macro mess like in the original)
* C++14 lambdas can be used as commands (including captures and type inference)
* Clean codebase that is easy to read and contribute to
* Core types can be changed as needed at compile time (larger floats? no problem)
* Allows building into a static or shared library, supports `-fvisibility=hidden`

Upcoming features:

* Thread safety (safely call into a single Cubescript state from multiple threads)
* Custom allocator support (control over how heap memory is allocated)

The API is currently unstable and a work in progress. The codebase itself is
also changing very quickly.

The only dependency is OctaSTD:

https://git.octaforge.org/tools/octastd.git/  
https://github.com/OctaForge/OctaSTD

If OctaSTD can work on your system, so can libcubescript.

The supplied Makefile builds a static library on Unix-like OSes. Link this
library together with your application and everything should just work. It also
builds the REPL.

The project also bundles the linenoise line editing library which has been modified
to compile cleanly as C++ (with the same flags as libcubescript). It's used strictly
for the REPL only (you don't need it to build libcubescript itself). The version
in the repository tracks Git revision c894b9e59f02203dbe4e2be657572cf88c4230c3.

See COPYING.md for licensing information.
