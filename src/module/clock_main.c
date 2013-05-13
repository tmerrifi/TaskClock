#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/path.h>
#include <linux/string.h>
#include <linux/task_clock.h>

MODULE_LICENSE("GPL");

void task_clock_overflow_handler(){
  printk(KERN_EMERG "Im in the overflow handler\n");
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
}




