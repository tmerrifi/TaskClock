
#ifndef LOGICAL_CLOCK_H
#define LOGICAL_CLOCK_H

#include "bounded_tso.h"

//whats the max overflow period
#define MAX_CLOCK_SAMPLE_PERIOD 200000

//whats the minimum overflow period
#define MIN_CLOCK_SAMPLE_PERIOD 1024

//The value bits in the counter...this is very much model specific
#define X86_CNT_VAL_BITS 48

#define X86_CNT_VAL_MASK (1ULL << X86_CNT_VAL_BITS) - 1

static inline void logical_clock_update_clock_ticks(struct task_clock_group_info * group_info, int tid){
    unsigned long rawcount = local64_read(&group_info->clocks[tid].event->count); 
    group_info->clocks[tid].debug_last_overflow_ticks=rawcount;
    __inc_clock_ticks(group_info, tid, rawcount);
    local64_set(&group_info->clocks[tid].event->count, 0);
}

//utility function to read the performance counter
static inline void __read_performance_counter(struct hw_perf_event * hwc, uint64_t * _new_raw_count, uint64_t * _prev_raw_count, uint64_t * _new_pmc){
    uint64_t new_raw_count, prev_raw_count, new_pmc;

    DECLARE_ARGS(val, low, high);

 again:    
    //read the raw counter
    rdmsrl(hwc->event_base + hwc->idx, new_raw_count);

    asm volatile("rdpmc" : EAX_EDX_RET(val, low, high) : "c" (hwc->idx));
    new_pmc=EAX_EDX_VAL(val, low, high);
    

    //get previous count
    prev_raw_count = local64_read(&hwc->prev_count);
    //in case an NMI fires while we're doing this...unlikely but who knows?!
    if (local64_cmpxchg(&hwc->prev_count, prev_raw_count, new_raw_count) != prev_raw_count){
        goto again;
    }
    //set the arguments to the values read
    *_new_raw_count=new_raw_count;
    *_prev_raw_count=prev_raw_count;
    *_new_pmc=new_pmc;
}

static inline void logical_clock_read_clock_and_update(struct task_clock_group_info * group_info, int id){
    uint64_t new_raw_count, prev_raw_count, new_pmc;
    int64_t delta;
    //for our version of perf counters (v3 for Intel) this works...probably not for anything else
    //ARCH_DEP_TODO
    int shift = 64 - X86_CNT_VAL_BITS;
    struct hw_perf_event * hwc = &group_info->clocks[id].event->hw;

    //lets check to make sure the counter is currently on, otherwise we'll still read the counter but throw away the ticks we accrued
    int counter_was_on = __tick_counter_is_running(group_info);
    //read the counters using rdmsr
    __read_performance_counter(hwc, &new_raw_count, &prev_raw_count, &new_pmc);
    //if this succeeds, then its safe to turn off the tick_counter...meaning we no longer do work inside the overflow handler.
    //Even if an NMI beats us to it...it won't have any work to do since prev_raw_count==new_raw_count after the cmpxchg
    __tick_counter_turn_off(group_info);
    //compute how much the counter has counted. The shift is used since only the first N (currently hard-coded to 48) bits matter
    delta = (new_raw_count << shift) - (prev_raw_count << shift);
    //shift it back to get the actually correct number
    delta >>= shift;
    //now add the event count in...why you may ask...Because the event's count field may have ticks we missed...I believe this is primarily
    //due to context switches.
    delta+=local64_read(&group_info->clocks[id].event->count);
    /*int cpu=smp_processor_id();
    u64 total=arch_irq_stat_cpu(cpu);
    if (id==1){
        printk(KERN_EMERG "update 2...delta: %lld id: %d current clock: %llu irqs: %d count: %llu cpu %d \n", 
               delta, id , __get_clock_ticks(group_info, id), total, local64_read(&group_info->clocks[id].event->count), cpu);
               }*/
    local64_set(&group_info->clocks[id].event->count, 0);
    if (counter_was_on){
        //add it to our current clock
        __inc_clock_ticks(group_info, id, delta);
        //let userspace see it
        current->task_clock.user_status->ticks=__get_clock_ticks(group_info, current->task_clock.tid);
    }
}

