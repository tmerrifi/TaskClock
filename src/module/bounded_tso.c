
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
#include <linux/sched.h>
#include <linux/task_clock.h>
#include <linux/ptrace.h>
#include <asm/page_types.h>
#include <linux/hardirq.h>
#include <linux/perf_event.h>

#include "utility.h"
#include "bounded_tso.h"

MODULE_LICENSE("GPL");

void begin_bounded_memory_fence( struct task_clock_group_info * group_info){
    uint64_t remaining_ticks;
    struct pt_regs *regs;

    remaining_ticks=(__get_chunk_ticks(group_info, __current_tid()) > BOUNDED_CHUNK_SIZE) ?
        0 : BOUNDED_CHUNK_SIZE - __get_chunk_ticks(group_info, __current_tid());
    //first things first (imma realist), make sure the tick counter is running (paranoia?)
    if ( __tick_counter_is_running(group_info)){
        //if we hit the chunk length on the nose, we have to terminate the chunk now and avoid single stepping
        if (remaining_ticks==0){
            __tick_counter_turn_off(group_info);
            __hit_bounded_fence_enable();
            //send a signal to the process
            force_sig(SIGUSR1, current);
        }
        else if (remaining_ticks>0){
            //nmi will have to do this later using bounded_memory_fence_turn_on_tf, since they use a different stack.
            if (!in_nmi()){
                task_pt_regs(current)->flags |= X86_EFLAGS_TF;
            }
            //we're single stepping baby!!!
            __single_stepping_enable(group_info, __current_tid(), remaining_ticks);
        }
    }
}

//For nmi context we need to pass the registers in
void bounded_memory_fence_turn_on_tf(struct task_clock_group_info * group_info, struct pt_regs * regs){
    regs->flags |= X86_EFLAGS_TF;
}

//this function gets called when the userspace program is butting up against the chunk boundary, we've
//turned on the single stepping...but then they finish the chunk. Now we need to turn off the trap
//flag and clean up the rest of our mess.
void end_bounded_memory_fence_early(struct task_clock_group_info * group_info){
    //because we're in process context, we can do this safely.
    task_pt_regs(current)->flags &= ~X86_EFLAGS_TF;
    //just in case the trap flag doesn't remove cleanly...I've seen extra calls into on_single_step
    __hit_bounded_fence_enable();
    //turn off single stepping
    __single_stepping_reset(group_info, __current_tid());
}

int on_single_step(struct task_clock_group_info * group_info, struct pt_regs *regs){

    int result=0;
    if (__single_stepping_on(group_info, __current_tid())){
        //dec by 1 the number of instructions we have left to single step
        __single_stepping_dec(group_info, __current_tid());
        //add one to our clock
        __inc_clock_ticks_no_chunk_add(group_info, __current_tid(), 1);
        if (__single_stepping_done(group_info, __current_tid())){
            //set the trap flag
            regs->flags &= ~X86_EFLAGS_TF;
            //we do a double decrement, since we added an extra 1 to start with
            __single_stepping_dec(group_info, __current_tid());
            //turn off the counter since we are about to signal the process and we don't want to count
            //stuff that happens in user space. It could be that the signal processing and such is all
            //deterministic but why take that chance?
            __tick_counter_turn_off(group_info);
            __hit_bounded_fence_enable();
            //send a signal to the process
            force_sig(SIGUSR1, current);
            //paranoia
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
