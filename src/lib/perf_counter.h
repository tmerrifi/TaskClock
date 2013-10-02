

#ifdef __cplusplus
 extern "C" {
#endif

#ifndef PERF_COUNTER_H
#define PERF_COUNTER_H

#include <determ_perf_event.h>
#include <sys/types.h>

   #define PAGE_SIZE 4096
   //used to mark a sample that has been read
   #define PERF_MAGIC_NUMBER 0xDEAD

   struct perf_counter_info{
     pid_t pid;
     int fd;
     void * ring_buffer;
     void * ring_buffer_current; //where are we pointing at right now in the ring buffer
   };

   struct perf_mmap_sample{
     struct perf_event_header header;
     u_int64_t   ip;         /* if PERF_SAMPLE_IP */
     u_int32_t   pid, tid;   /* if PERF_SAMPLE_TID */
     u_int64_t   time;       /* if PERF_SAMPLE_TIME */
     u_int64_t   addr;       /* if PERF_SAMPLE_ADDR */
   };
   


     void perf_counter_init(u_int32_t sample_period, int32_t group_fd, struct perf_counter_info * pci);
     void perf_counter_start(struct perf_counter_info * pci);
     void perf_counter_stop(struct perf_counter_info * pci);
     u_int64_t perf_counter_read(struct perf_counter_info * pci);

#endif

#ifdef __cplusplus
 }
#endif
