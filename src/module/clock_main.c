
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
#include <linux/hardirq.h>

#include "listarray.h"
#include "bounded_tso.h"
#include "utility.h"
#include "search_entries.h"

#ifdef NO_INSTRUCTION_COUNTING
#include "logical_clock_no_ticks.h"
#else
#include "logical_clock_instruction_counting.h"
#endif

MODULE_LICENSE("GPL");

#define DEBUG_THREAD_COUNT 24

struct clock_debug{
    uint64_t clocks[DEBUG_THREAD_COUNT];
    uint64_t clocks_computed[DEBUG_THREAD_COUNT];
    uint64_t initialized[DEBUG_THREAD_COUNT];
    uint64_t sleeping[DEBUG_THREAD_COUNT];
    uint64_t inactive[DEBUG_THREAD_COUNT];
    uint64_t waiting[DEBUG_THREAD_COUNT];
    uint8_t active[32];
    int new_low;
    int new_low_computed;
    uint64_t lowest_clock;
    int event;
    int current_is_lowest;
    int current_id;
    uint64_t sample_rate;
    uint64_t prev_count;
    int nmi_new_low;
    uint64_t last_written;
};

#define clock_debug_max_entries 5000

struct clock_debug clock_debug_overflow[clock_debug_max_entries];

int debug_counter_overflow;
int __debug_counter_overflow;
int start_counter;


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

void __clock_debug_overflow(struct task_clock_group_info * group_info, int new_low, int event){
    struct hw_perf_event * hwc = &group_info->clocks[current->task_clock.tid].event->hw;
    ++__debug_counter_overflow;    
    clock_debug_overflow[debug_counter_overflow].new_low = new_low;
    clock_debug_overflow[debug_counter_overflow].new_low_computed = __search_for_lowest_waiting_debug(group_info,  clock_debug_overflow[debug_counter_overflow].active);
    clock_debug_overflow[debug_counter_overflow].lowest_clock = group_info->lowest_ticks;
    clock_debug_overflow[debug_counter_overflow].event = event;
    clock_debug_overflow[debug_counter_overflow].current_id = current->task_clock.tid;
    clock_debug_overflow[debug_counter_overflow].sample_rate = group_info->clocks[current->task_clock.tid].event->hw.sample_period;
    clock_debug_overflow[debug_counter_overflow].prev_count =  
        local64_read(&group_info->clocks[__current_tid()].event->count);  //local64_read(&hwc->prev_count);
    clock_debug_overflow[debug_counter_overflow].nmi_new_low=group_info->nmi_new_low;
    clock_debug_overflow[debug_counter_overflow].last_written = group_info->clocks[__current_tid()].event->last_written;
    int i=0;
    for (;i<DEBUG_THREAD_COUNT;++i){
        clock_debug_overflow[debug_counter_overflow].clocks[i]=__get_clock_ticks(group_info, i);
        clock_debug_overflow[debug_counter_overflow].clocks_computed[i]=__get_clock_ticks(group_info,clock_debug_overflow[debug_counter_overflow].new_low_computed);
        clock_debug_overflow[debug_counter_overflow].initialized[i]=group_info->clocks[i].initialized;
        clock_debug_overflow[debug_counter_overflow].inactive[i]=group_info->clocks[i].inactive;
        clock_debug_overflow[debug_counter_overflow].waiting[i]=group_info->clocks[i].waiting;
        clock_debug_overflow[debug_counter_overflow].sleeping[i]=group_info->clocks[i].sleeping;
    }
    
    debug_counter_overflow=(debug_counter_overflow+1) % clock_debug_max_entries;
}


void task_clock_debug_add_event(struct task_clock_group_info * group_info, int32_t event){
    //__clock_debug_overflow(group_info, 0, event);
}

