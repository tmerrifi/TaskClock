
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

int test1(){
  determ_task_clock_start();
  do_work(5000);
  printf("clock value is %d\n", determ_task_clock_read());
}


int main(){
  int threads=5;
  for(int i=0;i<threads;++i){
    if (fork()==0){
      determ_task_clock_init();
    }
    else{
      test1();
      sleep(1);
      test1();
      sleep(1);
      test1();
    }
  }
}
