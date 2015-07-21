#ifndef SEARCH_ENTRIES_H
#define SEARCH_ENTRIES_H

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


//is this current tick_count the lowest
static inline int __is_lowest(struct task_clock_group_info * group_info, int32_t tid){
  if (tid==group_info->lowest_tid ||
      __get_clock_ticks(group_info, tid) < __get_clock_ticks(group_info, group_info->lowest_tid)){
    return 1;
  }
  return 0;
} 

static inline int32_t __active_thread_count(struct task_clock_group_info * group_info){
    int32_t count=0;
    int i=0;

    if (in_nmi()){
        listarray_foreach_allelements(group_info->active_threads, i){
            struct task_clock_entry_info * entry = &group_info->clocks[i];
            if (entry->initialized && !entry->inactive && !entry->sleeping){
                count++;
            }
        }
    }
    else{
        listarray_foreach(group_info->active_threads, i){
            struct task_clock_entry_info * entry = &group_info->clocks[i];
            if (entry->initialized && !entry->inactive && !entry->sleeping){
                count++;
            }
        }
    }
    return count;
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

static inline int32_t __search_for_lowest_print(struct task_clock_group_info * group_info){
  int i=0;
  int32_t min_tid=-1;
  printk(KERN_EMERG "STARTING SEARCH:");
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
          printk(KERN_EMERG "  looking at %d, min %d ", i, min_tid);
          if (min_tid>=0){
              printk(KERN_EMERG "    clock: %llu, min: %llu, result: %d, waiting: %d sleeping %d", 
                     __get_clock_ticks(group_info, i), __get_clock_ticks(group_info, min_tid), 
                     __clock_is_lower(group_info, i, min_tid), group_info->clocks[i].waiting, group_info->clocks[i].sleeping );
          }
          struct task_clock_entry_info * entry = &group_info->clocks[i];
          if (entry->initialized && !entry->inactive && (min_tid < 0 || __clock_is_lower(group_info, i, min_tid))){
              printk(KERN_EMERG "    setting min to %d\n", i);
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

//This function must be called while holding the group lock
static inline int32_t __search_for_lowest_waiting_print(struct task_clock_group_info * group_info){
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
    printk(KERN_EMERG "    SEARCH FOR LOWEST result %d return %d\n", low,  (low >= 0 && thread_arr[low]==1) ? low : -1);
    //if the lowest is greater than zero and it was waiting...return it
    return (low >= 0 && thread_arr[low]==1) ? low : -1;
}



//This function must be called while holding the group lock
static inline int32_t __search_for_lowest_waiting_debug(struct task_clock_group_info * group_info, uint8_t * debug_arr){
    int thread_arr[128];
    int i=0;
    int low=-1;
    int j=0;

    //for (;i<TASK_CLOCK_MAX_THREADS;++i){
    listarray_foreach(group_info->active_threads, i){
        struct task_clock_entry_info * entry = &group_info->clocks[i];
        if (entry->waiting){
            thread_arr[i]=1;
        }

        if (entry->initialized && !entry->inactive){
            debug_arr[j++]=i;
        }
        else{
            debug_arr[j++]=i+100;
        }
    }
    
    //now find the lowest
    low=__search_for_lowest(group_info);
    //if the lowest is greater than zero and it was waiting...return it
    return (low >= 0 && thread_arr[low]==1) ? low : -1;
}






#endif
