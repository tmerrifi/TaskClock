
ifeq ($(TASKCLOCK_ARCH),32)
	LIB_ARCH = -m32
endif

SHELL = /bin/bash
CFLAGS = -g -std=gnu99 -fPIC -L/usr/lib $(LIB_ARCH) -DDEBUG_CLOCK_CACHE_OFF -DDETERM_CLOCK_SAMPLE_PERIOD=5000 #-DNO_INSTRUCTION_COUNTING
SOURCES = ./lib/perf_counter.o ./lib/determ_clock.o
TESTS = clock_tests
LIBS = -lrt -lksnap

.PHONY : module

all : dynlib module

dynlib: $(SOURCES) 
	gcc -shared $(CFLAGS) -o libDetermTaskClock.so $(SOURCES) $(LIBS); \
	sudo cp libDetermTaskClock.so /usr/lib; \
	echo "10000000" | sudo tee /proc/sys/kernel/perf_event_max_sample_rate;

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
