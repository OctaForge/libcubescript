OSTD_PATH = ../octastd

LIBCS_CXXFLAGS = \
	-std=c++14 -Wall -Wextra -Wshadow -Wold-style-cast -I. \
	-fvisibility=hidden -I$(OSTD_PATH)

LIBCS_LDFLAGS = -shared

LIBCS_OBJ = \
	cubescript.o \
	cs_gen.o \
	cs_vm.o \
	lib_str.o \
	lib_math.o \
	lib_list.o

LIBCS_LIB = libcubescript.a

.cc.o:
	$(CXX) $(CXXFLAGS) $(LIBCS_CXXFLAGS) -c -o $@ $<

all: library

library: $(LIBCS_LIB)

$(LIBCS_LIB): $(LIBCS_OBJ)
	ar rcs $(LIBCS_LIB) $(LIBCS_OBJ)

clean:
	rm -f $(LIBCS_LIB) $(LIBCS_OBJ)

cubescript.o: cubescript.hh cs_vm.hh
cs_gen.o: cubescript.hh cs_vm.hh
cs_vm.o: cubescript.hh cs_vm.hh
lib_str.o: cubescript.hh
lib_math.o: cubescript.hh
lib_list.o: cubescript.hh
