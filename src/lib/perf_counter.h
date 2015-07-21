

#ifdef __cplusplus
 extern "C" {
#endif

#ifndef PERF_COUNTER_H
#define PERF_COUNTER_H


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


     
#include "determ_perf_event.h"
#include <sys/types.h>

   #define PAGE_SIZE 4096
   //used to mark a sample that has been read
   #define PERF_MAGIC_NUMBER 0xDEAD

   struct perf_counter_info{
       pid_t pid;
       int fd;
       void * ring_buffer;
       void * ring_buffer_current; //where are we pointing at right now in the ring buffer
       int started;
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
     void perf_counter_close(struct perf_counter_info * pci);

#endif

#ifdef __cplusplus
 }
#endif
