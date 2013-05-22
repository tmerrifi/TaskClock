
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>

#include <asm/unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <asm-generic/mman-common.h>
#include <syscall.h>
#include <poll.h>
#include <errno.h>

#include "perf_event.h"
#include "perf_counter.h"


long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                int cpu, int group_fd, unsigned long flags)
{
    int ret;

    ret = syscall(__NR_perf_event_open, hw_event, pid, cpu,
                   group_fd, flags);
    return ret;
}

struct perf_counter_info * perf_counter_init(u_int32_t sample_period, int32_t group_fd){

  struct perf_event_attr pe;
  int fd;
  long long count;

  //clear the perf_event_attr struct                                                                                   
  memset(&pe, 0, sizeof(struct perf_event_attr));
  pe.type = PERF_TYPE_HARDWARE;
  pe.size = sizeof(struct perf_event_attr);
  pe.config = PERF_COUNT_HW_INSTRUCTIONS;
  pe.disabled = 1;
  pe.exclude_kernel = 1;
  pe.exclude_hv = 1;
  pe.sample_period = sample_period;
  //this is our custom special flag!
  pe.task_clock = 1;

  fd = perf_event_open(&pe, 0, -1, group_fd, 0);
  if (fd == -1) {
    fprintf(stderr, "Error opening leader %llx\n", pe.config);
    exit(EXIT_FAILURE);
  }

  void * ring_buffer;

  printf("fd is %d\n", fd);
  //16MB ring buffer
  if ((ring_buffer = mmap(NULL, PAGE_SIZE + (PAGE_SIZE * 1024) , PROT_READ | PROT_WRITE,
			  MAP_SHARED, fd, 0)) == MAP_FAILED) {
    printf("\nFAILED! %d fd %d %d\n", getpid(), fd, errno);
    perror("FAILED");
    close(fd);
    exit(EXIT_FAILURE);
  }  

  //setup the info object
  struct perf_counter_info * pci = malloc(sizeof(struct perf_counter_info));
  pci->pid=getpid();
  pci->fd=fd;
  pci->ring_buffer_current=ring_buffer+PAGE_SIZE;
  pci->ring_buffer=ring_buffer;
  return pci;
}

void perf_counter_start(struct perf_counter_info * pci){

  if ( ioctl(pci->fd, PERF_EVENT_IOC_RESET, 0) != 0){
    printf("\nreset wrong\n");
    exit(EXIT_FAILURE);
  }
  
  if ( ioctl(pci->fd, PERF_EVENT_IOC_ENABLE, 0) != 0){
    printf("\nenable wrong\n");
    exit(EXIT_FAILURE);
  }
}

void perf_counter_stop(struct perf_counter_info * pci){
  printf("perf_counter_stop %d\n", getpid());
  if ( ioctl(pci->fd, PERF_EVENT_IOC_DISABLE, 0) != 0){
    printf("\ndisable wrong\n");
    exit(EXIT_FAILURE);
  }
}


struct perf_event_header * __move_one_record(struct perf_event_header * current){
  return (struct perf_event_header *)(((u_int8_t *) current) + current->size);
}

u_int64_t perf_counter_read(struct perf_counter_info * pci){
  //walk through the ring buffer and count the sample records
  //long long count;

  if ( ioctl(pci->fd, PERF_EVENT_IOC_DISABLE, 0) != 0){
    printf("\ndisable wrong\n");
    exit(EXIT_FAILURE);
  }

  u_int64_t event_count=0;
  struct perf_event_header * event_header = (struct perf_event_header *)pci->ring_buffer_current;
  struct perf_event_mmap_page * meta_data = (struct perf_event_mmap_page *)pci->ring_buffer;

  while(1){
    //if (event_header->type!=PERF_RECORD_SAMPLE || 
    //Either the buffer is empty...or we already saw this...so nothing is new
    if (event_header->size==0){
      break;
    }
    //some event may have triggered an event (process exit) that is not a valid sample
    if (event_header->type!=PERF_RECORD_SAMPLE){
      //just skip it for now
      goto movenext;
    }

    printf("found a sample!\n");
    //ok, we've got a valid sample, increment the event counter
    event_count++;
    //fall through to move_next

  movenext:
    //set this so we don't count this again
    event_header=__move_one_record(event_header);
  }

  pci->ring_buffer_current=event_header;
  return event_count;
}
