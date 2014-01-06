obj-m = task_clock.o
task_clock-y = clock_main.o

KBUILD_CFLAGS += -g -O3 #-DDEBUG_TASK_CLOCK_FINE_GRAINED -DDEBUG_TASK_CLOCK_COARSE_GRAINED -DNO_INSTRUCTION_COUNTING

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
