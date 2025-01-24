#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "stat.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"

int
sys_set_proc_info(void)
{
  int q_level;
  int cpu_burst;
  int cpu_wait_time;
  int io_wait_time;
  int end_time;

  struct proc *p = myproc();
  p->q_level = 0;
  p->cpu_burst = 0;
  p->cpu_wait = 0;
  p->io_wait_time = 0;
  p->end_time = -1;

  if(argint(0, &q_level) >= 0 && argint(0, &q_level) <= 3) {
    p->q_level = q_level; 
  }
    
  if(argint(1, &cpu_burst) >= 0) {
    p->cpu_burst = cpu_burst;
  }
    
  if(argint(2, &cpu_wait_time) >= 0) {
    p->cpu_wait = cpu_wait_time;
  }
    
  if(argint(3, &io_wait_time) >= 0) {
    p->io_wait_time = io_wait_time;
  }
    
  if(argint(4, &end_time) >= 0) {
    p->end_time = end_time;
  }

  if(p->q_level != 0) {
    qP0--;
    if(p->q_level == 1) {
      qP1++;
      Q1[qP1] = p;
      sigQ = 1;
      sig_level = 1;
    }
    else if(p->q_level == 2) {
      qP2++;
      Q2[qP2] = p;
      sigQ = 1; 
      sig_level = 2;
    }
    else if(p->q_level == 3) {
      qP3++;
      Q2[qP2] = p;
      sigQ = 1;
      sig_level = 3;
    }
  }  
  else {
    sigQ = 1;
    sig_level = 0;
  }

#ifdef DEBUG
  cprintf("set process %d's info complete\n", p->pid);
#endif

  return 0;
}

int 
sys_lseek(void) { // sys_lseek() 함수 구현   
  int fd; // 파일디스크립터 번호
  int offset; // 이동할 오프셋
  int whence; // SEEK_SET, SEEK_CUR, SEEK_END

  // 첫번째, 두번째, 세번째 인자를 각각 fd, offset, whence로 가져온다
  if(argint(0, &fd) < 0 || argint(1, &offset) < 0 || argint(2, &whence) < 0) {
    return -1;
  }

  // fd에 해당하는 파일을 가져온다
  struct file *f;
  if((f = myproc()->ofile[fd]) == 0) {
    return -1;
  }

  switch(whence) {
    case SEEK_SET: // whence 의 값이 SEEK_SET 인 경우
      if(offset < 0) // offset 의 값이 음수인 경우 에러
        return -1;
      f->off = offset; // 파일의 offset 을 offset 으로 변경
      break;

    case SEEK_CUR: // whence 의 값이 SEEK_CUR 인 경우
      if(f->off + offset < 0)
        return -1;
      f->off += offset;
      break;

    case SEEK_END: // whence 의 값이 SEEK_END 인 경우
      if(offset < 0 && (f->ip->size + offset < 0))
        return -1;
      f->off = f->ip->size + offset;
      break;

    default:
      return -1;
  }

  return f->off; 
}

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
