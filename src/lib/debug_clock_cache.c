#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "debug_clock_cache.h"

int __open_debug_clock_cache_file(uint32_t thread_id){   
    int fd;
    char file_name[100];
    sprintf(file_name, "%s-%d", "clock-cache", thread_id);
#ifdef DEBUG_CLOCK_CACHE_PROFILE
    if ((fd=open(file_name, O_CREAT | O_TRUNC | O_RDWR, 00600))==-1){
        perror("failed to open clock cache\n");
        exit(-1);
    }
    //give it the right size
    ftruncate(fd, DEBUG_CLOCK_CACHE_ARR_SIZE_BYTES);
#elif DEBUG_CLOCK_CACHE_ON
    if ((fd=open(file_name, O_RDONLY))==-1){
        perror("failed to open clock cache\n");
        exit(-1);
    }
#endif
    return fd;
}

void debug_clock_cache_init(uint32_t thread_id, struct debug_clock_cache * clocks){
    int prot;
    void * mem;

    //open the file where the data resides
    int fd = __open_debug_clock_cache_file(thread_id);
    #ifdef DEBUG_CLOCK_CACHE_PROFILE
    prot=PROT_WRITE|PROT_READ;
    #elif DEBUG_CLOCK_CACHE_ON
    prot=PROT_READ;
    #endif
    //map the file
    if ((mem=mmap(NULL, DEBUG_CLOCK_CACHE_ARR_SIZE_BYTES, prot, MAP_SHARED, fd, 0))==MAP_FAILED){
        perror("failed to mmap the file");
        exit(-1);
    }
    clocks->clock_arr=(uint64_t *)mem;
    clocks->count-0;
    clocks->thread_id=thread_id;
    memset(mem, DEBUG_CLOCK_CACHE_ARR_SIZE_BYTES, 0);
}

void debug_clock_cache_insert(struct debug_clock_cache * clocks, uint64_t clock_value, uint64_t * diff){
    uint64_t tmp_diff=0;
    if (clocks->count > 0){
        if (clock_value < clocks->clock_arr[clocks->count -1]){
            printf("DEBUG_CLOCK_CACHE: failed, new clock value %llu is less than previous value %llu\n", clock_value, clocks->clock_arr[clocks->count - 1]);
            exit(-1);
        }
        tmp_diff=clock_value - clocks->clock_arr[clocks->count-1];
    }
    else{
        tmp_diff=clock_value;
    }
    *diff=(uint64_t)((double)tmp_diff * DEBUG_CLOCK_CACHE_ERROR_ADJUSTMENT_PERCENTAGE);
    printf("pid: %d, diff %llu, adjusted %llu, current clock %llu, old clock %llu\n", getpid(), tmp_diff, *diff, clock_value, (clocks->count > 0) ? clocks->clock_arr[clocks->count-1] : 0);
    clocks->clock_arr[clocks->count]=clock_value + *diff;
    clocks->count++;
}

uint64_t debug_clock_cache_get(struct debug_clock_cache * clocks, uint64_t clock_value, uint64_t * diff){
    uint64_t return_val=clocks->clock_arr[clocks->count];
    if (return_val<clock_value){
        printf("DEBUG_CLOCK_CACHE: failure, new clock value %llu is greater than cached value %llu, diff %llu\n", 
               clock_value, return_val, (clocks->count > 0) ? clock_value - clocks->clock_arr[clocks->count-1] : 0);
        *diff=0;
        return 0;
    }
    *diff=return_val-clock_value;
    printf("pid: %d, diff %llu, current clock %llu, expected %llu\n", getpid(), *diff, clock_value, (clocks->count > 0) ? clocks->clock_arr[clocks->count] : 0);
    clocks->count++;
    return return_val;
}

void debug_clock_cache_print(struct debug_clock_cache * clocks){
    for (int i=0;i<clocks->count;++i){
        printf("%d: %d %llu\n", clocks->thread_id, i, clocks->clock_arr[i]);
    }
}
