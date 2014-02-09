#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/path.h>
#include <linux/pagemap.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>
#include <asm/msr.h>
#include <linux/sched.h>
#include <linux/task_clock.h>
#include <linux/slab.h>
#include <linux/irq_work.h>
#include <linux/wait.h>
#include <linux/perf_event.h>
#include <linux/bitops.h>
#include <linux/vmalloc.h>

#include "listarray.h"

MODULE_LICENSE("GPL");

#define MAX_CLOCK_SAMPLE_PERIOD 200000

#define MIN_CLOCK_SAMPLE_PERIOD 1024

#define DEBUG_THREAD_COUNT 10

struct clock_debug{
    uint64_t clocks[DEBUG_THREAD_COUNT];
    uint64_t clocks_computed[DEBUG_THREAD_COUNT];
    uint64_t initialized[DEBUG_THREAD_COUNT];
    uint64_t sleeping[DEBUG_THREAD_COUNT];
    uint64_t inactive[DEBUG_THREAD_COUNT];
    uint64_t waiting[DEBUG_THREAD_COUNT];
    int new_low;
    int new_low_computed;
    uint64_t lowest_clock;
    int event;
    int current_is_lowest;
    int current_id;
    uint64_t sample_rate;
    uint64_t prev_count;
    int nmi_new_low;
};

#define clock_debug_max_entries 2500

struct clock_debug clock_debug[clock_debug_max_entries];
struct clock_debug clock_debug_overflow[clock_debug_max_entries];

int debug_counter;
int debug_counter_overflow;;


int __lowest_time(struct timespec * t1, struct timespec * t2){
    if (t1->tv_sec != t2->tv_sec){
        return (t1->tv_sec < t2->tv_sec) ? 1 : 2;
    }
    else {
        return (t1->tv_nsec < t2->tv_nsec) ? 1 : 2;
    }
}


unsigned long __elapsed_time_ns(struct timespec * t1, struct timespec * t2){
    struct timespec * start, * end;
    int lowest = __lowest_time(t1,t2);
    start = (lowest==1) ? t1 : t2;
    end = (lowest==1) ? t2 : t1;
        
    return (end->tv_sec-start->tv_sec)*1000000000+(end->tv_nsec-start->tv_nsec);
}


#define __current_tid() current->task_clock.tid

#define __min(val1, val2) ((val1<val2) ? val1 : val2)

#define __max(val1, val2) ((val1>val2) ? val1 : val2)

#define __set_base_ticks(group_info, tid, val) (group_info->clocks[tid].base_ticks=val)

#define __get_base_ticks(group_info, tid) (group_info->clocks[tid].base_ticks)

#define __tick_counter_turn_off(group_info) \
    group_info->clocks[__current_tid()].count_ticks=0;

#define __tick_counter_turn_on(group_info) \
    group_info->clocks[__current_tid()].count_ticks=1;

#define __tick_counter_is_running(group_info) \
    group_info->clocks[__current_tid()].count_ticks


    
    

#ifdef NO_INSTRUCTION_COUNTING

#define __update_clock_ticks(group_info, tid)                           \
    unsigned long rawcount = local64_read(&group_info->clocks[tid].event->count); \
    group_info->clocks[tid].debug_last_overflow_ticks=rawcount;         \
    local64_set(&group_info->clocks[tid].event->count, 0);


#else

#define __update_clock_ticks(group_info, tid)                           \
    unsigned long rawcount = local64_read(&group_info->clocks[tid].event->count); \
    group_info->clocks[tid].debug_last_overflow_ticks=rawcount;         \
    if (rawcount < 0) printk(KERN_EMERG "UHOH, %d\n",tid); \
    group_info->clocks[tid].ticks+=rawcount; \ 
    local64_set(&group_info->clocks[tid].event->count, 0);
#endif

#define __inc_clock_ticks(group_info, tid, val) (group_info->clocks[tid].ticks+=val)

#define __set_clock_ticks(group_info, tid, val) (group_info->clocks[tid].ticks=val)

#define __get_clock_ticks(group_info, tid) (group_info->clocks[tid].ticks + group_info->clocks[tid].base_ticks)


#define __clock_is_lower(group_info, tid1, tid2) ( ((__get_clock_ticks(group_info, tid1))  < (__get_clock_ticks(group_info, tid2))) \
                                               || ((__get_clock_ticks(group_info, tid1)) == (__get_clock_ticks(group_info, tid2))  && (tid1 < tid2)))

#define __clear_entry_state(group_info) \
    group_info->clocks[__current_tid()].inactive=0;\
    group_info->clocks[__current_tid()].waiting=0;\
    group_info->clocks[__current_tid()].sleeping=0;
    

#define __clear_entry_state_by_id(group_info, id) \
    group_info->clocks[id].inactive=0;\
    group_info->clocks[id].waiting=0;\
    group_info->clocks[id].sleeping=0;

#define __mark_as_inactive(group_info, id)         \
    group_info->clocks[id].inactive=1; \
    listarray_remove(group_info->active_threads, id);

