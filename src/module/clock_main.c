#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/path.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/task_clock.h>

MODULE_LICENSE("GPL");

#define get_task_clock() (current->task_clock)

#define task_clock_ticks() (current->task_clock.tick_arr)

#define task_clock_tid() (current->task_clock.tid)

void task_clock_overflow_handler(){
  printk(KERN_EMERG "Im in the overflow handler, \n");
  if (!access_ok(VERIFY_WRITE, task_clock_ticks(), sizeof(uint64_t))){
    printk(KERN_EMERG "in task_clock_overflow_handler: access is not ok\n");
    return;
  }
  //increment our tick count
  task_clock_ticks()[task_clock_tid()]++;
  printk(KERN_EMERG "ticks: %llu\n",   task_clock_ticks()[task_clock_tid()]);
}

int init_module(void)
{
  printk(KERN_EMERG "initializing module\n");
  task_clock_func.task_clock_overflow_handler=task_clock_overflow_handler;
  return 0;
}

void cleanup_module(void)
{
  printk(KERN_EMERG "cleanup module\n");
  task_clock_func.task_clock_overflow_handler=NULL;
}




