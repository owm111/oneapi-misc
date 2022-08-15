CPPFLAGS = -I/opt/intel/oneapi/tbb/latest/include
CXXFLAGS = -O3 -Wall -march=native
LDFLAGS = -L/opt/intel/oneapi/tbb/latest/lib/intel64/gcc4.8
LDLIBS = -ltbb

PROGS = noploop mutexes recursive-fib

all: $(PROGS)

clean:
	$(RM) $(PROGS)

.PHONY: all clean