#define __mark_as_active(group_info, id)         \
    group_info->clocks[id].inactive=0; \
    listarray_insert(group_info->active_threads, id);

//Am I the only one here????
#define __current_is_only_active_thread(group_info) \
    (listarray_count(group_info->active_threads)==1 && group_info->clocks[__current_tid()].inactive==0)

//is this current tick_count the lowest
int __is_lowest(struct task_clock_group_info * group_info, int32_t tid){
  if (tid==group_info->lowest_tid ||
      __get_clock_ticks(group_info, tid) < __get_clock_ticks(group_info, group_info->lowest_tid)){
    return 1;
  }
  return 0;
} 

int32_t  __search_for_lowest_waiting_exclude_current(struct task_clock_group_info * group_info, int32_t tid){
    int i=0;
    int32_t min_tid=-1;
    listarray_foreach(group_info->active_threads, i){
        struct task_clock_entry_info * entry = &group_info->clocks[i];
        if (entry->initialized && !entry->inactive && entry->waiting && i!=tid && (min_tid < 0 || __clock_is_lower(group_info, i, min_tid))){
            min_tid=i;
        }
    }
    
    return (min_tid >=0) ? min_tid : -1;
}


int32_t __search_for_lowest(struct task_clock_group_info * group_info){
  int i=0;
  int32_t min_tid=-1;
  //for (;i<TASK_CLOCK_MAX_THREADS;++i){
  listarray_foreach(group_info->active_threads, i){
    struct task_clock_entry_info * entry = &group_info->clocks[i];
    if (entry->initialized && !entry->inactive && (min_tid < 0 || __clock_is_lower(group_info, i, min_tid))){
        min_tid=i;
    }
  }




  return min_tid;
}

//This function must be called while holding the group lock
int32_t __search_for_lowest_waiting(struct task_clock_group_info * group_info){
    int thread_arr[128];
    int i=0;
    int low=-1;

    //for (;i<TASK_CLOCK_MAX_THREADS;++i){
    listarray_foreach(group_info->active_threads, i){
        struct task_clock_entry_info * entry = &group_info->clocks[i];
        if (entry->waiting){
            thread_arr[i]=1;
        }
    }
    
    //now find the lowest
    low=__search_for_lowest(group_info);
    //if the lowest is greater than zero and it was waiting...return it
    return (low >= 0 && thread_arr[low]==1) ? low : -1;
}



void __debug_print(struct task_clock_group_info * group_info){
  int i=0;
  int32_t new_low;

  printk(KERN_EMERG "\n\nSEARCH FOR LOWEST....%d\n", current->task_clock.tid);
  for (;i<7;++i){
    struct task_clock_entry_info * entry = &group_info->clocks[i];
    //debugging
    printk(KERN_EMERG " tid: %d ticks %llu inactive %d init %d waiting %d sleeping %d", 
           i, __get_clock_ticks(group_info, i), entry->inactive, entry->initialized, entry->waiting, entry->sleeping);
  }
  new_low=__search_for_lowest_waiting(group_info);
  printk(KERN_EMERG "\nNEW LOW %d, from perspective of %d\n", new_low, current->task_clock.tid);

}

void __clock_debug(struct task_clock_group_info * group_info, int new_low, int event){

    if (debug_counter < clock_debug_max_entries){
        int i=0;
        for (;i<DEBUG_THREAD_COUNT;++i){
            clock_debug[debug_counter].clocks[i]=__get_clock_ticks(group_info, i);
            clock_debug[debug_counter].clocks_computed[i]=__get_clock_ticks(group_info, i) - group_info->lowest_ticks;
            clock_debug[debug_counter].sleeping[i]=group_info->clocks[i].sleeping;
            clock_debug[debug_counter].initialized[i]=group_info->clocks[i].initialized;
            clock_debug[debug_counter].inactive[i]=group_info->clocks[i].inactive;
        }
        clock_debug[debug_counter].new_low = new_low;
        clock_debug[debug_counter].new_low_computed = __search_for_lowest_waiting(group_info);
        clock_debug[debug_counter].lowest_clock = group_info->lowest_ticks;
        clock_debug[debug_counter].event = event;
        clock_debug[debug_counter].current_id = current->task_clock.tid;
        clock_debug[debug_counter].current_is_lowest = group_info->user_status_arr[current->task_clock.tid].lowest_clock;
        clock_debug[debug_counter].sample_rate = group_info->clocks[current->task_clock.tid].event->hw.sample_period;
        
        ++debug_counter;
    }
}


