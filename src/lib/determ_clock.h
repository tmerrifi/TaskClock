  

#ifdef __cplusplus
 extern "C" {
#endif

#ifndef DETERM_CLOCK_H
#define DETERM_CLOCK_H

#include <sys/types.h>
     
#include <perf_counter.h>

#include <debug_clock_cache.h>     

#define DETERM_CLOCK_MAX_THREADS 1024

#define DETERM_EVENT_DEBUGGING_SIZE 100000

   //used by userspace to know what is going on                                                                         
   struct task_clock_user_status{
       u_int64_t lowest_clock; //set when you inactivate the clock      
       u_int64_t ticks;
       u_int64_t notifying_clock;
       u_int64_t notifying_id;
       u_int64_t notifying_sample;
       u_int64_t notifying_diff;
       u_int8_t single_active_thread;
   } __attribute__ ((aligned (8), packed));

   struct determ_task_clock_info{
       u_int32_t tid;
       struct perf_counter_info perf_counter;
       struct task_clock_user_status * user_status;
       u_int8_t disabled;
       struct debug_clock_cache debug_clock_cache; //cache of clock values to use (for debugging)
   };

   struct determ_clock_info{
       /*volatile u_int64_t low_ticks;
         volatile u_int64_t low_tid;
         struct determ_task_clock clocks[DETERM_CLOCK_MAX_THREADS];*/
       struct perf_counter_info * leader_perf_counter;
       u_int64_t id_counter;
       char clock_file_name[200];
       u_int64_t current_event_count;
       struct task_clock_user_status user_status[DETERM_CLOCK_MAX_THREADS];
       u_int64_t event_debugging[100000]; //up to these many events
       u_int64_t event_tick_debugging[100000];
   };


   //how many instructions to count before generating a sample
#define DETERM_CLOCK_SAMPLE_PERIOD 5000

#define DETERM_CLOCK_NOT_WAITING 0
#define DETERM_CLOCK_LOWEST 1
#define DETERM_CLOCK_WAITING 2

     
     static void determ_clock_init();
     void determ_task_clock_init();
     void determ_task_clock_init_with_id(u_int32_t id);
     u_int64_t determ_task_clock_read();

     u_int64_t determ_debug_notifying_clock_read();
     int determ_debug_notifying_id_read();
     int determ_debug_notifying_sample_read();
     int determ_debug_notifying_diff_read();

     int determ_task_clock_is_lowest_wait();
     int determ_task_clock_is_lowest();
     void determ_task_clock_start();
     void determ_task_clock_stop();
     void determ_task_clock_halt();
     void determ_task_clock_activate();
     void determ_task_clock_activate_other(int32_t id);
     void determ_task_clock_add_ticks(int32_t ticks);
     void determ_task_clock_on_wakeup();
     int determ_task_clock_single_active_thread();
     void determ_task_clock_clear_single_active_thread();
     void determ_debugging_print_event();
     int determ_debugging_is_disabled();
     u_int32_t determ_task_get_id();
     struct determ_task_clock_info determ_task_clock_get_info();
#endif

#ifdef __cplusplus
 }
#endif
