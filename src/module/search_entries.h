#ifndef SEARCH_ENTRIES_H
#define SEARCH_ENTRIES_H

//is this current tick_count the lowest
static inline int __is_lowest(struct task_clock_group_info * group_info, int32_t tid){
  if (tid==group_info->lowest_tid ||
      __get_clock_ticks(group_info, tid) < __get_clock_ticks(group_info, group_info->lowest_tid)){
    return 1;
  }
  return 0;
} 


static inline int32_t  __search_for_lowest_waiting_exclude_current(struct task_clock_group_info * group_info, int32_t tid){
    int i=0;
    int32_t min_tid=-1;
    if (in_nmi()){
        listarray_foreach_allelements(group_info->active_threads, i){
            struct task_clock_entry_info * entry = &group_info->clocks[i];
            if (entry->initialized && !entry->inactive && entry->waiting && i!=tid && (min_tid < 0 || __clock_is_lower(group_info, i, min_tid))){
                min_tid=i;
            }
        }
    }
    else{
        listarray_foreach(group_info->active_threads, i){
            struct task_clock_entry_info * entry = &group_info->clocks[i];
            if (entry->initialized && !entry->inactive && entry->waiting && i!=tid && (min_tid < 0 || __clock_is_lower(group_info, i, min_tid))){
                min_tid=i;
            }
        }
    }
    
    return (min_tid >=0) ? min_tid : -1;
}


static inline int32_t __search_for_lowest(struct task_clock_group_info * group_info){
  int i=0;
  int32_t min_tid=-1;
  if (in_nmi()){
      listarray_foreach_allelements(group_info->active_threads, i){
          struct task_clock_entry_info * entry = &group_info->clocks[i];
          if (entry->initialized && !entry->inactive && (min_tid < 0 || __clock_is_lower(group_info, i, min_tid))){
              min_tid=i;
          }
      }
  }
  else{
      listarray_foreach(group_info->active_threads, i){
          struct task_clock_entry_info * entry = &group_info->clocks[i];
          if (entry->initialized && !entry->inactive && (min_tid < 0 || __clock_is_lower(group_info, i, min_tid))){
              min_tid=i;
          }
      }
  }
  return min_tid;
}

//This function must be called while holding the group lock
static inline int32_t __search_for_lowest_waiting(struct task_clock_group_info * group_info){
    int thread_arr[128];
    int i=0;
    int low=-1;

    //for (;i<TASK_CLOCK_MAX_THREADS;++i){
    listarray_foreach(group_info->active_threads, i){
        struct task_clock_entry_info * entry = &group_info->clocks[i];
        if (entry->waiting){
            thread_arr[i]=1;
        }
    }
    
    //now find the lowest
    low=__search_for_lowest(group_info);
    //if the lowest is greater than zero and it was waiting...return it
    return (low >= 0 && thread_arr[low]==1) ? low : -1;
}



#endif
