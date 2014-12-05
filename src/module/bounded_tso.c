#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/sched.h>
#include <linux/task_clock.h>
#include <linux/ptrace.h>
#include <asm/page_types.h>
#include <linux/hardirq.h>

#include "utility.h"
#include "bounded_tso.h"

MODULE_LICENSE("GPL");

void begin_bounded_memory_fence( struct task_clock_group_info * group_info){
    uint64_t remaining_ticks;
    struct pt_regs *regs;

    //first things first (Imma realist)...we need to single step to get to the right logical clock value
    if ( __tick_counter_is_running(group_info) &&
         (remaining_ticks=BOUNDED_CHUNK_SIZE - __get_chunk_ticks(group_info, __current_tid()))>0){

        if (remaining_ticks > BOUNDED_CHUNK_SIZE){
            return;
        }

        //nmi will have to do this later using bounded_memory_fence_turn_on_tf
        if (!in_nmi()){
            task_pt_regs(current)->flags |= X86_EFLAGS_TF;
        }

        __single_stepping_enable(group_info, __current_tid(), remaining_ticks);

        /*printk(KERN_EMERG "Starting bounded fence...tid: %d, chunks %llu, left %llu, ticks %llu, pid: %d\n", 
               __current_tid(),
               __get_chunk_ticks(group_info, __current_tid()),
               remaining_ticks,
               group_info->clocks[__current_tid()].count_ticks,
               current->pid);
        
        printk(KERN_EMERG "left %llu, ticks %llu\n", 
               remaining_ticks,
               group_info->clocks[__current_tid()].count_ticks);*/
        
        
    }
}

void bounded_memory_fence_turn_on_tf(struct task_clock_group_info * group_info, struct pt_regs * regs){
    regs->flags |= X86_EFLAGS_TF;
}


int on_single_step(struct task_clock_group_info * group_info, struct pt_regs *regs){

    int result=0;
    if (__single_stepping_on(group_info, __current_tid())){
        //dec by 1 the number of instructions we have left to single step
        __single_stepping_dec(group_info, __current_tid());
        //add one to our clock
        __inc_clock_ticks_from_single_step(group_info, __current_tid(), 1);
        if (__single_stepping_done(group_info, __current_tid())){
            //set the trap flag
            regs->flags &= ~X86_EFLAGS_TF;
            //set_tsk_thread_flag(current, TIF_SINGLESTEP);
            //we do a double decrement, since we added an extra 1 to start with
            __single_stepping_dec(group_info, __current_tid());
            //turn off the counter since we are about to signal the process and we don't want to count
            //stuff that happens in user space. It could be that the signal processing and such is all
            //deterministic but why take that chance?
            __tick_counter_turn_off(group_info);
            __hit_bounded_fence_enable();
            //send a signal to the process
            force_sig(SIGUSR1, current);
            regs->flags &= ~X86_EFLAGS_TF;
            
        }
        result=1;
    }
    else if (__hit_bounded_fence()){
        result=1;
    }
    else if (__get_chunk_ticks(group_info, __current_tid()) < BOUNDED_CHUNK_SIZE){
        //we're still single stepping for no good reason. Perhaps the eflags register wasn't
        //updated after the trap
        regs->flags &= ~X86_EFLAGS_TF;
        result=1;
    }

    return result;

}
