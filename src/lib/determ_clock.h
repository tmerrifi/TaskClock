  

#ifdef __cplusplus
 extern "C" {
#endif

#ifndef DETERM_CLOCK_H
#define DETERM_CLOCK_H

/*

  Copyright (c) 2012-15 Tim Merrifield, University of Illinois at Chicago


  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/


     
#include <sys/types.h>
     
#include <perf_counter.h>

#include <debug_clock_cache.h>     

#include "tx_estimate.h"

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
       //when we activate another thread, if they become the lowest that can interfere with another
       //thread that thought *they* were the lowest. Setting this flag means we need to tell those
       //threads that their view of the world needs to be refreshed.
       u_int8_t activated_lowest;
       uint8_t scaling_whole, scaling_fraction;
       //keeps the hw perf counter index to read the counter in userspace
       uint32_t hwc_idx; 
       uint64_t period_sets;
       uint64_t hit_bounded_fence;
   } __attribute__ ((aligned (8), packed));

   struct determ_task_clock_info{
       u_int32_t tid;
       struct perf_counter_info perf_counter;
       struct task_clock_user_status * user_status;
       u_int8_t disabled;
       struct debug_clock_cache debug_clock_cache; //cache of clock values to use (for debugging)
       u_int64_t last_clock_value;
       u_int64_t coarsened_ticks_counter;
       struct tx_estimator estimator;
       u_int8_t in_coarsened_tx;
       u_int64_t last_raw_perf;
       u_int64_t current_raw_perf;
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
#ifndef DETERM_CLOCK_SAMPLE_PERIOD
#define DETERM_CLOCK_SAMPLE_PERIOD 5000
#endif

#define DETERM_CLOCK_NOT_WAITING 0
#define DETERM_CLOCK_LOWEST 1
#define DETERM_CLOCK_WAITING 2

#define TASK_CLOCK_OP_STOP 1
#define TASK_CLOCK_OP_START 2
#define TASK_CLOCK_OP_START_COARSENED 3

#define TASK_CLOCK_COARSENED 1
#define TASK_CLOCK_NOT_COARSENED 2     


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
     void determ_task_clock_start_no_notify();
     void determ_task_clock_stop_with_id(size_t id);
     void determ_task_clock_stop();
     void determ_task_clock_stop_no_notify();
     void determ_task_clock_stop_with_id_no_notify(size_t id);
     void determ_task_clock_halt();
     void determ_task_clock_activate();
     int determ_task_clock_activate_other(int32_t id);
     void determ_task_clock_add_ticks(int32_t ticks);
     void determ_task_clock_on_wakeup();
     int determ_task_clock_single_active_thread();
     void determ_task_clock_clear_single_active_thread();
     int determ_task_clock_is_active();
     void determ_debugging_print_event();
     int determ_debugging_is_disabled();
     void determ_task_clock_close();
     void determ_task_clock_reset();
     u_int32_t determ_task_get_id();
     int64_t determ_task_clock_estimate_next_tx(size_t id);
     u_int64_t determ_task_clock_get_last_tx_size();
     struct determ_task_clock_info determ_task_clock_get_info();
     void determ_task_set_scaling_factor(uint8_t whole, uint8_t fraction);
     void determ_task_clock_end_coarsened_tx();
     uint64_t determ_task_clock_get_coarsened_ticks();
     struct task_clock_user_status * determ_task_clock_get_userspace_info();
     uint64_t determ_task_clock_last_raw_perf();
     uint64_t determ_task_clock_current_raw_perf();
     uint64_t determ_task_clock_period_sets();
     int determ_task_clock_in_coarsened_tx();
     int determ_task_clock_hit_bounded_fence();

#endif

#ifdef __cplusplus
 }
#endif
