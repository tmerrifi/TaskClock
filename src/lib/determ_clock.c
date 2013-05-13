
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "determ_clock.h"
#include "perf_counter.h"

struct determ_clock_info * clock_info;

#if __x86_64__
/* 64-bit */
#define __TASK_CLOCK_SYS_CALL 304
#else
#define __TASK_CLOCK_SYS_CALL 342
#endif

//making a simple system call to let the kernel know where the tick array is located
void __make_clock_sys_call(void * address, u_int64_t tid, u_int64_t fd){
  syscall(__TASK_CLOCK_SYS_CALL, fd, (unsigned long)address, tid);
}

//sick of writing this boiler plate mmap code! :( Oh well!
void * __create_shared_mem(){
  int fd;
  void * mem;
  char * file_path = "TASK_CLOCK_XXXXXX";
  if ((fd = mkstemp(file_path))==-1){
    perror("couldn't open the shared mem file from determ_clock.c");
    exit(1);
  }
  if ((mem = mmap(NULL,DETERM_CLOCK_MAX_THREADS * sizeof(struct determ_task_clock_info),PROT_READ,MAP_SHARED,fd,0))==NULL){
    perror("mmap failed in determ_clock.c");
    exit(1);
  }
  return mem;
}

__attribute__((constructor)) static void determ_clock_init(){
  //TODO: this needs to be shared memory..
  clock_info = __create_shared_mem();
  //zero out the memory
  memset(clock_info->clocks, 0, DETERM_CLOCK_MAX_THREADS*sizeof(struct determ_task_clock_info));
  //initialize the first clock now
  determ_task_clock_init(0);
  //now make a system call to open the task_clock in the kernel
  __make_clock_sys_call((unsigned long)clock_info->clocks, 0, clock_info->clocks[0].perf_counter->fd);
  printf("INITIALIZED\n");
}

//initialize the clock structure and call the perf object to actually set up the
//instruction counting
void determ_task_clock_init(u_int32_t tid){
  struct determ_task_clock_info * clock = clock_info[tid];
  clock_info->clocks[tid].tid=tid;
  clock_info->clocks[tid].ticks=0;
  clock_info->clocks[tid].perf_counter = perf_counter_init(DETERM_CLOCK_SAMPLE_PERIOD);
  return clock;
}

void determ_task_clock_start(u_int32_t tid){
  perf_counter_start(clock_info[tid].perf_counter);
}

void determ_task_clock_read(u_int32_t tid){
  return clock_info->clocks[tid].ticks;
}
