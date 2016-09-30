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
* Modern C++14 API (no macros, use of strongly typed enums, lambdas, ranges etc.)
* C++14 lambdas can be used as commands (including captures and type inference)
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

*(I've just begun working on this, so many things do not apply yet)*

Libcubescript aims to provide a degree of thread safety by introducing a concept
of threads itself. A `CsState` essentially represents a thread - it contains a
pointer to CubeScript global state plus any sort of local state and a call/alias
stack. The main thread (i.e. the state created without any arguments) also owns
the globals; child states (threads) merely point to them.

Thus, by creating a child state (thread) you get access to all globals (which
is thread safe in the implementation) but you also get your own call/alias stack,
error buffer and other things that would otherwise be unsafe to access. Thus,
if you need to call into a single CubeScript from multiple threads, you simply
create a main state within your main program thread and a child state per each
spawned thread you want to use CubeScript from. Since they're isolated, there
is no problem - and libcubescript can remain almost entirely lockless.

Coroutines are a related concept in this case. We will reuse CubeScript threads
for them - merely extending them with a way to save the current execution state
and restore it later. The language itself (or rather, its standard library) will
be extended with new commands to resume and yield coroutines, as well as the
appropriate type extensions.

## Building and usage

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
in the repository tracks Git revision https://github.com/antirez/linenoise/commit/c894b9e59f02203dbe4e2be657572cf88c4230c3.

## Licensing

See COPYING.md for licensing information.
