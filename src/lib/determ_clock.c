
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
#include <ftrace.h>
#include <time.h>

#include "determ_clock.h"
#include "perf_counter.h"
#include "determ_perf_event.h"

struct determ_clock_info * clock_info;
struct determ_task_clock_info task_clock_info;

#if __x86_64__
#define __TASK_CLOCK_SYS_CALL 304
#else
#define __TASK_CLOCK_SYS_CALL 342
#endif

#if __x86_64__
#define __TASK_CLOCK_DO_SYS_CALL 306
#else
#define __TASK_CLOCK_DO_SYS_CALL 344
#endif

#define MAX_CLOCK_VAL ((1ULL << 31) - 1)

#define MAX_SPIN_INT 1000

struct determ_task_clock_info determ_task_clock_get_info(){
    return task_clock_info;
}

struct task_clock_user_status * determ_task_clock_get_userspace_info(){
    return task_clock_info.user_status;
}

//making a simple system call to let the kernel know where the tick array is located
void __make_clock_sys_call(void * address, size_t tid, size_t fd){
  syscall(__TASK_CLOCK_SYS_CALL, fd, (unsigned long)address, tid);
}

void __sys_task_clock_do(uint32_t operation, uint64_t value){
    syscall(__TASK_CLOCK_DO_SYS_CALL, operation, value);
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
  mlock(mem, sizeof(struct determ_clock_info));
  return mem;
}

__attribute__((constructor)) static void determ_clock_init(){
    char file_name[200];
    //creating a chunk of shared memory
    clock_info = __create_shared_mem(file_name);
    //copy the filename that was created in __create_shared_mem
    strcpy(clock_info->clock_file_name, file_name);
    memset(clock_info->event_debugging, 0, DETERM_EVENT_DEBUGGING_SIZE * sizeof(u_int64_t));
    memset(clock_info->event_tick_debugging, 0, DETERM_EVENT_DEBUGGING_SIZE * sizeof(u_int64_t));
    //initialize the first clock now
    determ_task_clock_init();
    clock_info->leader_perf_counter=&task_clock_info.perf_counter;
}

//initialize the clock structure and call the perf object to actually set up the
//instruction counting
void determ_task_clock_init(){
    //atomically increment and set the id
    determ_task_clock_init_with_id(__sync_fetch_and_add(&(clock_info->id_counter), 1 ));
}

void determ_task_clock_init_with_id(u_int32_t id){

    //set the id
    task_clock_info.tid=id;
    //if we're not the first process, we open the memory mapping...the question is why? Since we forked
    //it should already be open. TODO: figure out whether to assume this mapping is already in our
    //address space, or make sure it isn't with madvise
    if (task_clock_info.tid!=0){
        clock_info=__open_shared_mem();
    }
    //allocating the user status which is looked at by the kernel
    task_clock_info.user_status = &clock_info->user_status[task_clock_info.tid]; //malloc(sizeof(struct task_clock_user_status));
    memset(task_clock_info.user_status, 0, sizeof(struct task_clock_user_status));
    task_clock_info.disabled=0;
    //set the last clock value to zero
    task_clock_info.last_clock_value=0ULL;
    task_clock_info.last_raw_perf=0;
    task_clock_info.coarsened_ticks_counter=0ULL;
    task_clock_info.in_coarsened_tx=0;
    tx_estimate_init(&task_clock_info.estimator);
    //set up the task clock for our process
    __make_clock_sys_call(task_clock_info.user_status, task_clock_info.tid, 0);
    //set up the performance counter
    perf_counter_init(DETERM_CLOCK_SAMPLE_PERIOD, (task_clock_info.tid==0) ? -1 : task_clock_info.perf_counter.fd, &task_clock_info.perf_counter);
#if defined(DEBUG_CLOCK_CACHE_PROFILE) || defined(DEBUG_CLOCK_CACHE_ON)
    debug_clock_cache_init(task_clock_info.tid, &task_clock_info.debug_clock_cache);
#endif
    //set the scaling factor
    determ_task_set_scaling_factor(1,0);
}

void determ_task_set_scaling_factor(uint8_t whole, uint8_t fraction){
    task_clock_info.user_status->scaling_whole=whole;
    task_clock_info.user_status->scaling_fraction=fraction;
}


