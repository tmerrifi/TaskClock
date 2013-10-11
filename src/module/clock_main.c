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


MODULE_LICENSE("GPL");

#define __set_base_ticks(group_info, tid, val) (group_info->clocks[tid].base_ticks=val)

#define __get_base_ticks(group_info, tid) (group_info->clocks[tid].base_ticks)

//TODO: get rid of that div. by 100
#define __update_clock_ticks(group_info, tid)                            \
    long rawcount = local64_read(&group_info->clocks[tid].event->count) / 1000; \
    group_info->clocks[tid].ticks=rawcount;

#define __set_clock_ticks(group_info, tid, val) (group_info->clocks[tid].ticks=val)

#define __get_clock_ticks(group_info, tid) (group_info->clocks[tid].ticks + group_info->clocks[tid].base_ticks)


#define __clock_is_lower(group_info, tid1, tid2) ((__get_clock_ticks(group_info, tid1) < __get_clock_ticks(group_info, tid2)) \
						  || (__get_clock_ticks(group_info, tid1) == __get_clock_ticks(group_info, tid2) && (tid1 < tid2)))

//#define __clock_is_lower(group_info, tid1, tid2) ((group_info->clocks[tid1].ticks < group_info->clocks[tid2].ticks) \
//						  || (group_info->clocks[tid1].ticks == group_info->clocks[tid2].ticks && (tid1 < tid2)))

//is this current tick_count the lowest
int __is_lowest(struct task_clock_group_info * group_info, int32_t tid){
  if (tid==group_info->lowest_tid ||
      __get_clock_ticks(group_info, tid) < __get_clock_ticks(group_info, group_info->lowest_tid)){
    return 1;
  }
  return 0;
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
    printk(KERN_EMERG " tid: %d ticks %d inactive %d init %d::", i, __get_clock_ticks(group_info, i), entry->inactive, entry->initialized);
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

int32_t __new_low_is_waiting(struct task_clock_group_info * group_info, int32_t tid){
  return (tid >= 0 && group_info->clocks[tid].waiting);
}

void __wake_up_waiting_thread(struct perf_event * event){
  struct perf_buffer * buffer;
  rcu_read_lock();
  buffer = rcu_dereference(event->buffer);
  atomic_set(&buffer->poll, POLL_IN);
  rcu_read_unlock();
  wake_up_all(&event->task_clock_waitq);
}

void __task_clock_notify_waiting_threads(struct irq_work * work){
  unsigned long flags;
  struct task_clock_group_info * group_info = container_of(work, struct task_clock_group_info, pending_work);
  spin_lock_irqsave(&group_info->lock, flags);

#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
  printk(KERN_EMERG "TASK CLOCK: beginning notification of %d\n", group_info->lowest_tid);
#endif

  if (group_info->notification_needed || group_info->nmi_new_low){
      group_info->nmi_new_low=0;
      group_info->notification_needed=0;
      //the lowest must be notified
      struct perf_event * event = group_info->clocks[group_info->lowest_tid].event;
      //__debug_print(group_info);
      __wake_up_waiting_thread(event);
  }
  else{
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED) && defined(DEBUG_TASK_CLOCK_FINE_GRAINED)
      printk(KERN_EMERG "TASK CLOCK: notification of %d didn't happen because of notification_needed\n", group_info->lowest_tid);
#endif
  }


  spin_unlock_irqrestore(&group_info->lock, flags);
}

