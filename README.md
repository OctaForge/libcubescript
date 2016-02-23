# libcubescript

This is an embeddable version of the CubeScript implementation from the
Cube 2 engine.

It depends on the latest Git version of OctaSTD:

https://git.octaforge.org/tools/octastd.git/
https://github.com/OctaForge/OctaSTD

Currently the API is unstable and the whole thing is a work in progress. It
requires C++14, just like OctaSTD does.

You can compile your application with it like:

    c++ myapp.cc cubescript.cc -o myapp -std=c++14 -Wall -Wextra -I. -Ipath/to/octastd

It also supports building as a shared library.

See COPYING.md for licensing information.