int determ_task_clock_single_active_thread(){
    return task_clock_info.user_status->single_active_thread;
}

void determ_task_clock_clear_single_active_thread(){
    task_clock_info.user_status->single_active_thread=0;
}

u_int64_t determ_debug_notifying_clock_read(){
    return task_clock_info.user_status->notifying_clock;
}

int determ_debug_notifying_id_read(){
    return task_clock_info.user_status->notifying_id;
}

int determ_debug_notifying_sample_read(){
    return task_clock_info.user_status->notifying_sample;
}

int determ_debug_notifying_diff_read(){
    return task_clock_info.user_status->notifying_diff;
}


u_int64_t determ_task_clock_read(){
    return task_clock_info.user_status->ticks + task_clock_info.coarsened_ticks_counter;
}

void determ_task_clock_reset(){
    if ( ioctl(task_clock_info.perf_counter.fd, PERF_EVENT_IOC_TASK_CLOCK_RESET, 0) != 0){
        printf("\nreset failed\n");
        exit(EXIT_FAILURE);
    }
}

void __woke_up(){
    if ( ioctl(task_clock_info.perf_counter.fd, PERF_EVENT_IOC_TASK_CLOCK_WOKE_UP, 0) != 0){
        printf("\nClock wakeup failed\n");
        exit(EXIT_FAILURE);
    }
    task_clock_info.disabled=0;
    task_clock_info.user_status->lowest_clock=0;
}

int determ_debugging_is_disabled(){
    return task_clock_info.disabled;
}

int determ_task_clock_is_lowest(){

    if (!task_clock_info.disabled && !task_clock_info.user_status->lowest_clock){
        if ( ioctl(task_clock_info.perf_counter.fd, PERF_EVENT_IOC_TASK_CLOCK_WAIT, 0) != 0){
            printf("\nClock wait failed\n");
            exit(EXIT_FAILURE);
        }
        task_clock_info.disabled=1;
    }
    
    //we are the lowest clock
    if (task_clock_info.user_status->lowest_clock){
        __woke_up();
        return 1;
    }
    else{
        return 0;
    }
}

//need to call this to make sure we mark ourselves as "waiting"
void determ_task_clock_on_wakeup(){
    task_clock_info.disabled=0;
    task_clock_info.user_status->lowest_clock=0;
}

//we arrive here if we're the lowest clock, else we need to poll and wait
int determ_task_clock_is_lowest_wait(){
    //are we the lowest? If we are, no reason to wait around
    int polled=0;
    struct timespec t1,t2;
    
    polled=task_clock_info.disabled;
    if ( ioctl(task_clock_info.perf_counter.fd, PERF_EVENT_IOC_TASK_CLOCK_WAIT, 0) != 0){
        printf("\nClock wait failed\n");
        exit(EXIT_FAILURE);
    }
    task_clock_info.disabled=1;
    
    clock_gettime(CLOCK_REALTIME, &t1);
    int spin_counter=0;
    while(!task_clock_info.user_status->lowest_clock && spin_counter<MAX_SPIN_INT){
        ++spin_counter;
    }
    clock_gettime(CLOCK_REALTIME, &t2);

    if (!task_clock_info.user_status->lowest_clock){
        if ( ioctl(task_clock_info.perf_counter.fd, PERF_EVENT_IOC_TASK_CLOCK_SLEEP, 0) != 0){
            printf("\nClock wait failed\n");
            exit(EXIT_FAILURE);
        }
        if (!task_clock_info.user_status->lowest_clock){
            //poll on the fd of the perf_event
            struct pollfd fds;
            memset(&fds, 0, sizeof(struct pollfd));
            fds.fd = task_clock_info.perf_counter.fd;
            fds.events = POLLIN;
            poll(&fds, 1, -1);
        }
    }

    if ( ioctl(task_clock_info.perf_counter.fd, PERF_EVENT_IOC_TASK_CLOCK_WOKE_UP, 0) != 0){
        printf("\nClock wakeup failed\n");
        exit(EXIT_FAILURE);
    }

    __woke_up(); 
    return polled;
}

