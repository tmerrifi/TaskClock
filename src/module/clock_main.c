#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/path.h>
#include <linux/pagemap.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>
#include <linux/sched.h>
#include <linux/task_clock.h>
#include <linux/slab.h>
#include <linux/irq_work.h>
#include <linux/wait.h>
#include <linux/perf_event.h>
#include <linux/bitops.h>
#include <linux/vmalloc.h>

MODULE_LICENSE("GPL");

#define MAX_CLOCK_SAMPLE_PERIOD 50000

#define MIN_CLOCK_SAMPLE_PERIOD 1024

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


#define __min(val1, val2) ((val1<val2) ? val1 : val2)

#define __max(val1, val2) ((val1>val2) ? val1 : val2)

#define __set_base_ticks(group_info, tid, val) (group_info->clocks[tid].base_ticks=val)

#define __get_base_ticks(group_info, tid) (group_info->clocks[tid].base_ticks)

//TODO: get rid of that div. by 100
#define __update_clock_ticks(group_info, tid)                            \
    unsigned long rawcount = local64_read(&group_info->clocks[tid].event->count); \
    group_info->clocks[tid].debug_last_overflow_ticks=rawcount; \
    int msb = fls_long(rawcount); \
    if (msb > 9){                                                       \
        group_info->clocks[tid].ticks+=(rawcount & ~((1 << (msb - 2)) - 1));\
    }\
    local64_set(&group_info->clocks[tid].event->count, 0);

#define __inc_clock_ticks(group_info, tid, val) (group_info->clocks[tid].ticks+=val)

#define __set_clock_ticks(group_info, tid, val) (group_info->clocks[tid].ticks=val)

#define __get_clock_ticks(group_info, tid) (group_info->clocks[tid].ticks + group_info->clocks[tid].base_ticks)


#define __clock_is_lower(group_info, tid1, tid2) ((__get_clock_ticks(group_info, tid1) < __get_clock_ticks(group_info, tid2)) \
						  || (__get_clock_ticks(group_info, tid1) == __get_clock_ticks(group_info, tid2) && (tid1 < tid2)))

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
  for (;i<TASK_CLOCK_MAX_THREADS;++i){
    struct task_clock_entry_info * entry = &group_info->clocks[i];
    if (entry->initialized && !entry->inactive && entry->waiting && i!=tid && (min_tid < 0 || __clock_is_lower(group_info, i, min_tid))){
        min_tid=i;
    }
  }

  return (min_tid >=0) ? min_tid : -1;
}

int32_t  __search_for_lowest_waiting_and_current(struct task_clock_group_info * group_info, int32_t tid){
  int i=0;
  int32_t min_tid=-1;
  for (;i<TASK_CLOCK_MAX_THREADS;++i){
    struct task_clock_entry_info * entry = &group_info->clocks[i];
    if (entry->initialized && !entry->inactive && (min_tid < 0 || __clock_is_lower(group_info, i, min_tid))){
        min_tid=i;
    }
  }

  return (min_tid==tid || (min_tid >=0 && group_info->clocks[min_tid].waiting)) ? min_tid : -1;
}

int32_t __search_for_lowest(struct task_clock_group_info * group_info){
  int i=0;
  int32_t min_tid=-1;
  for (;i<TASK_CLOCK_MAX_THREADS;++i){
    struct task_clock_entry_info * entry = &group_info->clocks[i];
    if (entry->initialized && !entry->inactive && (min_tid < 0 || __clock_is_lower(group_info, i, min_tid))){
        min_tid=i;
    }
  }

  return min_tid;
}

