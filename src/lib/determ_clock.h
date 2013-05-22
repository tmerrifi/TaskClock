

#ifdef __cplusplus
 extern "C" {
#endif

#ifndef DETERM_CLOCK_H
#define DETERM_CLOCK_H

   #include <sys/types.h>

#define DETERM_CLOCK_MAX_THREADS 1024

   //used by userspace to know what is going on                                                                         
   struct task_clock_user_status{
     u_int8_t lowest_clock; //set when you inactivate the clock                                                          
   };

   struct determ_task_clock_info{
     u_int32_t tid;
     struct perf_counter_info * perf_counter;
     struct task_clock_user_status * user_status;
   };

   struct determ_task_clock{
     volatile u_int64_t ticks;
   } __attribute__ ((aligned (8)));

   struct determ_clock_info{
     /*volatile u_int64_t low_ticks;
     volatile u_int64_t low_tid;
     struct determ_task_clock clocks[DETERM_CLOCK_MAX_THREADS];*/
     struct perf_counter_info * leader_perf_counter;
     u_int64_t id_counter;
     char clock_file_name[200];
   };


   //how many instructions to count before generating a sample
#define DETERM_CLOCK_SAMPLE_PERIOD 10000

#define DETERM_CLOCK_NOT_WAITING 0
#define DETERM_CLOCK_LOWEST 1
#define DETERM_CLOCK_WAITING 2


   static void determ_clock_init();
   void determ_task_clock_init();
   u_int64_t determ_task_clock_read();
   u_int8_t determ_task_clock_is_lowest();
   void determ_task_clock_start();
   void determ_task_clock_halt();
   void determ_task_clock_activate();

#endif

#ifdef __cplusplus
 }
#endif