int determ_task_clock_is_active(){
    return task_clock_info.disabled;
}

void determ_task_clock_activate(){
    task_clock_info.user_status->lowest_clock=0;
    task_clock_info.disabled=0;
    if ( ioctl(task_clock_info.perf_counter.fd, PERF_EVENT_IOC_TASK_CLOCK_ACTIVATE, 0) != 0){
        printf("\nClock start failed\n");
        exit(EXIT_FAILURE);
    }
}

void determ_task_clock_add_ticks(int32_t ticks){
    if ( ioctl(task_clock_info.perf_counter.fd, PERF_EVENT_IOC_TASK_CLOCK_ADD_TICKS, ticks) != 0){
        printf("\nClock start failed\n");
        exit(EXIT_FAILURE);
    }
}

//Returns 1 if the thread we activated is now the "lowest"
int determ_task_clock_activate_other(int32_t id){
    int ret=0;
    //the only way this flag could change is when we activate others, which happnes on create and cond_signal
    task_clock_info.user_status->single_active_thread=0;
    if ( ioctl(task_clock_info.perf_counter.fd, PERF_EVENT_IOC_TASK_CLOCK_ACTIVATE_OTHER, id) != 0){
        printf("\nClock start failed\n");
        exit(EXIT_FAILURE);
    }
    
    if (task_clock_info.user_status->activated_lowest){
        //we need to return true
        ret=1;
        //reset the flag
        task_clock_info.user_status->activated_lowest=0;
    }
    return ret;
}

uint64_t __rdpmc(uint32_t reg)
{
    uint64_t val;
    asm("rdpmc" : "=A" (val) : "c" (reg));    
    return val;
}

void __determ_task_clock_start(int start_type){
    uint64_t diff=0;
#ifdef DEBUG_CLOCK_CACHE_PROFILE
    debug_clock_cache_insert(&task_clock_info.debug_clock_cache, determ_task_clock_read(), &diff);
#elif DEBUG_CLOCK_CACHE_ON
    debug_clock_cache_get(&task_clock_info.debug_clock_cache, determ_task_clock_read(), &diff);
#endif
    task_clock_info.disabled=0;
    task_clock_info.user_status->lowest_clock=0;

    if (start_type!=TASK_CLOCK_COARSENED){
        //set the last clock read so we can figure out later the length of the tx in instructions
        task_clock_info.last_clock_value=determ_task_clock_read();
        if (diff>0){
            determ_task_clock_add_ticks(diff);
        }
        
        if (!task_clock_info.perf_counter.started){
            perf_counter_start(&task_clock_info.perf_counter);
        }
        else{
            __sys_task_clock_do(TASK_CLOCK_OP_START, 0);
            /*if ( ioctl(task_clock_info.perf_counter.fd, start_type) != 0){
              printf("\nClock read failed\n");
              exit(EXIT_FAILURE);
              }*/
        }
    }
    else{
        //we are STARTING a coarsened tx (the first one in the block)
        __sys_task_clock_do(TASK_CLOCK_OP_START_COARSENED, 0);
    }
}

uint64_t determ_task_clock_last_raw_perf(){
    return task_clock_info.last_raw_perf;
    //return ((__rdpmc(task_clock_info.user_status->hwc_idx) << 16) >> 16);
}

uint64_t determ_task_clock_period_sets(){
    return task_clock_info.user_status->period_sets;
}

uint64_t determ_task_clock_current_raw_perf(){
    return task_clock_info.current_raw_perf;
    //return ((__rdpmc(task_clock_info.user_status->hwc_idx) << 16) >> 16);
}


uint64_t determ_task_clock_get_coarsened_ticks(){
    return task_clock_info.coarsened_ticks_counter;
}

void determ_task_clock_end_coarsened_tx(){
    if (task_clock_info.coarsened_ticks_counter > 0){
        determ_task_clock_add_ticks(task_clock_info.coarsened_ticks_counter);
    }
    task_clock_info.in_coarsened_tx=0;
    task_clock_info.coarsened_ticks_counter=0;
}

void determ_task_clock_start(){
    __determ_task_clock_start(TASK_CLOCK_NOT_COARSENED);
}