void __clock_debug_print_last_n(int n){
    int i=(debug_counter_overflow > n) ? (debug_counter_overflow - n) : 0;
    printk(KERN_EMERG "counter: %d i %d\n", debug_counter_overflow, i);
    for (;i<debug_counter_overflow;++i){
        int j=0;
        for (;j<DEBUG_THREAD_COUNT;++j){
            printk(KERN_EMERG " id: %d clock: %lu computed %lu initialized %d inactive %d waiting %d sleeping %d\n", 
                   j, clock_debug_overflow[i].clocks[j], clock_debug_overflow[i].clocks_computed[j], 
                   clock_debug_overflow[i].initialized[j], clock_debug_overflow[i].inactive[j], 
                   clock_debug_overflow[i].waiting[j], clock_debug_overflow[i].sleeping[j]);
        }
        printk(KERN_EMERG " new_low: %d, new_low_computed: %d, lowest_clock %lu, event: %d, current: %d, sample: %lu, prev_count: %lu, nmi_new_low: %d, last written: %llu", 
               clock_debug_overflow[i].new_low, clock_debug_overflow[i].new_low_computed, 
               clock_debug_overflow[i].lowest_clock, clock_debug_overflow[i].event, 
               clock_debug_overflow[i].current_id, clock_debug_overflow[i].sample_rate,
               clock_debug_overflow[i].prev_count, clock_debug_overflow[i].nmi_new_low, clock_debug_overflow[i].last_written);
        char active_str[256];
        int counter=0;
        for (j=0;j<32;++j){
            counter+=sprintf(active_str+counter, "%d,", clock_debug_overflow[i].active[j]);
        }
        printk(KERN_EMERG " active: %s\n", active_str);
    }
}

void __clock_debug_print_overflow(void){
    int i=0;
    for (;i<debug_counter_overflow;++i){
        int j=0;
        for (;j<DEBUG_THREAD_COUNT;++j){
            printk(KERN_EMERG " id: %d clock: %lu computed %lu initialized %d inactive %d waiting %d sleeping %d\n", 
                   j, clock_debug_overflow[i].clocks[j], clock_debug_overflow[i].clocks_computed[j], 
                   clock_debug_overflow[i].initialized[j], clock_debug_overflow[i].inactive[j], 
                   clock_debug_overflow[i].waiting[j], clock_debug_overflow[i].sleeping[j] );
        }
        printk(KERN_EMERG " new_low: %d, new_low_computed: %d, lowest_clock %lu, event: %d, current: %d, sample: %lu, prev_count: %lu, nmi_new_low: %d, last written: %llu", 
               clock_debug_overflow[i].new_low, clock_debug_overflow[i].new_low_computed, 
               clock_debug_overflow[i].lowest_clock, clock_debug_overflow[i].event, 
               clock_debug_overflow[i].current_id, clock_debug_overflow[i].sample_rate,
               clock_debug_overflow[i].prev_count, clock_debug_overflow[i].nmi_new_low, clock_debug_overflow[i].last_written);
        char active_str[256];
        int counter=0;
        for (j=0;j<32;++j){
            counter+=sprintf(active_str+counter, "%d,", clock_debug_overflow[i].active[j]);
        }
        printk(KERN_EMERG " active: %s\n", active_str);
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
      //__clock_debug_overflow(group_info, group_info->lowest_tid, 2020);
      rcu_read_lock();
      buffer = rcu_dereference(event->buffer);
      atomic_set(&buffer->poll, POLL_IN);
      rcu_read_unlock();
      wake_up_all(&event->task_clock_waitq);
  }
  else{
      //set the lowest threads lowest
      group_info->user_status_arr[group_info->lowest_tid].lowest_clock=1;
      //__clock_debug_overflow(group_info, group_info->lowest_tid, 1010);
  }
}

