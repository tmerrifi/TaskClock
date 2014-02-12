
#ifndef LOGICAL_CLOCK_H
#define LOGICAL_CLOCK_H

//whats the max overflow period
#define MAX_CLOCK_SAMPLE_PERIOD 200000

//whats the minimum overflow period
#define MIN_CLOCK_SAMPLE_PERIOD 1024

#define logical_clock_update_clock_ticks(group_info, tid)                           \
    unsigned long rawcount = local64_read(&group_info->clocks[tid].event->count); \
    group_info->clocks[tid].debug_last_overflow_ticks=rawcount;         \
    if (rawcount < 0) printk(KERN_EMERG "UHOH, %d\n",tid); \
    group_info->clocks[tid].ticks+=rawcount; \ 
    local64_set(&group_info->clocks[tid].event->count, 0);

//utility function to read the performance counter
static inline void __read_performance_counter(struct hw_perf_event * hwc, uint64_t * _new_raw_count, uint64_t * _prev_raw_count){
    uint64_t new_raw_count, prev_raw_count;
 again:    
    //read the raw counter
    rdmsrl(hwc->event_base + hwc->idx, new_raw_count);
    //get previous count
    prev_raw_count = local64_read(&hwc->prev_count);
    //in case an NMI fires while we're doing this...unlikely but who knows?!
    if (local64_cmpxchg(&hwc->prev_count, prev_raw_count, new_raw_count) != prev_raw_count){
        goto again;
    }
    //set the arguments to the values read
    *_new_raw_count=new_raw_count;
    *_prev_raw_count=prev_raw_count;
}

static inline void logical_clock_read_clock_and_update(struct task_clock_group_info * group_info, int id){
    uint64_t new_raw_count, prev_raw_count;
    int64_t delta;
    //for our version of perf counters (v3 for Intel) this works...probably not for anything else
    //ARCH_DEP_TODO
    int shift = 16;
    struct hw_perf_event * hwc = &group_info->clocks[id].event->hw;
    //read the counters using rdmsr
    __read_performance_counter(hwc, &new_raw_count, &prev_raw_count);
    //if this succeeds, then its safe to turn off the tick_counter...meaning we no longer do work inside the overflow handler.
    //Even if an NMI beats us to it...it won't have any work to do since prev_raw_count==new_raw_count after the cmpxchg
    __tick_counter_turn_off(group_info);
    //compute how much the counter has counted. The shift is used since only the first N (currently hard-coded to 48) bits matter
    delta = (new_raw_count << shift) - (prev_raw_count << shift);
    //shift it back to get the actually correct number
    delta >>= shift;
    //add it to our current clock
    __inc_clock_ticks(group_info, id, delta);
    //let userspace see it
    current->task_clock.user_status->ticks=__get_clock_ticks(group_info, current->task_clock.tid);
}

static inline void logical_clock_reset_current_ticks(struct task_clock_group_info * group_info, int id){
    uint64_t new_raw_count, prev_raw_count;
    //we can just read the counter...since that will effectively reset it
    __read_performance_counter(&group_info->clocks[id].event->hw, &new_raw_count, &prev_raw_count);
    //reset the event's counter to 0
    local64_set(&group_info->clocks[id].event->count, 0);
}

static inline void logical_clock_update_overflow_period(struct task_clock_group_info * group_info, int id){
    uint64_t lowest_waiting_tid_clock, myclock, new_sample_period;
    int32_t lowest_waiting_tid=0;    

    if (group_info->clocks[current->task_clock.tid].event){
        new_sample_period=group_info->clocks[current->task_clock.tid].event->hw.sample_period+10000;
        lowest_waiting_tid = __search_for_lowest_waiting_exclude_current(group_info, current->task_clock.tid);
        if (lowest_waiting_tid>=0){
            lowest_waiting_tid_clock = __get_clock_ticks(group_info,lowest_waiting_tid);
            myclock = __get_clock_ticks(group_info,current->task_clock.tid);
            //if there is a waiting thread, and its clock is larger than ours, stop when we get there
            if (lowest_waiting_tid_clock > myclock){
                new_sample_period=__max(lowest_waiting_tid_clock - myclock + 1000, MIN_CLOCK_SAMPLE_PERIOD);
            }
        }
        group_info->clocks[current->task_clock.tid].debug_last_sample_period = group_info->clocks[current->task_clock.tid].event->hw.sample_period;
        group_info->clocks[current->task_clock.tid].event->hw.sample_period =  __min(new_sample_period, MAX_CLOCK_SAMPLE_PERIOD);
    }
}

static inline void logical_clock_reset_overflow_period(struct task_clock_group_info * group_info, int id){
    group_info->clocks[current->task_clock.tid].event->hw.sample_period=0;
    local64_set(&group_info->clocks[current->task_clock.tid].event->hw.period_left,0);
}




#endif
