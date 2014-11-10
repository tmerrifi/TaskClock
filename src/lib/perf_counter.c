
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

#include "determ_perf_event.h"
#include "perf_counter.h"


long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                int cpu, int group_fd, unsigned long flags)
{
    int ret;

    ret = syscall(__NR_perf_event_open, hw_event, pid, cpu,
                   group_fd, flags);
    return ret;
}

void perf_counter_init(u_int32_t sample_period, int32_t group_fd, struct perf_counter_info * pci){

  struct perf_event_attr pe;
  int fd;
  long long count;

  //clear the perf_event_attr struct                                                                                   
  memset(&pe, 0, sizeof(struct perf_event_attr));
  pe.type = PERF_TYPE_RAW;
  pe.size = sizeof(struct perf_event_attr);
  //INSTRUCTIONS RETIRED
  //pe.config = (0x0C0ULL) | (0x0000ULL);
  //Retired conditional branches
  pe.config = (0x00C4ULL) | (0x0100ULL);

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

  //16MB ring buffer
  if ((ring_buffer = mmap(NULL, PAGE_SIZE + (PAGE_SIZE * 64) , PROT_READ | PROT_WRITE,
			  MAP_SHARED, fd, 0)) == MAP_FAILED) {
      
      fprintf(stderr, "\nFAILED! %d fd %d %d\n", getpid(), fd, errno);
      perror("FAILED");
      close(fd);
      exit(EXIT_FAILURE);
  }  

  //setup the info object
  pci->pid=getpid();
  pci->fd=fd;
  pci->ring_buffer_current=ring_buffer+PAGE_SIZE;
  pci->ring_buffer=ring_buffer;
  pci->started=0;
}

void perf_counter_start(struct perf_counter_info * pci){
#ifndef NO_INSTRUCTION_COUNTING
  if ( ioctl(pci->fd, PERF_EVENT_IOC_ENABLE, 0) != 0){
    printf("\nenable wrong\n");
    exit(EXIT_FAILURE);
  }
#else
  if ( ioctl(pci->fd, PERF_EVENT_IOC_TASK_CLOCK_START) != 0){
      printf("\nClock read failed\n");
      exit(EXIT_FAILURE);
  }
#endif
  pci->started=1;

}

void perf_counter_stop(struct perf_counter_info * pci){
#ifndef NO_INSTRUCTION_COUNTING
  if ( ioctl(pci->fd, PERF_EVENT_IOC_DISABLE, 0) != 0){
    printf("\ndisable wrong\n");
    exit(EXIT_FAILURE);
  }
#else
  if ( ioctl(pci->fd, PERF_EVENT_IOC_TASK_CLOCK_STOP) != 0){
      printf("\nClock read failed\n");
      exit(EXIT_FAILURE);
  }

#endif
  pci->started=0;
}

void perf_counter_close(struct perf_counter_info * pci){
    if ( ioctl(pci->fd, PERF_EVENT_IOC_DISABLE, 0) != 0){
        printf("\ndisable wrong\n");
        exit(EXIT_FAILURE);
    }
    close(pci->fd);
}