int determ_task_clock_in_coarsened_tx(){
    return task_clock_info.in_coarsened_tx;
}

//Start the clock, but don't use that as an opportunity to notify waiting threads. This is useful if we're
//doing a coarsened transaction and don't care about the others. The reason we even do this is that its 
//faster to avoid that overhead
void determ_task_clock_start_no_notify(){
    if (!task_clock_info.in_coarsened_tx){
        __determ_task_clock_start(TASK_CLOCK_COARSENED);
        //we've now set the counter to be the max allowable size...we can now read the counter in userspace
        task_clock_info.in_coarsened_tx=1;
    }
    //we need to read the raw perf counter for calculation later
    task_clock_info.last_raw_perf = __rdpmc(task_clock_info.user_status->hwc_idx);
}

u_int64_t determ_task_clock_get_last_tx_size(){
    return determ_task_clock_read() - task_clock_info.last_clock_value;
}

void __determ_task_clock_stop_with_id(uint32_t id, int stop_type){
    //read the clock
    /*if ( ioctl(task_clock_info.perf_counter.fd, stop_type) != 0){
        printf("\nClock read failed\n");
        exit(EXIT_FAILURE);
        }*/

    uint64_t tx_size=0;

    if (stop_type==TASK_CLOCK_NOT_COARSENED){
        __sys_task_clock_do(TASK_CLOCK_OP_STOP, 0);
        tx_size = determ_task_clock_get_last_tx_size();
    }
    else{
        uint64_t new_raw = __rdpmc(task_clock_info.user_status->hwc_idx);
        //uint64_t max_raw = (uint64_t)(-((int64_t)MAX_CLOCK_VAL));
        uint64_t shift = 16ULL;
        uint64_t delta = (new_raw << shift) - (task_clock_info.last_raw_perf << shift);
        //shift it back to get the actually correct number
        delta >>= shift;
        //add the delta to the ticks
        task_clock_info.coarsened_ticks_counter+=delta;
        tx_size = delta;
        task_clock_info.current_raw_perf=new_raw;
    }

    tx_estimate_add_observation(&task_clock_info.estimator, id, tx_size);
#if defined(DEBUG_CLOCK_CACHE_PROFILE) || defined(DEBUG_CLOCK_CACHE_ON)
    uint64_t diff=0;
#ifdef DEBUG_CLOCK_CACHE_PROFILE
    debug_clock_cache_insert(&task_clock_info.debug_clock_cache, determ_task_clock_read(), &diff);
#elif DEBUG_CLOCK_CACHE_ON
    debug_clock_cache_get(&task_clock_info.debug_clock_cache, determ_task_clock_read(), &diff);
#endif
    if (diff>0){
        determ_task_clock_add_ticks(diff);
    }
#endif
}

void determ_task_clock_stop(){
    determ_task_clock_stop_with_id(0);
}

void determ_task_clock_stop_no_notify(){
    determ_task_clock_stop_with_id_no_notify(0);
}

void determ_task_clock_stop_with_id(uint32_t id){
    __determ_task_clock_stop_with_id(id, TASK_CLOCK_NOT_COARSENED);
}

//Stop and read the clock...but don't do extra work in the kernel to wake someone up. Minor optimization
//to speed up coarsened transactions
void determ_task_clock_stop_with_id_no_notify(uint32_t id){
    __determ_task_clock_stop_with_id(id, TASK_CLOCK_COARSENED);
}

int64_t determ_task_clock_estimate_next_tx(uint32_t id){
    return tx_estimate_next_observation_guess(&task_clock_info.estimator, id);
}


//Calling halt means that we are no longer considered as "part of the group." We can't have the lowest clock.
void determ_task_clock_halt(){
    perf_counter_stop(&task_clock_info.perf_counter);
    task_clock_info.disabled=1;
    if ( ioctl(task_clock_info.perf_counter.fd, PERF_EVENT_IOC_TASK_CLOCK_HALT, 0) != 0){
        exit(EXIT_FAILURE);
    }

}

u_int32_t determ_task_get_id(){
  return task_clock_info.tid;
}

void determ_debugging_print_event(){}

void determ_task_clock_close(){
    perf_counter_close(&task_clock_info.perf_counter);
}
