
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "determ_clock.h"
#include "perf_counter.h"

struct determ_clock_info * clock_info;

__attribute__((constructor)) static void determ_clock_init(){
  clock_info = malloc(sizeof(struct determ_clock_info));
  memset(clock_info->clocks, 0, DETERM_CLOCK_MAX_THREADS*sizeof(struct determ_task_clock_info));
  
  printf("INITIALIZED\n");
}

//initialize the clock structure and call the perf object to actually set up the
//instruction counting
void determ_task_clock_init(u_int32_t tid){
  struct determ_task_clock_info * clock = clock_info[tid];
  clock_info[tid].tid=tid;
  clock_info[tid].ticks=0;
  clock_info[tid].perf_counter = perf_counter_init(DETERM_CLOCK_SAMPLE_PERIOD);
  return clock;
}

void determ_task_clock_start(u_int32_t tid){
  perf_counter_start(clock_info[tid].perf_counter);
}

void determ_task_clock_read(u_int32_t tid){
  
}
