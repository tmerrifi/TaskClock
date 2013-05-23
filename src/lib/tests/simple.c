
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "../determ_clock.h"

void do_work(int work_count){
  int sum=0;
  for (int i=0; i <work_count; ++i){
    sum++;
  }
}

int test1(int work){
  determ_task_clock_start();
  do_work(work);
}


int main(){
  int thread_count=6;
  int * threads = malloc(sizeof(int)*thread_count);
  determ_task_clock_halt();

  for(int i=0;i<thread_count;++i){
    pid_t pid = fork();
    if (pid==0){
      determ_task_clock_init();
      test1((i+1)*100000);
      determ_task_clock_is_lowest_wait();
      test1((i+1)*100000);
      determ_task_clock_is_lowest_wait();
      determ_task_clock_halt();
      printf("Thread %d done\n", i);
      exit(1);
    }
    else{
      threads[i]=pid;
    }
  }
  
  int status;
  for(int i=0;i<thread_count;++i){
    waitpid(threads[i], &status, 0);
  }
  //test1();
  //sleep(1);
  //test1();
}