void __clock_debug_overflow(struct task_clock_group_info * group_info, int new_low, int event){
    struct hw_perf_event * hwc = &group_info->clocks[current->task_clock.tid].event->hw;
    if (debug_counter_overflow < clock_debug_max_entries){
        int i=0;
        for (;i<DEBUG_THREAD_COUNT;++i){
            clock_debug_overflow[debug_counter_overflow].clocks[i]=__get_clock_ticks(group_info, i);
            clock_debug_overflow[debug_counter_overflow].clocks_computed[i]=__get_clock_ticks(group_info, i) - group_info->lowest_ticks;
            clock_debug_overflow[debug_counter_overflow].initialized[i]=group_info->clocks[i].initialized;
            clock_debug_overflow[debug_counter_overflow].inactive[i]=group_info->clocks[i].inactive;
            clock_debug_overflow[debug_counter_overflow].waiting[i]=group_info->clocks[i].waiting;

        }
        clock_debug_overflow[debug_counter_overflow].new_low = new_low;
        clock_debug_overflow[debug_counter_overflow].new_low_computed = __search_for_lowest_waiting(group_info);
        clock_debug_overflow[debug_counter_overflow].lowest_clock = group_info->lowest_ticks;
        clock_debug_overflow[debug_counter_overflow].event = event;
        clock_debug_overflow[debug_counter_overflow].current_id = current->task_clock.tid;
        clock_debug_overflow[debug_counter_overflow].sample_rate = group_info->clocks[current->task_clock.tid].event->hw.sample_period;
        clock_debug_overflow[debug_counter_overflow].prev_count =  
            local64_read(&group_info->clocks[__current_tid()].event->count);  //local64_read(&hwc->prev_count);
        clock_debug_overflow[debug_counter_overflow].nmi_new_low=group_info->nmi_new_low;
        ++debug_counter_overflow;
    }
}


void task_clock_debug_add_event(struct task_clock_group_info * group_info, int32_t event){
    __clock_debug_overflow(group_info, 0, event);
}

void __clock_debug_print(void){
    int i=300;
    for (;i<debug_counter;++i){
        int j=0;
        for (;j<DEBUG_THREAD_COUNT;++j){
            printk(KERN_EMERG " id: %d clock: %lu computed %lu initialized %d sleeping %d\n", 
                   j, clock_debug[i].clocks[j], clock_debug[i].clocks_computed[j], clock_debug[i].initialized[j], clock_debug[i].sleeping[j]);
        }
        printk(KERN_EMERG " new_low: %d, new_low_computed: %d, lowest_clock %lu, event: %d, current: %d, current_marked_lowest: %d sample: %lu", 
               clock_debug[i].new_low, clock_debug[i].new_low_computed, 
               clock_debug[i].lowest_clock, clock_debug[i].event, 
               clock_debug[i].current_id, clock_debug[i].current_is_lowest, clock_debug[i].sample_rate);
    }
}

void __clock_debug_print_overflow(void){
    int i=0;
    for (;i<debug_counter_overflow;++i){
        int j=0;
        for (;j<DEBUG_THREAD_COUNT;++j){
            printk(KERN_EMERG " id: %d clock: %lu computed %lu initialized %d inactive %d waiting %d\n", 
                   j, clock_debug_overflow[i].clocks[j], clock_debug_overflow[i].clocks_computed[j], 
                   clock_debug_overflow[i].initialized[j], clock_debug_overflow[i].inactive[j], 
                   clock_debug_overflow[i].waiting[j]);
        }
        printk(KERN_EMERG " new_low: %d, new_low_computed: %d, lowest_clock %lu, event: %d, current: %d, sample: %lu, prev_count: %lu, nmi_new_low: %d", 
               clock_debug_overflow[i].new_low, clock_debug_overflow[i].new_low_computed, 
               clock_debug_overflow[i].lowest_clock, clock_debug_overflow[i].event, 
               clock_debug_overflow[i].current_id, clock_debug_overflow[i].sample_rate,
               clock_debug_overflow[i].prev_count, clock_debug_overflow[i].nmi_new_low);
    }
}



//after we execute this function, a new lowest task clock has been determined
int32_t __new_lowest(struct task_clock_group_info * group_info, int32_t tid){
  int32_t new_low=-1;
  int32_t tmp;

  if (group_info->lowest_tid == -1){
      new_low=__search_for_lowest(group_info);
  }
  //am I the current lowest?
  else if (tid==group_info->lowest_tid && ((tmp=__search_for_lowest(group_info))!=tid) ){
      //looks like things have changed...someone else is the lowest
      new_low=tmp;
  }
  //I'm not the lowest, but perhaps things have changed
  else if (tid!=group_info->lowest_tid && __clock_is_lower(group_info, tid, group_info->lowest_tid)){
    new_low=tid;
  }
  return new_low;
}

int32_t __thread_is_waiting(struct task_clock_group_info * group_info, int32_t tid){
  return (tid >= 0 && group_info->clocks[tid].waiting);
}

int8_t __is_sleeping(struct task_clock_group_info * group_info, int32_t tid){
    return (tid >= 0 && group_info->clocks[tid].sleeping);
}

