

#ifdef __cplusplus
 extern "C" {
#endif

#ifndef DETERM_CLOCK_H
#define DETERM_CLOCK_H

   #include <sys/types.h>

#define DETERM_CLOCK_MAX_THREADS 1024

   struct determ_task_clock_info{
     u_int32_t tid;
     u_int64_t ticks;
     struct perf_counter_info * perf_counter;
   };

   struct determ_clock_info{
     struct determ_task_clock_info clocks[DETERM_CLOCK_MAX_THREADS];
   };


   //how many instructions to count before generating a sample
#define DETERM_CLOCK_SAMPLE_PERIOD 1000

   static void determ_clock_init();
   void determ_task_clock_init(u_int32_t tid);
   void determ_task_clock_start(u_int32_t tid);
   u_int64_t determ_task_clock_read(u_int32_t tid);

#endif

#ifdef __cplusplus
 }
#endif
