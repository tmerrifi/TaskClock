
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <limits.h>
#include <semaphore.h>

#include "determ_clock.h"
#include "perf_counter.h"

struct determ_clock_info * clock_info;
struct determ_task_clock_info task_clock_info;

#if __x86_64__
/* 64-bit */
#define __TASK_CLOCK_SYS_CALL 304
#else
#define __TASK_CLOCK_SYS_CALL 342
#endif

//making a simple system call to let the kernel know where the tick array is located
void __make_clock_sys_call(void * address, u_int64_t tid, u_int64_t fd){
  printf("addr is %p\n", address);
  syscall(__TASK_CLOCK_SYS_CALL, fd, (unsigned long)address, tid);
}

//sick of writing this boiler plate mmap code! :( Oh well!
void * __create_shared_mem(){
  int fd;
  void * mem;
  char file_path[200];
  u_int64_t segment_size=sizeof(struct determ_clock_info);

  sprintf(file_path, "TASK_CLOCK_XXXXXX");
  if ((fd = mkstemp(file_path))==-1){
    perror("couldn't open the shared mem file from determ_clock.c");
    exit(1);
  }
  ftruncate(fd, segment_size); 
  if ((mem = mmap(NULL,segment_size,PROT_READ | PROT_WRITE,MAP_SHARED,fd,0))==NULL){
    perror("mmap failed in determ_clock.c");
    exit(1);
  }
  return mem;
}

__attribute__((constructor)) static void determ_clock_init(){
  //TODO: this needs to be shared memory..
  clock_info = __create_shared_mem();
  //zero out the memory
  memset(clock_info->clocks, 0, sizeof(struct determ_clock_info));
  //initialize the first clock now
  determ_task_clock_init(0);
  //now make a system call to open the task_clock in the kernel
  __make_clock_sys_call(clock_info->clocks, 0, task_clock_info.perf_counter->fd);
  clock_info->leader_perf_counter=task_clock_info.perf_counter;
  printf("INITIALIZED\n");
}

//initialize the clock structure and call the perf object to actually set up the
//instruction counting
void determ_task_clock_init(u_int32_t tid){
  task_clock_info.tid=tid;
  printf("initing.....%d\n", (tid==0) ? -1 : task_clock_info.perf_counter->fd);
  task_clock_info.perf_counter = perf_counter_init(DETERM_CLOCK_SAMPLE_PERIOD, (tid==0) ? -1 : task_clock_info.perf_counter->fd );
}

void determ_task_clock_start(u_int32_t tid){
  perf_counter_start(task_clock_info.perf_counter);
}

u_int64_t determ_task_clock_read(u_int32_t tid){
  perf_counter_stop(task_clock_info.perf_counter);
  return clock_info->clocks[tid].ticks;
}

//this will determine if we are the lowest clock in the system
u_int32_t __determ_task_clock_get_lowest(u_int32_t tid, u_int64_t * min_ticks_out){
  //ok, now its stopped so lets walk the array
  u_int32_t min_tid=0;
  u_int64_t min_ticks=ULLONG_MAX;
  for(int i=0;i<DETERM_CLOCK_MAX_THREADS;++i){
    if (clock_info->clocks[i].ticks < min_ticks){
      min_ticks=clock_info->clocks[i].ticks;
      min_tid=i;
    }
  }
  *min_ticks_out=min_ticks;
  //we also check to make sure that our is_lowest flag is set (from kernel space)
  return min_tid;
}

void determ_task_clock_is_lowest_wait(u_int32_t tid){
  perf_counter_stop(task_clock_info.perf_counter);
  return;
}

//lock is held when we enter
void determ_task_clock_remove(u_int32_t tid){
  perf_counter_stop(task_clock_info.perf_counter);
  
}