void __wake_up_waiting_thread(struct task_clock_group_info * group_info, int32_t tid){
  struct perf_buffer * buffer;
  struct perf_event * event = group_info->clocks[tid].event;
  if (__is_sleeping(group_info, group_info->lowest_tid)){
      rcu_read_lock();
      buffer = rcu_dereference(event->buffer);
      atomic_set(&buffer->poll, POLL_IN);
      rcu_read_unlock();
      wake_up_all(&event->task_clock_waitq);
  }
  else{
      //set the lowest threads lowest
      group_info->user_status_arr[group_info->lowest_tid].lowest_clock=1;
  }
}

void __set_new_low(struct task_clock_group_info * group_info, int32_t tid){
    group_info->lowest_tid=tid;
    group_info->lowest_ticks=__get_clock_ticks(group_info, tid);
}
    
void __task_clock_notify_waiting_threads(struct irq_work * work){
    unsigned long flags;
    int lowest_tid=-1;
    struct task_clock_group_info * group_info = container_of(work, struct task_clock_group_info, pending_work);
    if (!spin_trylock_irqsave(&group_info->lock, flags)){
        return;
    }

    int new_low=__search_for_lowest_waiting(group_info);
    
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
    printk(KERN_EMERG "TASK CLOCK: beginning notification of %d\n", new_low);
#endif
    if (new_low>=0 && __thread_is_waiting(group_info,new_low)){
        __set_new_low(group_info,new_low);
        if (group_info->notification_needed || group_info->nmi_new_low){
            group_info->nmi_new_low=0;
            group_info->notification_needed=0;
            //the lowest must be notified
            lowest_tid = group_info->lowest_tid;
        }
    }
    spin_unlock_irqrestore(&group_info->lock, flags);
    if (lowest_tid>=0){
        __wake_up_waiting_thread(group_info, lowest_tid);
    }
}

void __update_period(struct task_clock_group_info * group_info){
    uint64_t lowest_waiting_tid_clock, myclock, new_sample_period;
    int32_t lowest_waiting_tid=0;    

    if (group_info->clocks[current->task_clock.tid].event){
        new_sample_period=group_info->clocks[current->task_clock.tid].event->hw.sample_period+10000;
        lowest_waiting_tid = __search_for_lowest_waiting_exclude_current(group_info, current->task_clock.tid);
        if (lowest_waiting_tid>=0){
            lowest_waiting_tid_clock = __get_clock_ticks(group_info,lowest_waiting_tid);
            myclock = __get_clock_ticks(group_info,current->task_clock.tid);
            //if there is a waiting thread, and its clock is larger than ours, stop when we get there
            if (lowest_waiting_tid_clock > myclock){
                new_sample_period=__max(lowest_waiting_tid_clock - myclock + 1000, MIN_CLOCK_SAMPLE_PERIOD);
            }
        }
        group_info->clocks[current->task_clock.tid].debug_last_sample_period = group_info->clocks[current->task_clock.tid].event->hw.sample_period;
        group_info->clocks[current->task_clock.tid].event->hw.sample_period =  __min(new_sample_period, MAX_CLOCK_SAMPLE_PERIOD);
    }
}

void __reset_period(struct task_clock_group_info * group_info){
    group_info->clocks[current->task_clock.tid].event->hw.sample_period=0;
    local64_set(&group_info->clocks[current->task_clock.tid].event->hw.period_left,0);
}

void task_clock_entry_overflow_update_period(struct task_clock_group_info * group_info){
    unsigned long flags;

    //if we don't want to count ticks...don't do any of this work.
    if (!__tick_counter_is_running(group_info)){
        return;
    }
    
    spin_lock_irqsave(&group_info->nmi_lock, flags);

    __update_clock_ticks(group_info, current->task_clock.tid);

    __update_period(group_info);


    spin_unlock_irqrestore(&group_info->nmi_lock, flags);

}


void task_clock_overflow_handler(struct task_clock_group_info * group_info){
  unsigned long flags;
  int32_t new_low=-1;

  //if we don't want to count ticks...don't do any of this work.
  if (!__tick_counter_is_running(group_info)){
      return;
  }

  spin_lock_irqsave(&group_info->nmi_lock, flags);
  new_low=__new_lowest(group_info, current->task_clock.tid);
  
  if (new_low >= 0 && new_low != current->task_clock.tid && group_info->nmi_new_low==0){
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
      printk(KERN_EMERG "TASK CLOCK: new low %d ticks %llu\n", new_low, __get_clock_ticks(group_info, new_low));
#endif
      //there is a new lowest thread, make sure to set it
      if (__thread_is_waiting(group_info, new_low)){
          group_info->nmi_new_low=1;
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED) && defined(DEBUG_TASK_CLOCK_FINE_GRAINED)
          printk(KERN_EMERG "----TASK CLOCK: notifying %d ticks %llu\n", new_low, __get_clock_ticks(group_info, new_low));
#endif
          //schedule work to be done when we are not in NMI context
          irq_work_queue(&group_info->pending_work);
      }
  }
  spin_unlock_irqrestore(&group_info->nmi_lock, flags);
}

void __set_current_thread_to_lowest(struct task_clock_group_info * group_info){
    current->task_clock.user_status->lowest_clock=1;
    //clear the notification flag
    group_info->notification_needed=0;
    //clear the state
    __clear_entry_state(group_info);
    group_info->lowest_ticks=__get_clock_ticks(group_info,__current_tid());
}

