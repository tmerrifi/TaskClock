
.PHONY : module
SHELL = /bin/bash
CFLAGS = -g -std=c99 -fPIC -L/usr/lib -m32
SOURCES = ./lib/perf_counter.o ./lib/determ_clock.o
TESTS = clock_tests
LIBS = -lrt -lksnap

all : dynlib module

dynlib: $(SOURCES) 
	sudo cp lib/*.h /usr/include; \
	gcc -shared $(CFLAGS) -o libDetermTaskClock.so $(SOURCES) $(LIBS); \
	sudo cp libDetermTaskClock.so /usr/lib; \


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