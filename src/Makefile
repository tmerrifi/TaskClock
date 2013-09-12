
.PHONY : module
SHELL = /bin/bash
CFLAGS = -g -std=c99 -fPIC -L/usr/lib
SOURCES = ./lib/perf_counter.o ./lib/determ_clock.o
TESTS = clock_tests
LIBS = -lrt -lksnap

all : dynlib module

dynlib: $(SOURCES) 
	gcc -shared $(CFLAGS) -o libHiDet.so $(SOURCES) $(LIBS); \
	sudo cp libHiDet.so /usr/lib;

%.o : %.c
	gcc $(CFLAGS) -o $@ -c $< $(LIBS);

tests : $(TESTS)

module : 
	pushd module; \
	make clean; \
	make; \
	./install.sh;

clock_tests : 
	pushd lib/tests; \
	make; \
	make tests; \
	popd;

clean : 
	rm ./lib/*.o; \
	pushd lib/tests; \
	make clean; \
	popd;