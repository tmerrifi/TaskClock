
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <asm-generic/mman.h>
#include <sys/syscall.h>
#include <limits.h>
#include <semaphore.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sched.h>

#include "determ_clock.h"
#include "perf_counter.h"
#include "perf_event.h"

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
  syscall(__TASK_CLOCK_SYS_CALL, fd, (unsigned long)address, tid);
  printf("came back from the sys call %d cpu %d\n", getpid(), sched_getcpu());
}

//sick of writing this boiler plate mmap code! :( Oh well!
void * __create_shared_mem(char * file_name){
  int fd;
  void * mem;
  u_int64_t segment_size=sizeof(struct determ_clock_info);

  sprintf(file_name, "TASK_CLOCK_XXXXXX");
  if ((fd = mkstemp(file_name))==-1){
    perror("couldn't open the shared mem file from determ_clock.c");
    exit(1);
  }
  ftruncate(fd, segment_size); 
  if ((mem = mmap(NULL,segment_size,PROT_READ | PROT_WRITE,MAP_SHARED,fd,0))==NULL){
    perror("mmap failed in determ_clock.c");
    exit(1);
  }
  //lock it in to ram since we can't have page faults in NMI context in the kernel
  mlock(mem, segment_size);
  return mem;
}

void * __open_shared_mem(){
  int fd;
  void * mem;
  fd = open(clock_info->clock_file_name, O_RDWR, 0644);
  if (fd==-1){
    perror("couldn't open the shared mem file");
    exit(1);
  }
  if ((mem = mmap(NULL,sizeof(struct determ_clock_info),PROT_READ | PROT_WRITE,MAP_SHARED | MAP_POPULATE,fd,0))==NULL){
    perror("mmap failed in determ_clock.c");
    exit(1);
  }
  return mem;
}

__attribute__((constructor)) static void determ_clock_init(){
  //TODO: this needs to be shared memory..
  char file_name[200];
  clock_info = __create_shared_mem(file_name);
  //zero out the memory
  strcpy(clock_info->clock_file_name, file_name);
  //initialize the first clock now
  printf("what about here?\n");
  determ_task_clock_init();
  clock_info->leader_perf_counter=task_clock_info.perf_counter;
  printf("INITIALIZED\n");
}

//initialize the clock structure and call the perf object to actually set up the
//instruction counting
void determ_task_clock_init(){

  task_clock_info.tid=__sync_fetch_and_add ( &(clock_info->id_counter), 1 );
  //printf("HERE 1 ??????? tid %d pid %d\n", task_clock_info.tid, getpid());
  if (task_clock_info.tid!=0){
    clock_info=__open_shared_mem();
  }
  task_clock_info.user_status = malloc(sizeof(struct task_clock_user_status));
  memset(task_clock_info.user_status, 0, sizeof(struct task_clock_user_status));
  //printf("HERE 2 ??????? tid %d pid %d cpu %d\n", task_clock_info.tid, getpid(), sched_getcpu());
  //set up the task clock for our process
  __make_clock_sys_call(task_clock_info.user_status, task_clock_info.tid, 0);
  //set up the performance counter

  task_clock_info.perf_counter = perf_counter_init(DETERM_CLOCK_SAMPLE_PERIOD, (task_clock_info.tid==0) ? -1 : task_clock_info.perf_counter->fd );
}



u_int64_t determ_task_clock_read(){
  perf_counter_stop(task_clock_info.perf_counter);
  return 0;
}

void determ_task_clock_is_lowest_wait(){
  //stop the counter
  perf_counter_stop(task_clock_info.perf_counter);
  //are we the lowest? If we are, no reason to wait around
  if (!task_clock_info.user_status->lowest_clock){
    //poll on the fd of the perf_event
    struct pollfd * fds = malloc(sizeof(struct pollfd));
    memset(fds, 0, sizeof(struct pollfd));
    fds->fd = task_clock_info.perf_counter->fd;
    fds->events = POLLIN;
    poll(fds, 1, -1);
    printf("WOKE UP!!!!\n");
  }

  task_clock_info.user_status->lowest_clock=0;
  return;
}

void determ_task_clock_activate(){
  if ( ioctl(task_clock_info.perf_counter->fd, PERF_EVENT_IOC_TASK_CLOCK_ACTIVATE, 0) != 0){
    printf("\nClock start failed\n");
    exit(EXIT_FAILURE);
  }
}

void determ_task_clock_start(){
  perf_counter_start(task_clock_info.perf_counter);
}

//lock is held when we enter
void determ_task_clock_halt(){
  perf_counter_stop(task_clock_info.perf_counter);
  if ( ioctl(task_clock_info.perf_counter->fd, PERF_EVENT_IOC_TASK_CLOCK_HALT, 0) != 0){
    printf("\nHalt failed\n");
    exit(EXIT_FAILURE);
  }
}