void task_clock_overflow_handler(struct task_clock_group_info * group_info){
  unsigned long flags;
  int32_t new_low=-1;

  spin_lock_irqsave(&group_info->nmi_lock, flags);
  __update_clock_ticks(group_info, current->task_clock.tid);
  new_low=__new_lowest(group_info, current->task_clock.tid);


#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
  printk(KERN_EMERG "TASK CLOCK: overflow id %d ticks %llu\n", current->task_clock.tid, __get_clock_ticks(group_info, current->task_clock.tid));
#endif

  if (new_low >= 0 && new_low != current->task_clock.tid && group_info->nmi_new_low==0){
      group_info->nmi_new_low=1;
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
      printk(KERN_EMERG "TASK CLOCK: new low %d ticks %llu\n", new_low, __get_clock_ticks(group_info, new_low));
#endif
      //there is a new lowest thread, make sure to set it
      group_info->lowest_tid=new_low;
      if (__new_low_is_waiting(group_info, new_low)){
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED) && defined(DEBUG_TASK_CLOCK_FINE_GRAINED)
          printk(KERN_EMERG "----TASK CLOCK: notifying %d ticks %llu\n", new_low, __get_clock_ticks(group_info, new_low));
#endif
          irq_work_queue(&group_info->pending_work);
      }
  }
  spin_unlock_irqrestore(&group_info->nmi_lock, flags);
}

//userspace is disabling the clock. Perhaps they are about to start waiting to be named the lowest. In that
//case, we need to figure out if they are the lowest and let them know before they call poll
void task_clock_on_disable(struct task_clock_group_info * group_info){
  unsigned long flags;

  //TODO: Why are we disabling interrupts here?
  spin_lock_irqsave(&group_info->lock, flags);

  //am I the lowest?
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
  printk(KERN_EMERG "TASK CLOCK: disabling %d...lowest is %d lowest clock is %d pid %d\n", 
         current->task_clock.tid, group_info->lowest_tid, current->task_clock.user_status->lowest_clock, current->pid);
#endif

  //update our clock in case some of the instructions weren't counted in the overflow handler
  __update_clock_ticks(group_info, current->task_clock.tid);
  //base ticks need to be set to our current tick value, so we can use them next time
  __set_base_ticks(group_info, current->task_clock.tid, __get_clock_ticks(group_info, current->task_clock.tid));
  //now set the clock ticks to zero...since base_ticks is updated
  __set_clock_ticks(group_info, current->task_clock.tid, 0);

  int32_t oldlow = group_info->lowest_tid;

  int32_t new_low=__new_lowest(group_info, current->task_clock.tid);
  if (new_low >=0 && new_low!=group_info->lowest_tid){
      group_info->notification_needed=1;
      group_info->lowest_tid=new_low;
  }

  if (__get_clock_ticks(group_info, current->task_clock.tid)==2040){
      __debug_print(group_info);
      printk(KERN_EMERG "id %d and new_low %d and current lowest %d\n", current->task_clock.tid, new_low, group_info->lowest_tid);
  }
  
  if (group_info->lowest_tid == current->task_clock.tid){
      current->task_clock.user_status->lowest_clock=1;
      group_info->notification_needed=0;
  }
  else{
      current->task_clock.user_status->lowest_clock=0;
      if (__new_low_is_waiting(group_info, group_info->lowest_tid) && group_info->notification_needed){
          group_info->nmi_new_low=0;
          group_info->notification_needed=0;
          //the lowest must be notified
          struct perf_event * event = group_info->clocks[group_info->lowest_tid].event;
          __wake_up_waiting_thread(event);
      }
      //after we return from this function we'll have to call poll(), so we can set the waiting flag now
      group_info->clocks[current->task_clock.tid].waiting=1;
  }
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED) && defined(DEBUG_TASK_CLOCK_FINE_GRAINED)
  printk(KERN_EMERG "--------TASK CLOCK: setting user clock ticks to %llu, \n", __get_clock_ticks(group_info, current->task_clock.tid));
#endif

  current->task_clock.user_status->ticks=__get_clock_ticks(group_info, current->task_clock.tid);
  //we need to store away the current ticks in our base_ticks field...so that next time around this 
  //info will persist

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
    //are we the lowest?
    if (group_info->lowest_tid==current->task_clock.tid){
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED) && defined(DEBUG_TASK_CLOCK_FINE_GRAINED)
        printk(KERN_EMERG "----TASK CLOCK: ENABLING %d and setting notification to 1 %d\n", current->task_clock.tid);
