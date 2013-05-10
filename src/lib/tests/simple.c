
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
  struct determ_clock_info * clock = determ_clock_init();
  determ_clock_start(clock);
  do_work(5000);
  determ_clock_read(clock);
  printf("clock value is %d\n", clock->ticks);

}


int main(){
  if (test1()==1){
    printf("TEST1: PASSED\n");
  }
  else{
    printf("TEST1: FAILED\n");
  }

}
