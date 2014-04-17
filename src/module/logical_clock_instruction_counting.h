
#ifndef LOGICAL_CLOCK_H
#define LOGICAL_CLOCK_H

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

    if (tid==0 && tid==1){

        printk(KERN_EMERG " HUH 2 ????? %lld id %d current clock %llu userspace addr %p userspace %llu\n", 
               rawcount, tid , __get_clock_ticks(group_info, tid), &current->task_clock.user_status->ticks, current->task_clock.user_status->ticks);

    }

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

static inline uint64_t logical_clock_raw_read_pmc(struct task_clock_group_info * group_info, int id){
    uint64_t raw_val;
    DECLARE_ARGS(val, low, high);
    struct hw_perf_event * hwc = &group_info->clocks[id].event->hw;
    asm volatile("rdpmc" : EAX_EDX_RET(val, low, high) : "c" (hwc->idx));
    raw_val=EAX_EDX_VAL(val, low, high);
    return ((raw_val << (64 - X86_CNT_VAL_BITS)) >> (64 - X86_CNT_VAL_BITS));
}

static inline void logical_clock_read_clock_and_update(struct task_clock_group_info * group_info, int id){
    uint64_t new_raw_count, prev_raw_count, new_pmc;
    int64_t delta;
    //for our version of perf counters (v3 for Intel) this works...probably not for anything else
    //ARCH_DEP_TODO
    int shift = 64 - X86_CNT_VAL_BITS;
    struct hw_perf_event * hwc = &group_info->clocks[id].event->hw;
    //read the counters using rdmsr
    __read_performance_counter(hwc, &new_raw_count, &prev_raw_count, &new_pmc);
    //if this succeeds, then its safe to turn off the tick_counter...meaning we no longer do work inside the overflow handler.
    //Even if an NMI beats us to it...it won't have any work to do since prev_raw_count==new_raw_count after the cmpxchg
    __tick_counter_turn_off(group_info);
    //compute how much the counter has counted. The shift is used since only the first N (currently hard-coded to 48) bits matter
    delta = (new_raw_count << shift) - (prev_raw_count << shift);
    //shift it back to get the actually correct number
    delta >>= shift;
    if (id==0 && id==1){
        printk(KERN_EMERG " HUH????? %lld id %d current clock %llu userspace addr %p userspace %llu\n", 
               delta, id , __get_clock_ticks(group_info, id), &current->task_clock.user_status->ticks, current->task_clock.user_status->ticks);
    }
    //printk(KERN_EMERG " raw counter: %llu %llu %llu %llu idx %d tid %d new prev %llu\n", 
    //       ((new_raw_count << shift)>>shift), ((new_pmc << shift)>>shift), ((prev_raw_count << shift)>>shift), delta, hwc->idx, __current_tid(), local64_read(&hwc->prev_count));
    //add it to our current clock
    __inc_clock_ticks(group_info, id, delta);
    //let userspace see it
    current->task_clock.user_status->ticks=__get_clock_ticks(group_info, current->task_clock.tid);
}

static inline void logical_clock_reset_current_ticks(struct task_clock_group_info * group_info, int id){
    uint64_t new_raw_count, prev_raw_count, new_pmc;
    //we can just read the counter...since that will effectively reset it
    __read_performance_counter(&group_info->clocks[id].event->hw, &new_raw_count, &prev_raw_count, &new_pmc);
    //reset the event's counter to 0
    local64_set(&group_info->clocks[id].event->count, 0);
}

static inline logical_clock_set_perf_counter_max(struct task_clock_group_info * group_info, int id){
    struct hw_perf_event * hwc = &group_info->clocks[id].event->hw;
    int64_t val = (1ULL << 31) - 1;
    wrmsrl(hwc->event_base + hwc->idx, ((uint64_t)(-val) & X86_CNT_VAL_MASK));
    //what did we set the counter to?
    //printk(KERN_EMERG " new counter: %llu\n", (uint64_t)(-val) & X86_CNT_VAL_MASK);

}

static inline void logical_clock_update_overflow_period(struct task_clock_group_info * group_info, int id){
#ifdef USE_ADAPTIVE_OVERFLOW_PERIOD
    uint64_t lowest_waiting_tid_clock, myclock, new_sample_period;
    int32_t lowest_waiting_tid=0;    

    if (group_info->clocks[id].event){
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
        group_info->clocks[id].event->hw.sample_period =  __min(new_sample_period, MAX_CLOCK_SAMPLE_PERIOD);
    }
#endif
}

static inline void logical_clock_reset_overflow_period(struct task_clock_group_info * group_info, int id){
#ifdef USE_ADAPTIVE_OVERFLOW_PERIOD
    group_info->clocks[current->task_clock.tid].event->hw.sample_period=0;
    local64_set(&group_info->clocks[current->task_clock.tid].event->hw.period_left,0);
#endif
}




#endif
