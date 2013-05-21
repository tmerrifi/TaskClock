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
	pud = pud_offset(pgd, addr);
	if (!pud){
		goto error;
	}
	pmd = pmd_offset(pud, addr);
	if (!pmd){
		goto error;	
	}
	pte = pte_offset_map(pmd, addr);
	if (!pte){
		goto error;
	}

	printk(KERN_EMERG "PFN %lu pid %d\n", pte_pfn(*pte), current->pid);

	return pte;
	
	error:
		return NULL;
}


/*   int fd; */
/*   uint8_t waiting; */
/*   uint8_t inactive; */
/*   uint64_t ticks; */
/* }; */

/* struct task_clock_group_info{ */
/*   spinlock_t nmi_lock; */
/*   spinlock_t lock; */
/*   int32_t lowest_tid; */
/*   struct task_clock_entry_info clocks[TASK_CLOCK_MAX_THREADS]; */
/*   struct irq_work pending_work; */
/*   int pending; */
/* }; */

#define __inc_clock_ticks(group_info, tid) (group_info->clocks[tid].ticks)++

#define __get_clock_ticks(group_info, tid) (group_info->clocks[tid].ticks)

#define __clock_is_lower(group_info, tid1, tid2) ((group_info->clocks[tid1].ticks < group_info->clocks[tid2].ticks) \
						  || (group_info->clocks[tid1].ticks == group_info->clocks[tid2].ticks && (tid1 < tid2)))

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
  uint64_t min_ticks=1ULL<<64;
  int32_t min_tid=-1;
  for (;i<TASK_CLOCK_MAX_THREADS;++i){
    struct task_clock_entry_info * entry = &group_info->clocks[i];
    if (entry->fd >= 0 && !entry->inactive && (min_tid < 0 || __clock_is_lower(group_info, i, min_tid))){
      min_tid=i;
    }
  }
  return min_tid;
}

int32_t __new_lowest(struct task_clock_group_info * group_info, int32_t tid){
  int32_t new_low=-1;
  int32_t tmp;

  //it hasn't been initialized yet
  if (group_info->lowest_tid<0){
    new_low=tid;
  }
  //am I the current lowest?
  if (tid==group_info->lowest_tid && ((tmp=__search_for_lowest(group_info))!=tid) ){
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

void __task_clock_notify_waiting_threads(struct irq_work * work){
  unsigned long flags;
  struct task_clock_group_info * group_info = container_of(work, struct task_clock_group_info, pending_work);
  spin_lock_irqsave(&group_info->lock, flags);
  if (group_info->pending){
    //the lowest must be notified
    struct perf_event * event = group_info->clocks[group_info->lowest_tid].event;
    group_info->pending=0;
    //wake it up
    printk(KERN_EMERG "waking up %d\n", group_info->lowest_tid);
    wake_up_all(&event->waitq);
  }
  spin_unlock_irqrestore(&group_info->lock, flags);
}

void task_clock_overflow_handler(struct task_clock_group_info * group_info){
  unsigned long flags;
  int32_t new_low=-1;

  spin_lock_irqsave(&group_info->nmi_lock, flags);
  __inc_clock_ticks(group_info, current->task_clock.tid);
  new_low=__new_lowest(group_info, current->task_clock.tid);
  if (new_low >= 0){
    //there is a new lowest thread, make sure to set it
    group_info->lowest_tid=new_low;
    if (__new_low_is_waiting(group_info, new_low)){
      printk(KERN_EMERG " SIGNALING NEW LOW!\n");
      group_info->pending=1;
      irq_work_queue(&group_info->pending_work);
    }
  }
  spin_unlock_irqrestore(&group_info->nmi_lock, flags);
  //printk(KERN_EMERG " Ticks is %llu for pid %d\n", task_clock_ticks()[task_clock_tid()], current->pid);
}

//userspace is disabling the clock. Perhaps they are about to start waiting to be named the lowest. In that
//case, we need to figure out if they are the lowest and let them know before they call poll
void task_clock_on_disable(struct task_clock_group_info * group_info){
  unsigned long flags;
  spin_lock_irqsave(&group_info->lock, flags);
  //am I the lowest?
  if(group_info->lowest_tid == current->task_clock.tid){
    current->task_clock.user_status->lowest_clock=1;
  }
  spin_unlock_irqrestore(&group_info->lock, flags);
}

void __init_task_clock_entries(struct task_clock_group_info * group_info){
  int i=0;
  for (;i<TASK_CLOCK_MAX_THREADS;++i){
    struct task_clock_entry_info * entry = &group_info->clocks[i];
    entry->fd=-1;
    entry->waiting=0;
    entry->inactive=0;
    entry->ticks=(1<<63);
  }
}

void task_clock_entry_init(struct task_clock_group_info * group_info){
  //store the event pointer to use later
  struct perf_event * event = container_of(group_info, struct perf_event, task_clock_group);
  group_info->clocks[current->task_clock.tid].event=event;
}

struct task_clock_group_info * task_clock_group_init(){
  struct task_clock_group_info * group_info = kmalloc(sizeof(struct task_clock_group_info), GFP_KERNEL);
  spin_lock_init(&group_info->nmi_lock);
  spin_lock_init(&group_info->lock);
  group_info->lowest_tid=-1;
  group_info->pending=0;
  __init_task_clock_entries(group_info);
  init_irq_work(&group_info->pending_work, __task_clock_notify_waiting_threads);
  return group_info;
}

int init_module(void)
{
  printk(KERN_EMERG "initializing module\n");
  task_clock_func.task_clock_overflow_handler=task_clock_overflow_handler;
  task_clock_func.task_clock_group_init=task_clock_group_init;
  return 0;
}

void cleanup_module(void)
{
  printk(KERN_EMERG "cleanup module\n");
  task_clock_func.task_clock_overflow_handler=NULL;
}