void __debug_print(struct task_clock_group_info * group_info){
  int i=0;
  printk(KERN_EMERG "\n\nSEARCH FOR LOWEST....%d\n", current->task_clock.tid);
  for (;i<7;++i){
    struct task_clock_entry_info * entry = &group_info->clocks[i];
    //debugging
    printk(KERN_EMERG " tid: %d ticks %d inactive %d init %d waiting %d::", i, __get_clock_ticks(group_info, i), entry->inactive, entry->initialized, entry->waiting);
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

void __wake_up_waiting_thread(struct perf_event * event, struct task_clock_group_info * group_info){
  struct perf_buffer * buffer;
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

void __task_clock_notify_waiting_threads(struct irq_work * work){
  unsigned long flags;
  struct task_clock_group_info * group_info = container_of(work, struct task_clock_group_info, pending_work);
  spin_lock_irqsave(&group_info->lock, flags);

#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
  printk(KERN_EMERG "TASK CLOCK: beginning notification of %d\n", group_info->lowest_tid);
#endif

  if ((group_info->notification_needed || group_info->nmi_new_low) && __thread_is_waiting(group_info, group_info->lowest_tid)){
      group_info->nmi_new_low=0;
      group_info->notification_needed=0;
      //the lowest must be notified
      struct perf_event * event = group_info->clocks[group_info->lowest_tid].event;
      __wake_up_waiting_thread(event, group_info);
  }
  else{
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED) && defined(DEBUG_TASK_CLOCK_FINE_GRAINED)
      printk(KERN_EMERG "TASK CLOCK: notification of %d didn't happen because of notification_needed\n", group_info->lowest_tid);
#endif
  }


  spin_unlock_irqrestore(&group_info->lock, flags);
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
        local64_set(&group_info->clocks[current->task_clock.tid].event->hw.period_left,0);
    }
}



void task_clock_entry_overflow_update_period(struct task_clock_group_info * group_info){
    unsigned long flags;

    spin_lock_irqsave(&group_info->nmi_lock, flags);
    __update_clock_ticks(group_info, current->task_clock.tid);
    __update_period(group_info);
    spin_unlock_irqrestore(&group_info->nmi_lock, flags);
}

__debug_token_wakeup(struct task_clock_group_info * group_info){
    struct task_clock_entry_info * our_entry = &group_info->clocks[current->task_clock.tid];
    struct task_clock_entry_info * waiting_entry = &group_info->clocks[group_info->lowest_tid];

    group_info->user_status_arr[group_info->lowest_tid].notifying_clock=__get_clock_ticks(group_info,current->task_clock.tid);
    group_info->user_status_arr[group_info->lowest_tid].notifying_id=current->task_clock.tid;
    group_info->user_status_arr[group_info->lowest_tid].notifying_sample=group_info->clocks[current->task_clock.tid].debug_last_sample_period;
    group_info->user_status_arr[group_info->lowest_tid].notifying_diff=group_info->clocks[current->task_clock.tid].debug_last_overflow_ticks;

}



void task_clock_overflow_handler(struct task_clock_group_info * group_info){
  unsigned long flags;
  int32_t new_low=-1;

  spin_lock_irqsave(&group_info->nmi_lock, flags);
  current->task_clock.user_status->notifying_id++;
  new_low=__new_lowest(group_info, current->task_clock.tid);
  if (new_low >= 0 && new_low != current->task_clock.tid && group_info->nmi_new_low==0){
      group_info->nmi_new_low=1;
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
      printk(KERN_EMERG "TASK CLOCK: new low %d ticks %llu\n", new_low, __get_clock_ticks(group_info, new_low));
#endif
      //there is a new lowest thread, make sure to set it
      group_info->lowest_tid=new_low;
      if (__thread_is_waiting(group_info, new_low)){
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED) && defined(DEBUG_TASK_CLOCK_FINE_GRAINED)
          printk(KERN_EMERG "----TASK CLOCK: notifying %d ticks %llu\n", new_low, __get_clock_ticks(group_info, new_low));
#endif
          group_info->lowest_ticks=__get_clock_ticks(group_info,group_info->lowest_tid);
          //we use this opportunity to set the lowest threads flag...this way, if its spinning right now
          //it can wake up immediately. 
          group_info->user_status_arr[group_info->lowest_tid].lowest_clock=1;
          __debug_token_wakeup(group_info);
          //schedule work to be done when we are not in NMI context
          irq_work_queue(&group_info->pending_work);
      }
  }
  
  spin_unlock_irqrestore(&group_info->nmi_lock, flags);
}