static inline void logical_clock_reset_current_ticks(struct task_clock_group_info * group_info, int id){
    uint64_t new_raw_count, prev_raw_count, new_pmc;
    //we can just read the counter...since that will effectively reset it
    __read_performance_counter(&group_info->clocks[id].event->hw, &new_raw_count, &prev_raw_count, &new_pmc);
    //reset the event's counter to 0
    local64_set(&group_info->clocks[id].event->count, 0);
}

static inline void logical_clock_set_perf_counter_max(struct task_clock_group_info * group_info, int id){
    struct hw_perf_event * hwc = &group_info->clocks[id].event->hw;
    int64_t val = (1ULL << 31) - 1;
    wrmsrl(hwc->event_base + hwc->idx, ((uint64_t)(-val) & X86_CNT_VAL_MASK));
    wrmsrl(hwc->event_base + hwc->idx, ((uint64_t)(-val) & X86_CNT_VAL_MASK));
    
    local64_set(&hwc->period_left, val);
    group_info->clocks[id].event->hw.sample_period=val;
    local64_set(&group_info->clocks[id].event->count, 0);
    local64_set(&hwc->prev_count, (u64)-val);

    //what did we set the counter to?
    group_info->clocks[id].event->last_written = (uint64_t)(-val);
}

static inline void logical_clock_set_perf_counter(struct task_clock_group_info * group_info, int id){
    struct hw_perf_event * hwc = &group_info->clocks[id].event->hw;
    int64_t val = group_info->clocks[id].event->hw.sample_period;
    wrmsrl(hwc->event_base + hwc->idx, ((uint64_t)(-val) & X86_CNT_VAL_MASK));
    wrmsrl(hwc->event_base + hwc->idx, ((uint64_t)(-val) & X86_CNT_VAL_MASK));
    
    local64_set(&hwc->period_left, val);
    local64_set(&group_info->clocks[id].event->count, 0);
    local64_set(&hwc->prev_count, (u64)-val);
    //what did we set the counter to?
    group_info->clocks[id].event->last_written = (uint64_t)(-val);
}


static inline void logical_clock_update_overflow_period(struct task_clock_group_info * group_info, int id){
#ifdef USE_ADAPTIVE_OVERFLOW_PERIOD
    uint64_t lowest_waiting_tid_clock, myclock, new_sample_period;
    int32_t lowest_waiting_tid=0;    

    new_sample_period=group_info->clocks[id].event->hw.sample_period+10000;
    lowest_waiting_tid = __search_for_lowest_waiting_exclude_current(group_info, id);
    if (lowest_waiting_tid>=0){
        lowest_waiting_tid_clock = __get_clock_ticks(group_info,lowest_waiting_tid);
        myclock = __get_clock_ticks(group_info,id);
        //if there is a waiting thread, and its clock is larger than ours, stop when we get there
        if (lowest_waiting_tid_clock > myclock){
            new_sample_period=__max(lowest_waiting_tid_clock - myclock + 1000, MIN_CLOCK_SAMPLE_PERIOD);
        }
    }
    group_info->clocks[id].debug_last_sample_period = group_info->clocks[id].event->hw.sample_period;
    group_info->clocks[id].event->hw.sample_period =  __min(__bound_overflow_period(group_info, id, new_sample_period), MAX_CLOCK_SAMPLE_PERIOD);
#endif
}

static inline void logical_clock_reset_overflow_period(struct task_clock_group_info * group_info, int id){
#ifdef USE_ADAPTIVE_OVERFLOW_PERIOD
    group_info->clocks[current->task_clock.tid].event->hw.sample_period=0;
    local64_set(&group_info->clocks[current->task_clock.tid].event->hw.period_left,0);
#endif
}




#endif