void __set_new_low(struct task_clock_group_info * group_info, int32_t tid){
    group_info->lowest_tid=tid;
    group_info->lowest_ticks=__get_clock_ticks(group_info, tid);
}

    
void __task_clock_notify_waiting_threads(struct task_clock_group_info * group_info){
    unsigned long flags;
    int lowest_tid=-1;
    spin_lock_irqsave(&group_info->lock, flags);
    int new_low=__search_for_lowest_waiting(group_info);
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
    printk(KERN_EMERG "TASK CLOCK: beginning notification of %d\n", new_low);
#endif
    if (new_low>=0 && __thread_is_waiting(group_info,new_low)){
        __set_new_low(group_info,new_low);
        //__clock_debug_overflow(group_info, new_low, 888);
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

void __task_clock_notify_waiting_threads_irq(struct irq_work * work){
    struct task_clock_group_info * group_info = container_of(work, struct task_clock_group_info, pending_work);
    __task_clock_notify_waiting_threads(group_info);
}

void task_clock_entry_overflow_update_period(struct task_clock_group_info * group_info){
    unsigned long flags;

    //if we don't want to count ticks...don't do any of this work.
    if (!__tick_counter_is_running(group_info)){
        return;
    }
    
    //spin_lock_irqsave(&group_info->nmi_lock, flags);

    logical_clock_update_clock_ticks(group_info, __current_tid());

    //__update_period(group_info);
    logical_clock_update_overflow_period(group_info, __current_tid());

    //spin_unlock_irqrestore(&group_info->nmi_lock, flags);

}


void task_clock_overflow_handler(struct task_clock_group_info * group_info, struct pt_regs *regs){
  unsigned long flags;
  int32_t new_low=-1;

  //if we don't want to count ticks...don't do any of this work.
  if (!__tick_counter_is_running(group_info)){
      return;
  }
  
  if (__single_stepping_on(group_info, __current_tid())){
      bounded_memory_fence_turn_on_tf(group_info, regs);
  }

  //spin_lock_irqsave(&group_info->nmi_lock, flags);
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
  //spin_unlock_irqrestore(&group_info->nmi_lock, flags);
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
        current->task_clock.user_status->single_active_thread=1;
        __set_current_thread_to_lowest(group_info);
        //__clock_debug_overflow(group_info, new_low, 1234);
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
            //__clock_debug_overflow(group_info, new_low, 222);
        }
        //if we're the lowest, set up our state
        if (new_low >= 0 && group_info->lowest_tid == current->task_clock.tid){
            __set_current_thread_to_lowest(group_info);
            //__clock_debug_overflow(group_info, new_low, 4321);
        }
        else{
            //we are not the lowest clock, make sure we're all on the same page
            current->task_clock.user_status->lowest_clock=0;
            //is this thread waiting, and has the notification not been sent yet? If so, then send it
            if (__thread_is_waiting(group_info, group_info->lowest_tid) && group_info->notification_needed){
                //set the state for this thread
                group_info->nmi_new_low=0;
                group_info->notification_needed=0;
                thread_to_wakeup=group_info->lowest_tid;
                //the lowest must be notified
            }
        }
        if (group_info->lowest_tid>=0){
            //set the group-level lowest ticks
            group_info->lowest_ticks=__get_clock_ticks(group_info,group_info->lowest_tid);
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

    //if we are in single stepping mode, we no longer need it
    if (__single_stepping_on(group_info, __current_tid())){
        end_bounded_memory_fence_early(group_info);
    }

    //if the counter is running, grab the ticks that may not have been counted by the NMI handler
    if (__tick_counter_is_running(group_info)){
        logical_clock_update_clock_ticks(group_info, current->task_clock.tid);
        //turn the counter off...
        __tick_counter_turn_off(group_info);
    }
    else{
        //clear the counter
        logical_clock_reset_current_ticks(group_info,__current_tid());
    }
    group_info->notification_needed=1;
    spin_lock_irqsave(&group_info->lock, flags);
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
    __inc_clock_ticks_no_chunk_add(group_info, current->task_clock.tid, ticks);
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

    //reset the tick value now so we can figure out the length of a chunk later
    __reset_chunk_ticks(group_info, __current_tid());

    //turn the counter back on...next overflow will actually be counted
    __tick_counter_turn_on(group_info);
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
    printk(KERN_EMERG "TASK CLOCK: ENABLING %d, lowest? %d\n", current->task_clock.tid, group_info->lowest_tid);
#endif
    logical_clock_reset_overflow_period(group_info, __current_tid());
    //__update_period(group_info);
    logical_clock_update_overflow_period(group_info, __current_tid());

    if (group_info->clocks[current->task_clock.tid].userspace_reading==1){
        group_info->clocks[current->task_clock.tid].userspace_reading=0;
        logical_clock_set_perf_counter(group_info, __current_tid());
    }

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
    group_info->clocks[current->task_clock.tid].userspace_reading=0;
    group_info->clocks[current->task_clock.tid].event=event;
    group_info->clocks[current->task_clock.tid].count_ticks=0;
    
    __reset_chunk_ticks(group_info, __current_tid());
    __clear_entry_state(group_info);
    __tick_counter_turn_off(group_info);

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
    current->task_clock.group_info=group_info;
    current->task_clock.user_status->hwc_idx=-1;

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
  listarray_init(group_info->active_threads);
  __init_task_clock_entries(group_info);
  init_irq_work(&group_info->pending_work, __task_clock_notify_waiting_threads_irq);
  debug_counter_overflow=0;
  return group_info;
}

void task_clock_entry_halt(struct task_clock_group_info * group_info){
  int32_t lowest_tid=-1;
  //first, check if we're the lowest
  unsigned long flags;
  spin_lock_irqsave(&group_info->lock, flags);
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
  printk(KERN_EMERG "TASK CLOCK: halting %d\n", __current_tid());
#endif
  //make us inactive
  __mark_as_inactive(group_info, __current_tid());
  lowest_tid=__determine_lowest_and_notify_or_wait(group_info, 13);
  //clear the waiting flag...if its set we are not really "waiting"
  group_info->clocks[__current_tid()].waiting=0;
  spin_unlock_irqrestore(&group_info->lock, flags);
  if (lowest_tid>=0){
      __wake_up_waiting_thread(group_info, lowest_tid);
  }
}

void task_clock_entry_activate(struct task_clock_group_info * group_info){
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
  printk(KERN_EMERG "TASK CLOCK: activating %d\n", current->task_clock.tid);
#endif
  unsigned long flags;
  spin_lock_irqsave(&group_info->lock, flags);
  __clear_entry_state(group_info);
  //need to reset chunk ticks before calling logical_clock_update_overflow_period
  __reset_chunk_ticks(group_info, __current_tid());
  __mark_as_active(group_info, __current_tid());
  logical_clock_reset_overflow_period(group_info, __current_tid());
  //__update_period(group_info);
  logical_clock_update_overflow_period(group_info, __current_tid());
  current->task_clock.user_status->notifying_id=0;
  current->task_clock.user_status->notifying_clock=666;
  current->task_clock.user_status->notifying_sample=666;
  current->task_clock.user_status->hit_bounded_fence=0;

  group_info->clocks[current->task_clock.tid].userspace_reading=0;
    //if I'm the new lowest, we need to set the flag so userspace can see that that is the case
  int32_t new_low=__new_lowest(group_info, current->task_clock.tid);
  if (new_low >= 0 && new_low == current->task_clock.tid){
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
      printk(KERN_EMERG "TASK CLOCK: activated thread is the new low,  %d\n", current->task_clock.tid);
#endif
      group_info->notification_needed=0;
      group_info->lowest_tid=new_low;
      current->task_clock.user_status->lowest_clock=1;
      //__clock_debug_overflow(group_info, new_low, 999);

  }
  spin_unlock_irqrestore(&group_info->lock, flags);
}

void task_clock_entry_activate_other(struct task_clock_group_info * group_info, int32_t id){
    
    unsigned long flags;
    spin_lock_irqsave(&group_info->lock, flags);
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
    printk(KERN_EMERG "TASK CLOCK: activating_other %d activating %d\n", current->task_clock.tid, id);
#endif
    if (group_info->clocks[id].initialized!=1){
        group_info->clocks[id].base_ticks=__get_clock_ticks(group_info, current->task_clock.tid) + 1;
        group_info->clocks[id].ticks=0;
    }
    group_info->clocks[id].initialized=1;
    //group_info->clocks[id].ticks=0;
    //group_info->clocks[id].base_ticks=__get_clock_ticks(group_info, current->task_clock.tid) + 1;
    __clear_entry_state_by_id(group_info, id);
    __mark_as_active(group_info, id);
    __reset_chunk_ticks(group_info, id);
    //if the newly activated thread is the lowest, then we need to set a flag so userspace can deal with it. Since
    //another thread may be convinced it is the lowest
    if (group_info->lowest_tid < 0 || __clock_is_lower(group_info, id, group_info->lowest_tid)){
        current->task_clock.user_status->activated_lowest=1;
    }

    spin_unlock_irqrestore(&group_info->lock, flags);
}


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

void task_clock_entry_reset(struct task_clock_group_info * group_info){
    group_info->clocks[__current_tid()].initialized=0;
}

void task_clock_entry_sleep(struct task_clock_group_info * group_info){
    int lowest_tid=-1;
    unsigned long flags;
    spin_lock_irqsave(&group_info->lock, flags);
    while(true){
        lowest_tid=__determine_lowest_and_notify_or_wait(group_info, 14);
        //set ourselves to be sleeping
        if (group_info->clocks[current->task_clock.tid].waiting){
            group_info->clocks[current->task_clock.tid].sleeping=1;
        }
        //if we aren't going to wake anyone up, and no one else is alive...
        //do what we can to wake someone up
        if (lowest_tid<0 && __active_thread_count(group_info)==0){
#if defined(DEBUG_TASK_CLOCK_FINE_GRAINED)
            printk(KERN_EMERG "DLC: No one is awake %d old lowest tid %d count %d total %d\n", 
                   group_info->notification_needed, group_info->lowest_tid, debug_counter_overflow, __debug_counter_overflow);
            __clock_debug_print_last_n(10);
            __search_for_lowest_print(group_info);
#endif
        }
        else{
            //break out
            break;
        }
    }

    spin_unlock_irqrestore(&group_info->lock, flags);

    if (lowest_tid>=0){
        __wake_up_waiting_thread(group_info, lowest_tid);
    }
}

//our thread is waking up
void task_clock_entry_woke_up(struct task_clock_group_info * group_info){
    unsigned long flags;
    spin_lock_irqsave(&group_info->lock, flags);    
    group_info->clocks[current->task_clock.tid].sleeping=0;
    group_info->clocks[current->task_clock.tid].waiting=0;
    spin_unlock_irqrestore(&group_info->lock, flags);
}

//Called when the counting has finished...we don't actually stop counting, we just
//won't consider ticks that happen after this
void task_clock_entry_stop(struct task_clock_group_info * group_info){
    uint64_t new_raw_count, prev_raw_count;
    int64_t delta;
    int lowest_tid=-1;
    int profile=0;

    //if we are in single stepping mode, we no longer need it
    if (__single_stepping_on(group_info, __current_tid())){
        end_bounded_memory_fence_early(group_info);
    }
    
    //read the counter and update our clock
    logical_clock_read_clock_and_update(group_info, __current_tid());
    
    unsigned long flags;
    spin_lock_irqsave(&group_info->lock, flags);

    lowest_tid=__determine_lowest_and_notify_or_wait(group_info, 787);
    spin_unlock_irqrestore(&group_info->lock, flags);

    if (lowest_tid>=0){
        __wake_up_waiting_thread(group_info, lowest_tid);
    }
}

//just like regular stop, just don't try and wake any one up. Or grab the lock!
void task_clock_entry_stop_no_notify(struct task_clock_group_info * group_info){
    logical_clock_read_clock_and_update(group_info, __current_tid());
}

//lets start caring about the ticks we see (again)
void task_clock_entry_start(struct task_clock_group_info * group_info){
    current->task_clock.user_status->notifying_id=0;
    //the clock may have continued to run...so reset the ticks we've seen
    logical_clock_reset_current_ticks(group_info,__current_tid());
    task_clock_on_enable(group_info);
    ++start_counter;
}

void task_clock_entry_start_no_notify(struct task_clock_group_info * group_info){
    group_info->clocks[current->task_clock.tid].userspace_reading=1;
    //the clock may have continued to run...so reset the ticks we've seen
    logical_clock_reset_current_ticks(group_info,__current_tid());
    //now that its reset, lets set the counter to be VERY HIGH...if we get beat by an overflow its ok
    //because we've already reset the counter
    logical_clock_set_perf_counter_max(group_info,__current_tid());
    //turn off overflows...just in case
    __tick_counter_turn_off(group_info);
    current->task_clock.user_status->notifying_id=666;
    ++start_counter;
}

int task_clock_entry_is_singlestep(struct task_clock_group_info * group_info, struct pt_regs *regs){
    return on_single_step(group_info, regs);
}

int init_module(void)
{
#ifdef USE_BOUNDED_FENCE
    BUG_ON(BOUNDED_CHUNK_SIZE<MAX_CLOCK_SAMPLE_PERIOD);
#endif
    
    task_clock_func.task_clock_overflow_handler=task_clock_overflow_handler;
    task_clock_func.task_clock_group_init=task_clock_group_init;
    task_clock_func.task_clock_entry_init=task_clock_entry_init;
    task_clock_func.task_clock_entry_activate=task_clock_entry_activate;
    task_clock_func.task_clock_entry_halt=task_clock_entry_halt;
    task_clock_func.task_clock_on_disable=task_clock_on_disable;
    task_clock_func.task_clock_on_enable=task_clock_on_enable;
    task_clock_func.task_clock_entry_activate_other=task_clock_entry_activate_other;
    task_clock_func.task_clock_entry_wait=task_clock_entry_wait;
    task_clock_func.task_clock_entry_sleep=task_clock_entry_sleep;
    task_clock_func.task_clock_overflow_update_period=task_clock_entry_overflow_update_period;
    task_clock_func.task_clock_add_ticks=task_clock_add_ticks;
    task_clock_func.task_clock_entry_woke_up=task_clock_entry_woke_up;
    task_clock_func.task_clock_debug_add_event=task_clock_debug_add_event;
    task_clock_func.task_clock_entry_stop=task_clock_entry_stop;
    task_clock_func.task_clock_entry_start=task_clock_entry_start;
    task_clock_func.task_clock_entry_reset=task_clock_entry_reset;
    task_clock_func.task_clock_entry_start_no_notify=task_clock_entry_start_no_notify;
    task_clock_func.task_clock_entry_stop_no_notify=task_clock_entry_stop_no_notify;
#ifdef USE_BOUNDED_FENCE
    task_clock_func.task_clock_entry_is_singlestep=task_clock_entry_is_singlestep;
#else
    task_clock_func.task_clock_entry_is_singlestep=NULL;
#endif
    
  debug_counter_overflow=0;
  __debug_counter_overflow=0;
  start_counter=0;

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
  task_clock_func.task_clock_entry_activate_other=NULL;
  task_clock_func.task_clock_entry_wait=NULL;
  task_clock_func.task_clock_add_ticks=NULL;
  task_clock_func.task_clock_entry_woke_up=NULL;
  task_clock_func.task_clock_debug_add_event=NULL;  
  task_clock_func.task_clock_entry_stop=NULL;
  task_clock_func.task_clock_entry_start=NULL;
  task_clock_func.task_clock_entry_is_singlestep=NULL;


  printk(KERN_EMERG "start counter: %d\n", start_counter);
}