int __determine_lowest_and_notify_or_wait(struct task_clock_group_info * group_info, int user_event){

    int thread_to_wakeup=-1;
    int32_t new_low;

    //put the clock ticks into userspace no matter what
    current->task_clock.user_status->ticks=__get_clock_ticks(group_info, current->task_clock.tid);

    //if we're the only active thread, do our work and get out
    if (__current_is_only_active_thread(group_info)){
        //set it as a single active thread
        task_clock_debug_add_event(group_info, 6969);
        current->task_clock.user_status->single_active_thread=1;
        __set_current_thread_to_lowest(group_info);
    }
    else{
        //we set ourselves to be waiting, this may change
        group_info->clocks[current->task_clock.tid].waiting=1;
        //figure out who the lowest clock is
        new_low=__search_for_lowest_waiting(group_info);    
        //if its not already the lowest_tid, then set it and make sure we notify
        if (new_low!=group_info->lowest_tid){
            group_info->notification_needed=1;
            group_info->lowest_tid=new_low;
        }
        //if we're the lowest, set up our state
        if (new_low >= 0 && group_info->lowest_tid == current->task_clock.tid){
            __set_current_thread_to_lowest(group_info);
        }
        else{
            //we are not the lowest clock, make sure we're all on the same page
            current->task_clock.user_status->lowest_clock=0;
            //is this thread waiting, and has the notification not been sent yet? If so, then send it
            if (__thread_is_waiting(group_info, group_info->lowest_tid) && group_info->notification_needed){
                //set the state for this thread
                group_info->lowest_ticks=__get_clock_ticks(group_info,group_info->lowest_tid);
                group_info->nmi_new_low=0;
                group_info->notification_needed=0;
                thread_to_wakeup=group_info->lowest_tid;
                //the lowest must be notified
            }
        }
        
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED) && defined(DEBUG_TASK_CLOCK_FINE_GRAINED)
        printk(KERN_EMERG "--------TASK CLOCK: lowest_notify_or_wait clock ticks %llu, id %d, waiting %d, lowest clock %d\n", 
               __get_clock_ticks(group_info, current->task_clock.tid), current->task_clock.tid, 
               group_info->clocks[current->task_clock.tid].waiting, current->task_clock.user_status->lowest_clock);
#endif
    }
    
    return thread_to_wakeup;
}

//userspace is disabling the clock. Perhaps they are about to start waiting to be named the lowest. In that
//case, we need to figure out if they are the lowest and let them know before they call poll
void task_clock_on_disable(struct task_clock_group_info * group_info){
    int lowest_tid=-1;
    unsigned long flags;
    spin_lock_irqsave(&group_info->lock, flags);

    //turn the counter off...not that it matters since the "real" perf-counter is off
    __tick_counter_turn_off(group_info);
    //update our clock in case some of the instructions weren't counted in the overflow handler
    __update_clock_ticks(group_info, current->task_clock.tid);
    lowest_tid=__determine_lowest_and_notify_or_wait(group_info, 10);
    //am I the lowest?
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
    printk(KERN_EMERG "TASK CLOCK: disabling %d...lowest is %d lowest clock is %d pid %d\n", 
           current->task_clock.tid, group_info->lowest_tid, current->task_clock.user_status->lowest_clock, current->pid);
#endif
    spin_unlock_irqrestore(&group_info->lock, flags);
    if (lowest_tid>=0){
        __wake_up_waiting_thread(group_info, lowest_tid);
    }
  
}

void task_clock_add_ticks(struct task_clock_group_info * group_info, int32_t ticks){
    int lowest_tid=-1;
    //TODO: Why are we disabling interrupts here?
    unsigned long flags;
    spin_lock_irqsave(&group_info->lock, flags);

    __inc_clock_ticks(group_info, current->task_clock.tid, ticks);
    current->task_clock.user_status->ticks=__get_clock_ticks(group_info, current->task_clock.tid);
    lowest_tid=__determine_lowest_and_notify_or_wait(group_info, 11);
    spin_unlock_irqrestore(&group_info->lock, flags);
    if (lowest_tid>=0){
        __wake_up_waiting_thread(group_info, lowest_tid);
    }

}

void task_clock_on_enable(struct task_clock_group_info * group_info){
    int lowest_tid=-1;
    unsigned long flags;
    spin_lock_irqsave(&group_info->lock, flags);

    //turn the counter back on...next overflow will actually be counted
    __tick_counter_turn_on(group_info);
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
    printk(KERN_EMERG "TASK CLOCK: ENABLING %d, lowest? %d\n", current->task_clock.tid, group_info->lowest_tid);
#endif
    __reset_period(group_info);
    __update_period(group_info);
    
    lowest_tid=__determine_lowest_and_notify_or_wait(group_info, 12);
    //are we the lowest?
    if (group_info->lowest_tid==current->task_clock.tid){
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED) && defined(DEBUG_TASK_CLOCK_FINE_GRAINED)
        printk(KERN_EMERG "----TASK CLOCK: ENABLING %d and setting notification to 1 %d\n", current->task_clock.tid);
#endif
        group_info->notification_needed=1;
        group_info->nmi_new_low=0;
    }
    __clear_entry_state(group_info);
    spin_unlock_irqrestore(&group_info->lock, flags);
    if (lowest_tid>=0){           
        __wake_up_waiting_thread(group_info, lowest_tid);
    }

}

