obj-m = task_clock.o
task_clock-y = clock_main.o

KBUILD_CFLAGS += -g -O3

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