void __determine_lowest_and_notify_or_wait(struct task_clock_group_info * group_info){

    int32_t new_low=__search_for_lowest_waiting_and_current(group_info, current->task_clock.tid);    

    if (new_low >=0 && new_low!=group_info->lowest_tid){
        group_info->notification_needed=1;
        group_info->lowest_tid=new_low;
    }

    if (group_info->lowest_tid == current->task_clock.tid && new_low!=-1){
        current->task_clock.user_status->lowest_clock=1;
        group_info->notification_needed=0;
        group_info->clocks[current->task_clock.tid].waiting=0;
        group_info->clocks[current->task_clock.tid].sleeping=0;
        group_info->lowest_ticks=__get_clock_ticks(group_info,current->task_clock.tid);
    }
    else{
        current->task_clock.user_status->lowest_clock=0;
        if (__thread_is_waiting(group_info, group_info->lowest_tid) && group_info->notification_needed){
            group_info->lowest_ticks=__get_clock_ticks(group_info,group_info->lowest_tid);
            group_info->nmi_new_low=0;
            group_info->notification_needed=0;
            //the lowest must be notified
            struct perf_event * event = group_info->clocks[group_info->lowest_tid].event;
            __wake_up_waiting_thread(event, group_info);
        }
        //after we return from this function we'll have to call poll(), so we can set the waiting flag now
        group_info->clocks[current->task_clock.tid].waiting=1;
    }

#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED) && defined(DEBUG_TASK_CLOCK_FINE_GRAINED)
    printk(KERN_EMERG "--------TASK CLOCK: lowest_notify_or_wait clock ticks %llu, id %d, waiting %d, lowest clock %d\n", 
           __get_clock_ticks(group_info, current->task_clock.tid), current->task_clock.tid, 
           group_info->clocks[current->task_clock.tid].waiting, current->task_clock.user_status->lowest_clock);
#endif
    
    current->task_clock.user_status->ticks=__get_clock_ticks(group_info, current->task_clock.tid);
}

//userspace is disabling the clock. Perhaps they are about to start waiting to be named the lowest. In that
//case, we need to figure out if they are the lowest and let them know before they call poll
void task_clock_on_disable(struct task_clock_group_info * group_info){
  unsigned long flags;

  //TODO: Why are we disabling interrupts here?
  spin_lock_irqsave(&group_info->lock, flags);

  //update our clock in case some of the instructions weren't counted in the overflow handler
  __update_clock_ticks(group_info, current->task_clock.tid);
  __determine_lowest_and_notify_or_wait(group_info);

  //am I the lowest?
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
  printk(KERN_EMERG "TASK CLOCK: disabling %d...lowest is %d lowest clock is %d pid %d\n", 
         current->task_clock.tid, group_info->lowest_tid, current->task_clock.user_status->lowest_clock, current->pid);
#endif
  
  getrawmonotonic(&group_info->clocks[current->task_clock.tid].debug_last_disable);

  spin_unlock_irqrestore(&group_info->lock, flags);
}

void task_clock_add_ticks(struct task_clock_group_info * group_info, int32_t ticks){
    unsigned long flags;

  //TODO: Why are we disabling interrupts here?
  spin_lock_irqsave(&group_info->lock, flags);
  __inc_clock_ticks(group_info, current->task_clock.tid, ticks);
  current->task_clock.user_status->ticks=__get_clock_ticks(group_info, current->task_clock.tid);

  spin_unlock_irqrestore(&group_info->lock, flags);
}