void __init_task_clock_entries(struct task_clock_group_info * group_info){
  int i=0;
  for (;i<TASK_CLOCK_MAX_THREADS;++i){
    struct task_clock_entry_info * entry = &group_info->clocks[i];
    entry->initialized=0;
    entry->waiting=0;
    entry->sleeping=0;
    entry->inactive=0;
    entry->ticks=0;
  }
}

//return a pte given an address
pte_t * pte_get_entry_from_address(struct mm_struct * mm, unsigned long addr){
	
	pgd_t * pgd;
	pud_t *pud;
	pte_t * pte;
	pmd_t *pmd;

	pgd = pgd_offset(mm, addr);
	if (!pgd){
		goto error;
	}
	pud = pud_alloc(mm, pgd, addr);
	if (!pud){
		goto error;
	}
	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd){
		goto error;	
	}
	pte = pte_alloc_map(mm, pmd, addr);
	if (!pte){
		goto error;
	}
	return pte;
	
	error:
		return NULL;
}

void task_clock_entry_init(struct task_clock_group_info * group_info, struct perf_event * event){
 
    pte_t * the_pte;

   if (group_info->clocks[current->task_clock.tid].initialized==0){
        group_info->clocks[current->task_clock.tid].ticks=0;
        group_info->clocks[current->task_clock.tid].base_ticks=0;
    }
    group_info->clocks[current->task_clock.tid].initialized=1;
    group_info->clocks[current->task_clock.tid].event=event;
    __clear_entry_state(group_info);

    if (current->task_clock.tid==0 && group_info->user_status_arr==NULL){
        unsigned long user_page_addr = PAGE_ALIGN((unsigned long)(current->task_clock.user_status)) - PAGE_SIZE;
        //get the offset (from the start of the page) to user_stats
        unsigned int page_offset = (unsigned long)current->task_clock.user_status - user_page_addr;
        //get the physical frame
        the_pte=pte_get_entry_from_address(current->mm, user_page_addr);
        BUG_ON(the_pte==NULL);
        struct page * p = pte_page(*the_pte);
        //map it into the kernel's address space
        void * mapped_page_addr=vmap(&p, 1, VM_MAP, PAGE_KERNEL);
        group_info->user_status_arr=(struct task_clock_user_status *)(mapped_page_addr+page_offset);
    }

#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
  printk(KERN_EMERG "TASK CLOCK: INIT, tid %d event is %p....ticks are %llu \n", 
         current->task_clock.tid, event, __get_clock_ticks(group_info, current->task_clock.tid));
#endif
}

struct task_clock_group_info * task_clock_group_init(void){
  struct task_clock_group_info * group_info = kmalloc(sizeof(struct task_clock_group_info), GFP_KERNEL);
  spin_lock_init(&group_info->nmi_lock);
  spin_lock_init(&group_info->lock);
  group_info->lowest_tid=-1;
  group_info->lowest_ticks=0;
  group_info->notification_needed=1;
  group_info->nmi_new_low=0;
  group_info->user_status_arr=NULL;
  group_info->active_threads = kmalloc(sizeof(struct listarray), GFP_KERNEL);
  printk(KERN_EMERG "listarray_init, size %d \n", sizeof(struct listarray));
  listarray_init(group_info->active_threads);
  __init_task_clock_entries(group_info);
  init_irq_work(&group_info->pending_work, __task_clock_notify_waiting_threads);
  debug_counter_overflow=0;
  return group_info;
}

void task_clock_entry_halt(struct task_clock_group_info * group_info){
  int32_t new_low=-1;
  //first, check if we're the lowest
  unsigned long flags;
  spin_lock_irqsave(&group_info->lock, flags);

  //make us inactive

#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
  printk(KERN_EMERG "TASK CLOCK: halting %d\n", __current_tid());
#endif
  
  task_clock_debug_add_event(group_info, 80085);

  __mark_as_inactive(group_info, __current_tid());
  //are we the lowest?
  if (group_info->lowest_tid==current->task_clock.tid || group_info->lowest_tid==-1){

      group_info->notification_needed=1;
      //we need to find the new lowest and set it
      new_low=__new_lowest(group_info, current->task_clock.tid);
      //is there a new_low?
      group_info->lowest_tid=(new_low >= 0) ? new_low : -1;
      if (new_low >= 0 && __thread_is_waiting(group_info, new_low) && group_info->notification_needed){
          //lets wake it up
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED) && defined(DEBUG_TASK_CLOCK_FINE_GRAINED)
          printk(KERN_EMERG "--------TASK CLOCK: HALTING %d SIGNALING NEW LOW!\n", current->task_clock.tid);
#endif
          group_info->lowest_ticks=__get_clock_ticks(group_info,new_low);
          irq_work_queue(&group_info->pending_work);
      }
  }
  spin_unlock_irqrestore(&group_info->lock, flags);
}

