OSTD_PATH = ../octastd

LIBCS_CXXFLAGS = \
	-std=c++14 -Wall -Wextra -Wshadow -Wold-style-cast -I. \
	-fvisibility=hidden -I$(OSTD_PATH)

LIBCS_LDFLAGS = -shared

LIBCS_OBJ = \
	cubescript.o \
	cs_gen.o \
	cs_vm.o \
	cs_util.o \
	lib_str.o \
	lib_math.o \
	lib_list.o

LIBCS_LIB = libcubescript.a

.cc.o:
	$(CXX) $(CXXFLAGS) $(LIBCS_CXXFLAGS) -c -o $@ $<

all: library repl

library: $(LIBCS_LIB)

$(LIBCS_LIB): $(LIBCS_OBJ)
	ar rcs $(LIBCS_LIB) $(LIBCS_OBJ)

repl: $(LIBCS_LIB)
	$(CXX) $(CXXFLAGS) $(LIBCS_CXXFLAGS) $(LDFLAGS) repl.cc -o repl $(LIBCS_LIB)

clean:
	rm -f $(LIBCS_LIB) $(LIBCS_OBJ)

cubescript.o: cubescript.hh cubescript_conf.hh cs_vm.hh cs_util.hh
cs_gen.o: cubescript.hh cubescript_conf.hh cs_vm.hh cs_util.hh
cs_vm.o: cubescript.hh cubescript_conf.hh cs_vm.hh cs_util.hh
lib_str.o: cubescript.hh cubescript_conf.hh
lib_math.o: cubescript.hh cubescript_conf.hh
lib_list.o: cubescript.hh cubescript_conf.hh cs_util.hh