#endif
        group_info->notification_needed=1;
        group_info->nmi_new_low=0;
    }

    spin_unlock_irqrestore(&group_info->lock, flags);

    //we need to store away the current ticks in our base_ticks field...so that next time around this 
    //info will persist
    if (__get_base_ticks(group_info, current->task_clock.tid)==0){
        __set_base_ticks(group_info, current->task_clock.tid, __get_clock_ticks(group_info, current->task_clock.tid));
    }
}

void __init_task_clock_entries(struct task_clock_group_info * group_info){
  int i=0;
  for (;i<TASK_CLOCK_MAX_THREADS;++i){
    struct task_clock_entry_info * entry = &group_info->clocks[i];
    entry->initialized=0;
    entry->waiting=0;
    entry->inactive=0;
    entry->ticks=(1ULL<<63);
  }
}

void task_clock_entry_init(struct task_clock_group_info * group_info, struct perf_event * event){
    if (group_info->clocks[current->task_clock.tid].initialized==0){
        group_info->clocks[current->task_clock.tid].ticks=0;
        group_info->clocks[current->task_clock.tid].base_ticks=0;
    }
    group_info->clocks[current->task_clock.tid].initialized=1;
    group_info->clocks[current->task_clock.tid].event=event;
    group_info->clocks[current->task_clock.tid].inactive=0;
    group_info->clocks[current->task_clock.tid].waiting=0;

#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
  printk(KERN_EMERG "TASK CLOCK: INIT, tid %d event is %p....ticks are %llu \n", current->task_clock.tid, event, __get_clock_ticks(group_info, current->task_clock.tid));
#endif
}

struct task_clock_group_info * task_clock_group_init(void){
  struct task_clock_group_info * group_info = kmalloc(sizeof(struct task_clock_group_info), GFP_KERNEL);
  spin_lock_init(&group_info->nmi_lock);
  spin_lock_init(&group_info->lock);
  group_info->lowest_tid=-1;
  group_info->notification_needed=1;
  group_info->nmi_new_low=0;
  __init_task_clock_entries(group_info);
  init_irq_work(&group_info->pending_work, __task_clock_notify_waiting_threads);
  return group_info;
}

void task_clock_entry_halt(struct task_clock_group_info * group_info){
  unsigned long flags;
  int32_t new_low=-1;
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED)
  printk(KERN_EMERG "TASK CLOCK: HALTING %d, lowest? %d\n", current->task_clock.tid, group_info->lowest_tid);
#endif
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
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED) && defined(DEBUG_TASK_CLOCK_FINE_GRAINED)
      printk(KERN_EMERG "----TASK CLOCK: HALTING %d, setting new low to %d, new low waiting? %d notification %d\n", 
             current->task_clock.tid, group_info->lowest_tid, (new_low>=0) ? __new_low_is_waiting(group_info, new_low) : 0, group_info->notification_needed);
#endif
      if (new_low >= 0 && __new_low_is_waiting(group_info, new_low) && group_info->notification_needed){
          //lets wake it up
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED) && defined(DEBUG_TASK_CLOCK_FINE_GRAINED)
          printk(KERN_EMERG "--------TASK CLOCK: HALTING %d SIGNALING NEW LOW!\n", current->task_clock.tid);
#endif
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
  group_info->clocks[current->task_clock.tid].inactive=0;
  group_info->clocks[current->task_clock.tid].waiting=0;
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
        group_info->clocks[id].ticks=group_info->clocks[current->task_clock.tid].ticks + 1;
        group_info->clocks[id].base_ticks=0;
#if defined(DEBUG_TASK_CLOCK_COARSE_GRAINED) && defined(DEBUG_TASK_CLOCK_FINE_GRAINED)
        printk(KERN_EMERG "--------TASK CLOCK: Thread %d is setting thread %d's clock to %llu\n", current->task_clock.tid, id, group_info->clocks[id].ticks);
#endif
    }
    group_info->clocks[current->task_clock.tid].inactive=0;
    group_info->clocks[current->task_clock.tid].waiting=0;

    spin_unlock_irqrestore(&group_info->lock, flags);
}

//TODO:Need to remove this and from the kernel
void task_clock_on_wait(struct task_clock_group_info * group_info){ }

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
}
