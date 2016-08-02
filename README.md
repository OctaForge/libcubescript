# libcubescript

This is an embeddable version of the CubeScript implementation from the
Cube 2 engine. The API is highly unstable right now and overall it's a work
in progress.

It depends on the latest Git version of OctaSTD:

https://git.octaforge.org/tools/octastd.git/
https://github.com/OctaForge/OctaSTD

Currently the API is unstable and the whole thing is a work in progress. It
requires C++14, just like OctaSTD does.

The supplied Makefile builds a static library on Unix-like OSes. Link this library
together with your application and everything should just work.

See COPYING.md for licensing information.
