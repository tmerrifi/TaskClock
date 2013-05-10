#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/path.h>
#include <linux/string.h>

MODULE_LICENSE("GPL");

int init_module(void)
{
  printk(KERN_EMERG "initializing module\n");
  return 0;
}

void cleanup_module(void)
{
  printk(KERN_EMERG "cleanup module\n");
}




