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
	-DCS_REPL_USE_LINENOISE -DCS_REPL_HAS_HINTS -DCS_REPL_HAS_COMPLETE \
	tools/linenoise.cc tools/repl.cc -o repl $(LIBCS_LIB)

clean:
	rm -f $(LIBCS_LIB) $(LIBCS_OBJ) repl

cubescript.o: cubescript.hh cubescript_conf.hh cs_vm.hh
cs_gen.o: cubescript.hh cubescript_conf.hh cs_vm.hh cs_util.hh
cs_vm.o: cubescript.hh cubescript_conf.hh cs_vm.hh cs_util.hh
cs_val.o: cubescript.hh cubescript_conf.hh cs_vm.hh cs_util.hh
lib_str.o: cubescript.hh cubescript_conf.hh
lib_math.o: cubescript.hh cubescript_conf.hh
lib_list.o: cubescript.hh cubescript_conf.hh cs_util.hh
