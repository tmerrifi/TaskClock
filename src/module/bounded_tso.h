#ifndef BOUNDED_TSO_H

#define BOUNDED_TSO_H

//this is our estimate of how "fuzzy" the performance counter overflows are. By setting
//the counter to n-IMPRECISE_OVERFLOW_BUFFER, we are certain that the overflow will
//arrive where count<n. In reality, the accuracy of IMPRECISE_OVERFLOW_BUFFER will (anecdotally)
//be determined by the size of the reorder buffer.
#define IMPRECISE_OVERFLOW_BUFFER (200)

//This determines the maximum store buffer size. When we hit BOUNDED_CHUNK_SIZE instructions,
//we force a commit.
#define BOUNDED_CHUNK_SIZE (1024000000)

#define IMPRECISE_BOUNDED_CHUNK_SIZE (BOUNDED_CHUNK_SIZE - IMPRECISE_OVERFLOW_BUFFER)

void begin_bounded_memory_fence(struct task_clock_group_info * group_info);

int on_single_step(struct task_clock_group_info * group_info, struct pt_regs *regs);

void bounded_memory_fence_turn_on_tf(struct task_clock_group_info * group_info, struct pt_regs * regs);

void end_bounded_memory_fence_early(struct task_clock_group_info * group_info);

#define __get_chunk_ticks(group_info, tid) (group_info->clocks[tid].chunk_ticks)

#define __reset_chunk_ticks(group_info, tid) (group_info->clocks[tid].chunk_ticks=0)

#ifdef USE_BOUNDED_FENCE
//using the bounded fence
#define __inc_chunk_ticks(group_info, tid, val) \
    group_info->clocks[tid].chunk_ticks+=val; \
    if (__get_chunk_ticks(group_info, tid) > IMPRECISE_BOUNDED_CHUNK_SIZE){ \
        begin_bounded_memory_fence(group_info);                         \
    }

#define __hit_bounded_fence() \
    (current->task_clock.user_status->hit_bounded_fence)

#define __hit_bounded_fence_enable() \
    current->task_clock.user_status->hit_bounded_fence=1

#define __hit_bounded_fence_disable() \
    current->task_clock.user_status->hit_bounded_fence=0

#define __bound_overflow_period(group_info, tid, period)\
    ((period + __get_chunk_ticks(group_info, tid) > IMPRECISE_BOUNDED_CHUNK_SIZE) ? (IMPRECISE_BOUNDED_CHUNK_SIZE - __get_chunk_ticks(group_info,tid)) : period)


#define __single_stepping_dec(group_info, tid)      \
    group_info->clocks[tid].count_ticks--    

#define __single_stepping_done(group_info, tid)      \
    (group_info->clocks[tid].count_ticks<=2)

//we reuse the "count_ticks" variable to track how many ticks we need to single step
#define __single_stepping_enable(group_info, tid, val)      \
    group_info->clocks[tid].count_ticks=val+2

#define __single_stepping_on(group_info, tid) \
    (group_info->clocks[tid].count_ticks>1)

#define __single_stepping_reset(group_info, tid)\
    group_info->clocks[tid].count_ticks=0

#else
//no bounded fence
#define __inc_chunk_ticks(group_info, tid, val) \
    group_info->clocks[tid].chunk_ticks+=val;

#define __bound_overflow_period(group_info, tid, period)\
    (period)


#define __hit_bounded_fence() \
    (0)

#define __hit_bounded_fence_enable()

#define __hit_bounded_fence_disable()

#define __single_stepping_dec(group_info, tid)      

#define __single_stepping_done(group_info, tid) (0)

//we reuse the "count_ticks" variable to track how many ticks we need to single step
#define __single_stepping_enable(group_info, tid, val)      

#define __single_stepping_on(group_info, tid) \
    (0)

#define __single_stepping_reset(group_info, tid)

#endif

#endif