void task_clock_entry_activate(struct task_clock_group_info * group_info){
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
  printk(KERN_EMERG "TASK CLOCK: activating %d\n", current->task_clock.tid);
#endif
  unsigned long flags;
  spin_lock_irqsave(&group_info->lock, flags);
  __set_base_ticks(group_info, current->task_clock.tid,group_info->lowest_ticks);
  __clear_entry_state(group_info);
  task_clock_debug_add_event(group_info, 1234);
  __mark_as_active(group_info, __current_tid());
  __reset_period(group_info);
  __update_period(group_info);
  current->task_clock.user_status->notifying_id=0;
  current->task_clock.user_status->notifying_clock=666;
  current->task_clock.user_status->notifying_sample=666;

    //if I'm the new lowest, we need to set the flag so userspace can see that that is the case
  int32_t new_low=__new_lowest(group_info, current->task_clock.tid);
  if (new_low >= 0 && new_low == current->task_clock.tid){
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
      printk(KERN_EMERG "TASK CLOCK: activated thread is the new low,  %d\n", current->task_clock.tid);
#endif
      group_info->notification_needed=0;
      group_info->lowest_tid=new_low;
      current->task_clock.user_status->lowest_clock=1;

  }
  spin_unlock_irqrestore(&group_info->lock, flags);
}

void task_clock_entry_activate_other(struct task_clock_group_info * group_info, int32_t id){
    
    unsigned long flags;
    spin_lock_irqsave(&group_info->lock, flags);
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
    printk(KERN_EMERG "TASK CLOCK: activating_other %d activating %d\n", current->task_clock.tid, id);
#endif
    group_info->clocks[id].initialized=1;
    group_info->clocks[id].ticks=0;
    group_info->clocks[id].base_ticks=__get_clock_ticks(group_info, current->task_clock.tid) + 1;
    __clear_entry_state_by_id(group_info, id);
    __mark_as_active(group_info, id);
    spin_unlock_irqrestore(&group_info->lock, flags);
}

//TODO:Need to remove this and from the kernel
void task_clock_on_wait(struct task_clock_group_info * group_info){ }

void task_clock_entry_wait(struct task_clock_group_info * group_info){
    int lowest_tid=-1;
    unsigned long flags;
    spin_lock_irqsave(&group_info->lock, flags);

    lowest_tid=__determine_lowest_and_notify_or_wait(group_info, 13);
    spin_unlock_irqrestore(&group_info->lock, flags);
    if (lowest_tid>=0){
        __wake_up_waiting_thread(group_info, lowest_tid);
    }

}

void task_clock_entry_sleep(struct task_clock_group_info * group_info){
    int lowest_tid=-1;
    unsigned long flags;
    spin_lock_irqsave(&group_info->lock, flags);
    lowest_tid=__determine_lowest_and_notify_or_wait(group_info, 14);
    if (group_info->clocks[current->task_clock.tid].waiting){
        group_info->clocks[current->task_clock.tid].sleeping=1;
    }

    /*if (__current_tid()==1){
        printk(KERN_EMERG "SLEEPING for %d\n", __current_tid());
      __debug_print(group_info);
      }*/
    
    spin_unlock_irqrestore(&group_info->lock, flags);
    if (lowest_tid>=0){
        __wake_up_waiting_thread(group_info, lowest_tid);
    }

}

void task_clock_entry_woke_up(struct task_clock_group_info * group_info){
    unsigned long flags;
    spin_lock_irqsave(&group_info->lock, flags);    
    group_info->clocks[current->task_clock.tid].sleeping=0;
    group_info->clocks[current->task_clock.tid].waiting=0;
    spin_unlock_irqrestore(&group_info->lock, flags);
}

//utility function to read the performance counter
void __read_perf_counter(struct hw_perf_event * hwc, uint64_t * _new_raw_count, uint64_t * _prev_raw_count){
    uint64_t new_raw_count, prev_raw_count;
 again:    
    //read the raw counter
    rdmsrl(hwc->event_base + hwc->idx, new_raw_count);
    //get previous count
    prev_raw_count = local64_read(&hwc->prev_count);
    //in case an NMI fires while we're doing this...unlikely but who knows?!
    if (local64_cmpxchg(&hwc->prev_count, prev_raw_count, new_raw_count) != prev_raw_count){
        goto again;
    }
    //set the arguments to the values read
    *_new_raw_count=new_raw_count;
    *_prev_raw_count=prev_raw_count;
}

