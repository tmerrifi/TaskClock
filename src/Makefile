
.PHONY : module
SHELL = /bin/bash
CFLAGS = -g -std=gnu99 -fPIC -L/usr/lib -m32 -DDEBUG_CLOCK_CACHE_OFF #-DNO_INSTRUCTION_COUNTING
SOURCES = ./lib/perf_counter.o ./lib/determ_clock.o ./lib/debug_clock_cache.o
TESTS = clock_tests
LIBS = -lrt -lksnap

all : dynlib module

dynlib: $(SOURCES) 
	gcc -shared $(CFLAGS) -o libDetermTaskClock.so $(SOURCES) $(LIBS); \
	sudo cp libDetermTaskClock.so /usr/lib; \


%.o : %.c
	sudo cp lib/*.h /usr/include; \
	gcc $(CFLAGS) -o $@ -c $<;

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