#include "types.h"
#include "stat.h"
#include "user.h"

int main() {
  printf(1, "start scheduler_test\n");

  int process = 1;
  int pid = 0;
  
  if((pid = fork()) < 0) {
    printf(1, "fork error!\n");
    return -1;
  }
  else if(pid == 0) {

    set_proc_info(0, 0, 0, 0, 500);

    while(1);
    
  }

  wait();

  printf(1, "end of scheduler_test\n");

  exit();
  return 0;
}