void task_clock_entry_stop(struct task_clock_group_info * group_info){
    uint64_t new_raw_count, prev_raw_count;
    int64_t delta;
    //for our version of perf counters (v3 for Intel) this works...probably not for anything else
    //ARCH_DEP_TODO
    int shift = 16;
    int lowest_tid=-1;
    struct hw_perf_event * hwc = &group_info->clocks[current->task_clock.tid].event->hw;
    //read the counters using rdmsr
    __read_perf_counter(hwc, &new_raw_count, &prev_raw_count);
    //if this succeeds, then its safe to turn off the tick_counter...meaning we no longer do work inside the overflow handler.
    //Even if an NMI beats us to it...it won't have any work to do since prev_raw_count==new_raw_count after the cmpxchg
    __tick_counter_turn_off(group_info);
    //compute how much the counter has counted. The shift is used since only the first N (currently hard-coded to 48) bits matter
    delta = (new_raw_count << shift) - (prev_raw_count << shift);
    //shift it back to get the actually correct number
    delta >>= shift;
    //add it to our current clock
    __inc_clock_ticks(group_info, current->task_clock.tid, delta);
    //let userspace see it
    current->task_clock.user_status->ticks=__get_clock_ticks(group_info, current->task_clock.tid);
    
    unsigned long flags;
    spin_lock_irqsave(&group_info->lock, flags);

    lowest_tid=__determine_lowest_and_notify_or_wait(group_info, 787);
    spin_unlock_irqrestore(&group_info->lock, flags);

    if (lowest_tid>=0){
        __wake_up_waiting_thread(group_info, lowest_tid);
    }


    BUG_ON(delta > MAX_CLOCK_SAMPLE_PERIOD*5);
    BUG_ON(delta < 0);
}

void task_clock_entry_start(struct task_clock_group_info * group_info){
    uint64_t new_raw_count, prev_raw_count;
    struct hw_perf_event * hwc = &group_info->clocks[current->task_clock.tid].event->hw;
    //by "reading" the counter, we reset the prev_count to the current count. But we won't actually
    //do anything with the delta. That will effectively throw away the ticks that were counted (since
    //we don't want them)
    __read_perf_counter(hwc, &new_raw_count, &prev_raw_count);
    //reset the event's counter to 0
    local64_set(&group_info->clocks[__current_tid()].event->count, 0);
    /*printk(KERN_EMERG "TASK CLOCK: START read ticks for %d is %llu\n", 
      current->task_clock.tid, __get_clock_ticks(group_info, current->task_clock.tid));*/
    //do the rest of what needs to be done (look for others to wakeup, etc...)
    task_clock_on_enable(group_info);
}

int init_module(void)
{
  task_clock_func.task_clock_overflow_handler=task_clock_overflow_handler;
  task_clock_func.task_clock_group_init=task_clock_group_init;
  task_clock_func.task_clock_entry_init=task_clock_entry_init;
  task_clock_func.task_clock_entry_activate=task_clock_entry_activate;
  task_clock_func.task_clock_entry_halt=task_clock_entry_halt;
  task_clock_func.task_clock_on_disable=task_clock_on_disable;
  task_clock_func.task_clock_on_enable=task_clock_on_enable;
  task_clock_func.task_clock_on_wait=task_clock_on_wait;
  task_clock_func.task_clock_entry_activate_other=task_clock_entry_activate_other;
  task_clock_func.task_clock_entry_wait=task_clock_entry_wait;
  task_clock_func.task_clock_entry_sleep=task_clock_entry_sleep;
  task_clock_func.task_clock_overflow_update_period=task_clock_entry_overflow_update_period;
  task_clock_func.task_clock_add_ticks=task_clock_add_ticks;
  task_clock_func.task_clock_entry_woke_up=task_clock_entry_woke_up;
  task_clock_func.task_clock_debug_add_event=task_clock_debug_add_event;
  task_clock_func.task_clock_entry_stop=task_clock_entry_stop;
  task_clock_func.task_clock_entry_start=task_clock_entry_start;

  debug_counter=0;
  debug_counter_overflow=0;

  return 0;
}

void cleanup_module(void)
{
  task_clock_func.task_clock_overflow_handler=NULL;
  task_clock_func.task_clock_group_init=NULL;
  task_clock_func.task_clock_entry_init=NULL;
  task_clock_func.task_clock_entry_activate=NULL;
  task_clock_func.task_clock_entry_halt=NULL;
  task_clock_func.task_clock_on_disable=NULL;
  task_clock_func.task_clock_on_enable=NULL;
  task_clock_func.task_clock_on_wait=NULL;
  task_clock_func.task_clock_entry_activate_other=NULL;
  task_clock_func.task_clock_entry_wait=NULL;
  task_clock_func.task_clock_add_ticks=NULL;
  task_clock_func.task_clock_entry_woke_up=NULL;
  task_clock_func.task_clock_debug_add_event=NULL;  
  task_clock_func.task_clock_entry_stop=NULL;
  task_clock_func.task_clock_entry_start=NULL;

  printk(KERN_EMERG " counter %d\n",  debug_counter_overflow);
  
  __clock_debug_print_overflow(); 

}
