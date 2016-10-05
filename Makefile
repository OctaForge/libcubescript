OSTD_PATH = ../octastd

LIBCS_CXXFLAGS = \
	-std=c++14 -Wall -Wextra -Wshadow -Wold-style-cast -Iinclude -Isrc -g \
	-fvisibility=hidden -I$(OSTD_PATH)

LIBCS_LDFLAGS = -shared

LIBCS_OBJ = \
	src/cubescript.o \
	src/cs_gen.o \
	src/cs_vm.o \
	src/cs_val.o \
	src/cs_util.o \
	src/lib_str.o \
	src/lib_math.o \
	src/lib_list.o

LIBCS_LIB = libcubescript.a

.cc.o:
	$(CXX) $(CXXFLAGS) $(LIBCS_CXXFLAGS) -c -o $@ $<

all: library repl

library: $(LIBCS_LIB)

$(LIBCS_LIB): $(LIBCS_OBJ)
	ar rcs $(LIBCS_LIB) $(LIBCS_OBJ)

repl: $(LIBCS_LIB) tools/repl.cc tools/linenoise.cc tools/linenoise.hh
	$(CXX) $(CXXFLAGS) $(LIBCS_CXXFLAGS) $(LDFLAGS) \
	-DCS_REPL_USE_LINENOISE tools/linenoise.cc tools/repl.cc -o repl $(LIBCS_LIB)

clean:
	rm -f $(LIBCS_LIB) $(LIBCS_OBJ) repl

src/cubescript.o: include/cubescript/cubescript.hh include/cubescript/cubescript_conf.hh src/cs_vm.hh
src/cs_gen.o: include/cubescript/cubescript.hh include/cubescript/cubescript_conf.hh src/cs_vm.hh src/cs_util.hh
src/cs_vm.o: include/cubescript/cubescript.hh include/cubescript/cubescript_conf.hh src/cs_vm.hh src/cs_util.hh
src/cs_val.o: include/cubescript/cubescript.hh include/cubescript/cubescript_conf.hh src/cs_vm.hh src/cs_util.hh
src/cs_util.o: include/cubescript/cubescript.hh include/cubescript/cubescript_conf.hh src/cs_util.hh
src/lib_str.o: include/cubescript/cubescript.hh include/cubescript/cubescript_conf.hh
src/lib_math.o: include/cubescript/cubescript.hh include/cubescript/cubescript_conf.hh
src/lib_list.o: include/cubescript/cubescript.hh include/cubescript/cubescript_conf.hh src/cs_util.hh
