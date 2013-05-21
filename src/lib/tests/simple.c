
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
  printf("clock value is %d\n", determ_task_clock_read());
}


int main(){
  int threads=3;
  printf("first pid is %d\n", getpid());

  for(int i=0;i<threads;++i){
    pid_t pid = fork();
    if (pid==0){
      determ_task_clock_init();
      printf("pid: %d %d\n", getpid(), i);
      test1((i+1)*10000);
      sleep(1);
      test1((i+1)*10000);
      sleep(1);
      test1((i+1)*10000);
      exit(1);
    }
  }
  
  sleep(5);
  //test1();
  //sleep(1);
  //test1();
}
