obj-m = task_clock.o
task_clock-y = clock_main.o bounded_tso.o

#turn off instruction counting
#-DNO_INSTRUCTION_COUNTING

#use to print a bunch of stuff to the log file. Use with caution though, since
#it can really slow things down
#-DDEBUG_TASK_CLOCK_FINE_GRAINED -DDEBUG_TASK_CLOCK_COARSE_GRAINED

#adjust the period for the instruction counting at runtime
#-DUSE_ADAPTIVE_OVERFLOW_PERIOD

#turns on bounded fence, which can be used to execute programs with ad-hoc sync
#-DUSE_BOUNDED_FENCE

KBUILD_CFLAGS += -g -O3 -DUSE_ADAPTIVE_OVERFLOW_PERIOD -DUSE_BOUNDED_FENCE

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:

	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
