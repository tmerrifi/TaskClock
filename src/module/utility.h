#ifndef LOGICAL_CLOCK_UTIL_H
#define LOGICAL_CLOCK_UTIL_H

#define __current_tid() current->task_clock.tid

#define __min(val1, val2) ((val1<val2) ? val1 : val2)

#define __max(val1, val2) ((val1>val2) ? val1 : val2)

#define __set_base_ticks(group_info, tid, val) (group_info->clocks[tid].base_ticks=val)

#define __get_base_ticks(group_info, tid) (group_info->clocks[tid].base_ticks)

#define __tick_counter_turn_off(group_info) \
    group_info->clocks[__current_tid()].count_ticks=0;

#define __tick_counter_turn_on(group_info) \
    group_info->clocks[__current_tid()].count_ticks=1;

#define __tick_counter_is_running(group_info) \
    group_info->clocks[__current_tid()].count_ticks


#define __inc_clock_ticks(group_info, tid, val) (group_info->clocks[tid].ticks+=val)

#define __set_clock_ticks(group_info, tid, val) (group_info->clocks[tid].ticks=val)

#define __get_clock_ticks(group_info, tid) (group_info->clocks[tid].ticks + group_info->clocks[tid].base_ticks)


#define __clock_is_lower(group_info, tid1, tid2) ( ((__get_clock_ticks(group_info, tid1))  < (__get_clock_ticks(group_info, tid2))) \
                                               || ((__get_clock_ticks(group_info, tid1)) == (__get_clock_ticks(group_info, tid2))  && (tid1 < tid2)))

#define __clear_entry_state(group_info) \
    group_info->clocks[__current_tid()].inactive=0;\
    group_info->clocks[__current_tid()].waiting=0;\
    group_info->clocks[__current_tid()].sleeping=0;
    

#define __clear_entry_state_by_id(group_info, id) \
    group_info->clocks[id].inactive=0;\
    group_info->clocks[id].waiting=0;\
    group_info->clocks[id].sleeping=0;

#define __mark_as_inactive(group_info, id)         \
    group_info->clocks[id].inactive=1; \
    listarray_remove(group_info->active_threads, id);

#define __mark_as_active(group_info, id)         \
    group_info->clocks[id].inactive=0; \
    listarray_insert(group_info->active_threads, id);

//Am I the only one here????
#define __current_is_only_active_thread(group_info) \
    (listarray_count(group_info->active_threads)==1 && group_info->clocks[__current_tid()].inactive==0)


#endif
