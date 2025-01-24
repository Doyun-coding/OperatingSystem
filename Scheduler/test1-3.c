#include "types.h"
#include "stat.h"
#include "user.h"

int main(void) {
    int i;
    int num_processes = 3; // 최대 프로세스 개수

    printf(1, "start scheduler_test\n");

    int pid1, pid2, pid3;

    if((pid1 = fork()) == 0) {
      set_proc_info(2, 0, 0, 0, 300);
      
      while(1);
    }

    if((pid2 = fork()) == 0) {
      set_proc_info(2, 0, 0, 0, 300);
      
      while(1);
    }
    if((pid3 = fork()) == 0) {
      set_proc_info(2, 0, 0, 0, 300);

      while(1);
    } 


    // 부모 프로세스는 자식들이 종료될 때까지 기다림
    for (i = 0; i < num_processes; i++) {
        wait();
    }

    printf(1, "end of scheduler_test\n");
    exit();
    exit();
    exit();

    return 0;
}