void task_clock_on_enable(struct task_clock_group_info * group_info){
    unsigned long flags;
    spin_lock_irqsave(&group_info->lock, flags);
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
    printk(KERN_EMERG "TASK CLOCK: ENABLING %d, lowest? %d\n", current->task_clock.tid, group_info->lowest_tid);
#endif
    group_info->clocks[current->task_clock.tid].inactive=0;
    group_info->clocks[current->task_clock.tid].waiting=0;
    group_info->clocks[current->task_clock.tid].sleeping=0;
    group_info->clocks[group_info->lowest_tid].event->hw.sample_period=0;
    __update_period(group_info);
    __determine_lowest_and_notify_or_wait(group_info);
    //are we the lowest?
    if (group_info->lowest_tid==current->task_clock.tid){
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED) && defined(DEBUG_TASK_CLOCK_FINE_GRAINED)
        printk(KERN_EMERG "----TASK CLOCK: ENABLING %d and setting notification to 1 %d\n", current->task_clock.tid);
#endif
        group_info->notification_needed=1;
        group_info->nmi_new_low=0;
    }

    getrawmonotonic(&group_info->clocks[current->task_clock.tid].debug_last_enable);
    group_info->clocks[current->task_clock.tid].debug_last_enable_ticks = __get_clock_ticks(group_info, current->task_clock.tid);
    
    //GET RID OF THIS
    current->task_clock.user_status->notifying_id=0;

    spin_unlock_irqrestore(&group_info->lock, flags);

}

void __init_task_clock_entries(struct task_clock_group_info * group_info){
  int i=0;
  for (;i<TASK_CLOCK_MAX_THREADS;++i){
    struct task_clock_entry_info * entry = &group_info->clocks[i];
    entry->initialized=0;
    entry->waiting=0;
    entry->sleeping=0;
    entry->inactive=0;
    entry->ticks=(1ULL<<63);
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
    group_info->clocks[current->task_clock.tid].inactive=0;
    group_info->clocks[current->task_clock.tid].waiting=0;
    group_info->clocks[current->task_clock.tid].sleeping=0;
    

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
  __init_task_clock_entries(group_info);
  init_irq_work(&group_info->pending_work, __task_clock_notify_waiting_threads);
  return group_info;
}

void task_clock_entry_halt(struct task_clock_group_info * group_info){
  unsigned long flags;
  int32_t new_low=-1;
  //first, check if we're the lowest
  spin_lock_irqsave(&group_info->lock, flags);
  //make us inactive
  group_info->clocks[current->task_clock.tid].inactive=1;


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
  unsigned long flags;
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
  printk(KERN_EMERG "TASK CLOCK: activating %d\n", current->task_clock.tid);
#endif
  spin_lock_irqsave(&group_info->lock, flags);
  __set_base_ticks(group_info, current->task_clock.tid,group_info->lowest_ticks);
  group_info->clocks[current->task_clock.tid].inactive=0;
  group_info->clocks[current->task_clock.tid].waiting=0;
  group_info->clocks[current->task_clock.tid].sleeping=0;
  group_info->clocks[group_info->lowest_tid].event->hw.sample_period=0;
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
    if (group_info->clocks[id].initialized==0){
        //this is activating a new thread...so it needs to inherit the activating parent's clock + 1
        group_info->clocks[id].initialized=1;
        group_info->clocks[id].ticks=0;
        group_info->clocks[id].base_ticks=__get_clock_ticks(group_info, current->task_clock.tid) + 1;
        #if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED) && defined(DEBUG_TASK_CLOCK_FINE_GRAINED)
        printk(KERN_EMERG "--------TASK CLOCK: Thread %d is setting thread %d's clock to %llu\n", 
               current->task_clock.tid, id, __get_clock_ticks(group_info, current->task_clock.tid));
        #endif
    }
    group_info->clocks[id].inactive=0;
    group_info->clocks[id].waiting=0;
    group_info->clocks[id].sleeping=0;
    spin_unlock_irqrestore(&group_info->lock, flags);
}

//TODO:Need to remove this and from the kernel
void task_clock_on_wait(struct task_clock_group_info * group_info){ }

void task_clock_entry_wait(struct task_clock_group_info * group_info){
    unsigned long flags;
    spin_lock_irqsave(&group_info->lock, flags);
    __determine_lowest_and_notify_or_wait(group_info);
    spin_unlock_irqrestore(&group_info->lock, flags);
}

void task_clock_entry_sleep(struct task_clock_group_info * group_info){
    unsigned long flags;
    spin_lock_irqsave(&group_info->lock, flags);
    __determine_lowest_and_notify_or_wait(group_info);
    if (group_info->clocks[current->task_clock.tid].waiting){
        group_info->clocks[current->task_clock.tid].sleeping=1;
    }
    spin_unlock_irqrestore(&group_info->lock, flags);
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
}
