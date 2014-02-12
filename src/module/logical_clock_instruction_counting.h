#ifndef LOGICAL_CLOCK_INSTRUCTION_COUNTING_H
#define LOGICAL_CLOCK_INSTRUCTION_COUNTING_H

//update the clock, using the count kept in the perf_event struct
#define logical_clock_update(group_info, tid)   \
    group_info->clocks[tid].ticks+=local64_read(&group_info->clocks[tid].event->count); \ 
    local64_set(&group_info->clocks[tid].event->count, 0);

#endif
