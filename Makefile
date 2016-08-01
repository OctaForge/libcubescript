OSTD_PATH = ../octastd

LIBCS_CXXFLAGS = \
	-std=c++14 -Wall -Wextra -Wshadow -Wold-style-cast -I. \
	-fPIC -fvisibility=hidden \
	-I$(OSTD_PATH)

LIBCS_LDFLAGS = -shared

LIBCS_OBJ = \
	cubescript.o

LIBCS_LIB = libcubescript.so

.cc.o:
	$(CXX) $(CXXFLAGS) $(LIBCS_CXXFLAGS) -c -o $@ $<

all: library

library: $(LIBCS_LIB)

$(LIBCS_LIB): $(LIBCS_OBJ)
	$(CXX) $(CXXFLAGS) $(LIBCS_CXXFLAGS) \
	$(LDFLAGS) $(LIBCS_LDFLAGS) -o $@ $(LIBCS_OBJ)

clean:
	rm -f $(LIBCS_LIB) $(LIBCS_OBJ)

cubescript.o: cubescript.hh