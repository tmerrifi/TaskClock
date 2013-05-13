
SHELL = /bin/bash
CFLAGS = -g -std=c99 -fPIC
SOURCES = ./clock/perf_counter.o ./clock/determ_clock.o
TESTS = clock_tests
LIBS = -lrt

dynlib: $(SOURCES) 
	gcc -shared $(CFLAGS) -o libHiDet.so $(SOURCES) $(LIBS); \
	sudo cp libHiDet.so /usr/lib;

%.o : %.c
	gcc $(CFLAGS) -o $@ -c $< $(LIBS);

tests : $(TESTS)

clock_tests : 
	pushd clock/tests; \
	make; \
	make tests; \
	popd;

clean : 
	rm ./clock/*.o; \
	pushd clock/tests; \
	make clean; \
	popd;