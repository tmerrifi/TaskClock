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


MODULE_LICENSE("GPL");

//#define FIRST_BIT_SET 0x8000000000000000

#define get_task_clock() (current->task_clock)

#define task_clock_ticks() (current->task_clock.clocks)

#define task_clock_tid() (current->task_clock.tid)

/*#define set_first_bit(x) (x | FIRST_BIT_SET)

#define clear_first_bit(x) (x & ~(FIRST_BIT_SET))

#define first_bit_set(x) (x & FIRST_BIT_SET)

void task_clock_find_lowest(uint64_t * ticks){
  int i;
  uint64_t min_ticks=~(0x0);
  uint64_t tmp_ticks, new_val;
  uint32_t min_tid;
  uint8_t bit_set;

  while(1){
    i=0;
    for (;i<TASK_CLOCK_MAX_THREADS;++i){
      if (clear_first_bit(ticks[i]) < min_ticks){
	min_ticks=clear_first_bit(ticks[i]);
	min_tid=i;
      }
    }
    //compute new value
    new_val=set_first_bit(min_ticks);
    //was there any trickery here? For example, this thread may have removed themselves from contention
    if (atomic64_cmpxchg((atomic64_t *)(ticks + min_td), min_ticks, new_val) == min_ticks){
      break;
    }
  }
  }*/


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

void __task_clock_notify_waiting_threads(struct irq_work * work){
  printk(KERN_EMERG "NOTIFY!!!\n");
}

void task_clock_overflow_handler(int is_nmi){
  task_clock_ticks()[task_clock_tid()]++;
  //printk(KERN_EMERG " Ticks is %llu for pid %d\n", task_clock_ticks()[task_clock_tid()], current->pid);
}

struct task_clock_group_info * task_clock_group_init(){
  struct task_clock_group_info * group_info = kmalloc(sizeof(struct task_clock_group_info), GFP_KERNEL);
  spin_lock_init(&group_info->lock);
  group_info->lowest_tid=-1;
  init_irq_work(group_info->pending_work, __task_clock_notify_waiting_threads);
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




