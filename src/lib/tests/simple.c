
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
  if (test1()==1){
    printf("TEST1: PASSED\n");
  }
  else{
    printf("TEST1: FAILED\n");
  }

}
