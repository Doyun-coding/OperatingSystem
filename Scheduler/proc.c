#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include <stddef.h>


struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

int time_quantum[4] = {10, 20, 40, 80};
struct proc* Q0[64];
struct proc* Q1[64];
struct proc* Q2[64];
struct proc* Q3[64];
int qP0 = -1;
int qP1 = -1;
int qP2 = -1;
int qP3 = -1;

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();

  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p; // 프로세스 구조체 선언
  char *sp;

  acquire(&ptable.lock); // 테이블 락 획득

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) // 반복문을 통해 UNUSED 상태의 프로세스 탐색
    if(p->state == UNUSED)
      goto found; // 찾으면 found 로 goto

  release(&ptable.lock); // 락 해제
  return 0;

found:
  p->state = EMBRYO; // 프로세스 상태를 EMBRYO 설정
  p->pid = nextpid++; // 고유한 프로세스 pid 를 할당

  release(&ptable.lock); // 락 해제

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){  // 커널 스택을 할당
    p->state = UNUSED; // 0인 경우 상태 UNUSED 로 설정하고 리턴
    return 0;
  }
  sp = p->kstack + KSTACKSIZE; // 커널 스택의 최상단 주소 설정

  // Leave room for trap frame.
  // trapframe 을 위한 공간 확보
  sp -= sizeof *p->tf; 
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  // forkret에서 trapret으로 돌아가기 위한 주소 설정
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context); // context 를 초기화
  p->context->eip = (uint)forkret; // 새로운 프로세스는 forkret에서 시작
 
  p->end_time = 0; // 응용프로그램의 CPU 사용량 초기화
  p->cpu_burst = 0; // 처음 CPU 의 사용시간은 0 으로 초기화
  p->cpu_wait = 0; // RUNNABLE 이후 큐에서 대기시간 0 으로 초기화
  p->io_wait_time = 0; // SLEEPING 시간 0 으로 초기화
  p->ticks = 0; 

  if(p->pid == 0 || p->pid == 1 || p->pid == 2) {
    p->q_level = 3; // 처음 프로세스는 우선 순위가 제일 높은 큐로 초기화
    
    qP3++;
    Q3[qP3] = p;
  }
  else {
    p->q_level = 0; // 처음 프로세스는 우선 순위가 제일 높은 큐로 초기화
    qP0++; // 0번째 큐의 인덱스 값을 하나 늘리고 새로운 프로세스 대입
    
    Q0[qP0] = p;

  }

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  if(pid >= 4) {
    #ifdef DEBUG
    cprintf("PID: %d created\n", pid);
    #endif
  }
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc(); // 현재 실행 중인 프로세스를 구조체에 대입
  struct proc *p;
  int fd;

  if(curproc == initproc) // init 프로세스가 종료되면 panic 함수 호출
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){ // 반복문을 통해 열려 있는 파일을 모두 닫는다
    if(curproc->ofile[fd]){ // 파일이 열려있을 경우
      fileclose(curproc->ofile[fd]); // 파일을 닫는다
      curproc->ofile[fd] = 0; // 파일 테이블을 비운다
    }
  }

  begin_op(); // 현재 프로세스의 디렉토리를 정리
  iput(curproc->cwd); // 작업 디렉토리의 inode 를 해제
  end_op();
  curproc->cwd = 0; // 작업 디렉토리 초기화

  acquire(&ptable.lock); // 테이블 락 획득

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent); // 부모 프로세스가 잠들어 있을 수 있으니 깨운다

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){ // 현재 프로세스의 자식 프로세스를 init 프로세스로 양도
    if(p->parent == curproc){ // 자식 프로세스인 경우
      p->parent = initproc; // 부모의 init 프로세스 설정
      if(p->state == ZOMBIE) // 자식이 좀비인 경우
        wakeup1(initproc); // init 프로세스 깨운다
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE; // 프로세스 상태를 좀비로 설정
  sched(); // 스케줄러 호출
  panic("zombie exit"); // 패닉 상태로 전환
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

typedef struct Node {
  int q_level;
  int idx;
  int wait;
} Node;

struct Node *AgingQ[64];
int sigQ = 0;
int sig_level = 0;

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
// 우선순위가 가장 높은 큐 (Q0) 탐색	
    if((qP0 != -1 && sigQ == 0) || (qP0 != -1 && sigQ == 1 && sig_level == 0)) {
	for(int i = qP0; i >= 0; i--) {
	  p = Q0[i];
	  
	  if(p->state != RUNNABLE || p->q_level != 0)
	    continue;
	    
	  c->proc = p;
	 
	  switchuvm(p);
	  p->state = RUNNING;
	  
	  swtch(&(c->scheduler), p->context);
	  switchkvm();

	  if(p->io_wait_time >= 10) { // IO 프로세서
	    p->state = SLEEPING;
	    c->proc = 0;
	    continue;
	  }
// Aging 체크: 대기 시간이 250 이상인 프로세스는 상위 큐로 이동
	for(struct proc *pp = ptable.proc; pp < &ptable.proc[NPROC]; pp++) {
	      if(pp->state != RUNNABLE || pp->q_level == 0 || pp->pid == 0 || pp->pid == 1 || pp->pid == 2)
	        continue;
	    
	      if(pp->cpu_wait >= 250) {
	        sigQ = 1;
		sig_level = 0;
	        if(pp->q_level == 1) {
	          for(int i = 0; i <= qP1; i++) {
	            if(Q1[i] == pp) {
	            
	              Q1[i] = NULL;
	            
	              for(int j = i; j < qP1; j++) {
	                Q1[j] = Q1[j+1];
	              }
	              qP1--;
	              qP0++;
	            
	              pp->q_level = 0;
	              pp->cpu_wait = 0;
	              #ifdef DEBUG 
		      cprintf("PID: %d Aging\n", pp->pid);
	              #endif 
	              Q0[qP0] = pp;
	              break;
	            }
	          }
	        }
	        else if(pp->q_level == 2) {
	          for(int i = 0; i <= qP2; i++) {
	            if(Q2[i] == pp) {
	            
	              Q2[i] = NULL;
	            
	              for(int j = i; j < qP2; j++) {
	                Q2[j] = Q2[j+1];
	              }
	              qP2--;
	              qP1++;
	            
	              pp->q_level = 1;
	              pp->cpu_wait = 0;
		      #ifdef DEBUG
		      cprintf("PID: %d Aging\n", pp->pid);
	              #endif 
	              Q1[qP1] = pp;
	              break;
	            }
	          }
	        }
	        else if(pp->q_level == 3) {
	          for(int  i = 0; i <= qP3; i++) {
	            if(Q3[i] == pp) {
	            
	              Q3[i] = NULL;
	            
	              for(int j = i; j < qP3; j++) {
	                Q3[j] = Q3[j+1];
	              } 
	              qP3--;
	              qP2++;
	            
	              pp->q_level = 2;
	              pp->cpu_wait = 0;
		      #ifdef DEBUG
		      cprintf("PID: %d Aging\n", pp->pid);
	              #endif
	              Q2[qP2] = pp;
	              break;
	            }
	          }
	        } 
	      }
	    }
// 프로세스가 할당된 시간만큼 CPU를 사용했거나 종료 조건에 도달했는지 확인
	  if(p->cpu_burst - p->ticks >= time_quantum[0] || p->end_time - p->cpu_burst <= 0) {
	    sigQ = 0; 
	    int time_slice = p->cpu_burst - p->ticks;
	    p->ticks = p->cpu_burst;
	    
	    Q0[i] = NULL; 
	    for(int j = i; j < qP0; j++) {
	      Q0[j] = Q0[j+1];
	    }
	    qP0--;
	    
	    if(p->end_time - p->cpu_burst > 0) {
#ifdef DEBUG
              cprintf("PID: %d uses %d ticks in mlfq[%d], total(%d/%d)\n", p->pid, time_slice, p->q_level, p->cpu_burst, p->end_time);
#endif
            }
// 프로세스가 종료된 경우
            if(p->end_time - p->cpu_burst <= 0 && p->end_time != 0) {
              if(p->end_time != 0 && p->pid != 0 && p->pid != 1 && p->pid != 2) {
#ifdef DEBUG
                cprintf("PID: %d uses %d ticks in mlfq[%d], total(%d/%d)\n", p->pid, time_slice, p->q_level, p->cpu_burst, p->end_time);
                cprintf("PID: %d, used %d ticks. terminated\n", p->pid, p->cpu_burst);
#endif
                p->state = ZOMBIE;

                c->proc = 0;

		wakeup1(p->parent);

                break;
              }
            }
// 다음 우선순위 큐로 프로세스 이동
	    qP1++;
	    p->q_level = 1;
	    p->cpu_wait = 0;
	    Q1[qP1] = p;

	  }
	
	  c->proc = 0;
	  break;
	}
    }
    else if((qP1 != -1 && sigQ == 0) || (qP1 != -1 && sigQ == 1 && sig_level == 1)) {
      for(int i = qP1; i >= 0; i--) {
        p = Q1[i];
        
        if(p->state != RUNNABLE || p->q_level != 1)
          continue;
          
        c->proc = p;
        
        switchuvm(p);
        p->state = RUNNING;

        swtch(&(c->scheduler), p->context);
        switchkvm();

	for(struct proc *pp = ptable.proc; pp < &ptable.proc[NPROC]; pp++) {
	    if(pp->state != RUNNABLE || pp->q_level == 0 || pp->pid == 0 || pp->pid == 1 || pp->pid == 2)
	      continue;
	    
	    if(pp->cpu_wait >= 250) {
	      sigQ = 1;
	      sig_level = 1;
	      if(pp->q_level == 1) {
	        for(int i = 0; i <= qP1; i++) {
	          if(Q1[i] == pp) {
	            
	            Q1[i] = NULL;
	            
	            for(int j = i; j < qP1; j++) {
	              Q1[j] = Q1[j+1];
	            }
	            qP1--;
	            qP0++;
		    #ifdef DEBUG
		    cprintf("PID: %d Aging\n", pp->pid);
	            #endif 
	            pp->q_level = 0;
	            pp->cpu_wait = 0;
	            
	            Q0[qP0] = pp;
	            break;
	          }
	        }
	      }
	      else if(pp->q_level == 2) {
	        for(int i = 0; i <= qP2; i++) {
	          if(Q2[i] == pp) {
	            
	            Q2[i] = NULL;
	            
	            for(int j = i; j < qP2; j++) {
	              Q2[j] = Q2[j+1];
	            }
	            qP2--;
	            qP1++;
		    #ifdef DEBUG
		    cprintf("PID: %d Aging\n", pp->pid);
	            #endif 
	            pp->q_level = 1;
	            pp->cpu_wait = 0;
	            
	            Q1[qP1] = pp;
	            break;
	          }
	        }
	      }
	      else if(pp->q_level == 3) {
	        for(int  i = 0; i <= qP3; i++) {
	          if(Q3[i] == pp) {
	            
	            Q3[i] = NULL;
	            
	            for(int j = i; j < qP3; j++) {
	              Q3[j] = Q3[j+1];
	            } 
	            qP3--;
	            qP2++;
		    #ifdef DEBUG 	
		    cprintf("PID: %d Aging\n", pp->pid);
	            #endif
	            pp->q_level = 2;
	            pp->cpu_wait = 0;
	            
	            Q2[qP2] = pp;
	            break;
	          }
	        }
	      }
	    }
	  }

        
        if(p->cpu_burst - p->ticks >= time_quantum[1] || p->end_time - p->cpu_burst <= 0) { 
          sigQ = 0;

	  int time_slice = p->cpu_burst - p->ticks;
	  p->ticks = p->cpu_burst;
          
          Q1[i] = NULL;
          
          for(int j = i; j < qP1; j++) {
            Q1[j] = Q1[j+1];
          }
          qP1--;

	  if(p->end_time - p->cpu_burst > 0) {
	#ifdef DEBUG
            cprintf("PID: %d uses %d ticks in mlfq[%d], total(%d/%d)\n", p->pid, time_slice, p->q_level, p->cpu_burst, p->end_time);
#endif
          }

          if(p->end_time - p->cpu_burst <= 0 && p->end_time != 0) {
            if(p->end_time != 0 && p->pid != 0 && p->pid != 1 && p->pid != 2) {
#ifdef DEBUG
              cprintf("PID: %d uses %d ticks in mlfq[%d], total(%d/%d)\n", p->pid, time_slice, p->q_level, p->cpu_burst, p->end_time);
              cprintf("PID: %d, used %d ticks. terminated\n", p->pid, p->cpu_burst);
#endif
              p->state = ZOMBIE;
	      wakeup1(p->parent);

              c->proc = 0;

              break;
            }
          }
			  
	  qP2++;
	  p->q_level = 2;
	  p->cpu_wait = 0;
	  Q2[qP2] = p;
          
        }
        c->proc = 0;
	break;
      }
    }
    else if((qP2 != -1 && sigQ == 0) || (qP2 != -1 && sigQ == 1 && sig_level == 2)) {
      for(int i = qP2; i >= 0; i--) {
        p = Q2[i];
      
        if(p->state != RUNNABLE || p->q_level != 2)
          continue;
        
        c->proc = p;
      
        switchuvm(p);
        p->state = RUNNING;
      
        swtch(&(c->scheduler), p->context);
        switchkvm();

	for(struct proc *pp = ptable.proc; pp < &ptable.proc[NPROC]; pp++) {
	    if(pp->state != RUNNABLE || pp->q_level == 0 || pp->pid == 0 || pp->pid == 1 || pp->pid == 2)
	      continue;
	    
	    if(pp->cpu_wait >= 250) {
	      sigQ = 1;
	      sig_level = 2;
	      if(pp->q_level == 1) {
	        for(int i = 0; i <= qP1; i++) {
	          if(Q1[i] == pp) {
	            
	            Q1[i] = NULL;
	            
	            for(int j = i; j < qP1; j++) {
	              Q1[j] = Q1[j+1];
	            }
	            qP1--;
	            qP0++;
	            #ifdef DEBUG	
		    cprintf("PID: %d Aging\n", pp->pid);
	            #endif
	            pp->q_level = 0;
	            pp->cpu_wait -= 250;
	            
	            Q0[qP0] = pp;
	            break;
	          }
	        }
	      }
	      else if(pp->q_level == 2) {
	        for(int i = 0; i <= qP2; i++) {
	          if(Q2[i] == pp) {
	            
	            Q2[i] = NULL;
	            
	            for(int j = i; j < qP2; j++) {
	              Q2[j] = Q2[j+1];
	            }
	            qP2--;
	            qP1++;
 		    #ifdef DEBUG
		    cprintf("PID: %d Aging\n", pp->pid);
	            #endif 
	            pp->q_level = 1;
	            pp->cpu_wait -= 250;
	            
	            Q1[qP1] = pp;
	            break;
	          }
	        }
	      }
	      else if(pp->q_level == 3) {
	        for(int  i = 0; i <= qP3; i++) {
	          if(Q3[i] == pp) {
	            
	            Q3[i] = NULL;
	            
	            for(int j = i; j < qP3; j++) {
	              Q3[j] = Q3[j+1];
	            } 
	            qP3--;
	            qP2++;
		    #ifdef DEBUG
		    cprintf("PID: %d Aging\n", pp->pid);
	            #endif
	            pp->q_level = 2;
	            pp->cpu_wait -= 250;
	            
	            Q2[qP2] = pp;
	            break;
	          }
	        }
	      }
	    }
	  }
		
        if(p->cpu_burst - p->ticks >= time_quantum[2] || p->end_time - p->cpu_burst <= 0) {
	  sigQ = 0;
	  
	  int time_slice = p->cpu_burst - p->ticks;
	  p->ticks = p->cpu_burst;
        
          Q2[i] = NULL;
        
          for(int j = i; j < qP2; j++) {
            Q2[j] = Q2[j+1];
          }
          qP2--;

	  if(p->end_time - p->cpu_burst > 0) {
#ifdef DEBUG
            cprintf("PID: %d uses %d ticks in mlfq[%d], total(%d/%d)\n", p->pid, time_slice, p->q_level, p->cpu_burst, p->end_time);
#endif
          }

          if(p->end_time - p->cpu_burst <= 0 && p->end_time != 0) {
            if(p->end_time != 0 && p->pid != 0 && p->pid != 1 && p->pid != 2) {
#ifdef DEBUG
              cprintf("PID: %d uses %d ticks in mlfq[%d], total(%d/%d)\n", p->pid, time_slice, p->q_level, p->cpu_burst, p->end_time);
              cprintf("PID: %d, used %d ticks. terminated\n", p->pid, p->cpu_burst);
#endif
              p->state = ZOMBIE;
	      wakeup1(p->parent);

              c->proc = 0;
              
	      break;
            }
          }

	  qP3++;
	  p->q_level = 3;
	  p->cpu_wait = 0;
	  Q3[qP3] = p;

	}
	c->proc = 0;
	break;
      }
    }
    else if((qP3 != -1 && sigQ == 0) || (qP3 != -1 && sigQ == 1 && sig_level == 3)) {
      for(int i = qP3; i >= 0; i--) {
        p = Q3[i];

	if(p->state != RUNNABLE || p->q_level != 3)
          continue;

	c->proc = p;

        switchuvm(p);
        p->state = RUNNING;
        
        swtch(&(c->scheduler), p->context);
        switchkvm();

	for(struct proc *pp = ptable.proc; pp < &ptable.proc[NPROC]; pp++) {
	    if(pp->state != RUNNABLE || pp->q_level == 0 || pp->pid == 0 || pp->pid == 1 || pp->pid == 2)
	      continue;
	    
	    if(pp->cpu_wait >= 250) {
	      sigQ = 1;
	      sig_level = 3;
	      if(pp->q_level == 1) {
	        for(int i = 0; i <= qP1; i++) {
	          if(Q1[i] == pp) {
	            
	            Q1[i] = NULL;
	            
	            for(int j = i; j < qP1; j++) {
	              Q1[j] = Q1[j+1];
	            }
	            qP1--;
	            qP0++;
		    #ifdef DEBUG
		    cprintf("PID: %d Aging\n", pp->pid);
	            #endif
	            pp->q_level = 0;
	            pp->cpu_wait -= 250;
	            
	            Q0[qP0] = pp;
	            break;
	          }
	        }
	      }
	      else if(pp->q_level == 2) {
	        for(int i = 0; i <= qP2; i++) {
	          if(Q2[i] == pp) {
	            
	            Q2[i] = NULL;
	            
	            for(int j = i; j < qP2; j++) {
	              Q2[j] = Q2[j+1];
	            }
	            qP2--;
	            qP1++;
		    #ifdef DEBUG
		    cprintf("PID: %d Aging\n", pp->pid);
	            #endif 
	            pp->q_level = 1;
	            pp->cpu_wait -= 250;
	            
	            Q1[qP1] = pp;
	            break;
	          }
	        }
	      }
	      else if(pp->q_level == 3) {
	        for(int  i = 0; i <= qP3; i++) {
	          if(Q3[i] == pp) {
	            
	            Q3[i] = NULL;
	            
	            for(int j = i; j < qP3; j++) {
	              Q3[j] = Q3[j+1];
	            } 
	            qP3--;
	            qP2++;
   		    #ifdef DEBUG
		    cprintf("PID: %d Aging\n", pp->pid);
		    #endif	            
	            pp->q_level = 2;
	            pp->cpu_wait -= 250;
	            
	            Q2[qP2] = pp;
	            break;
	          }
	        }
	      }

	    }
	  }

	if(p->cpu_burst - p->ticks >= time_quantum[3] || p->end_time - p->cpu_burst <= 0) { 
	  sigQ = 0;
	  
	  int time_slice = p->cpu_burst - p->ticks;
	  p->ticks = p->cpu_burst;

	  Q3[i] = NULL;
	  
	  for(int j = i; j < qP3; j++) {
	    Q3[j] = Q3[j+1];
	  }
	 
	  if(p->end_time - p->cpu_burst > 0) {
#ifdef DEBUG
	    cprintf("PID: %d uses %d ticks in mlfq[%d], total(%d/%d)\n", p->pid, time_slice, p->q_level, p->cpu_burst, p->end_time);	
#endif 
	  }
	  
	  if(p->end_time - p->cpu_burst <= 0 && p->end_time != 0) {
	    if(p->end_time != 0 && p->pid != 0 && p->pid != 1 && p->pid != 2) {
#ifdef DEBUG
	      cprintf("PID: %d uses %d ticks in mlfq[%d], total(%d/%d)\n", p->pid, time_slice, p->q_level, p->cpu_burst, p->end_time);
	      cprintf("PID: %d, used %d ticks. terminated\n", p->pid, p->cpu_burst);
#endif
	      qP3--; 
	      c->proc = 0;
	      
	      p->state = ZOMBIE;
	      wakeup1(p->parent);

	      break;
	    }
	  }
	  
	  p->q_level = 3;
	  p->cpu_wait = 0;
	  Q3[qP3] = p;
	}
	c->proc = 0;
        break;	
      }
    }
    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void) 
{
  int intena;
  struct proc *p = myproc(); // 현재 실행 중인 프로세스를 proc 구조체에 대입

  if(!holding(&ptable.lock)) // 테이블 락이 없으면 panic 함수 호출
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1) // 현재 CPU 의 모든 잠금이 해제되지 않은 경우 panic 함수 호출
    panic("sched locks");
  if(p->state == RUNNING) // 프로세스 상태가 실행 중인 경우 panic 함수 호출
    panic("sched running");
  if(readeflags()&FL_IF) // 인터럽트가 비활성화 되어 있지 않은 경우 panic 함수 호출
    panic("sched interruptible");
  intena = mycpu()->intena; // 현재 CPU 의 인터럽트를 저장
  swtch(&p->context, mycpu()->scheduler); // 현재 프로세스의 문맥을 현재 CPU 스케줄러와 바꿈
  mycpu()->intena = intena; // 이전 인터럽트 플래그를 복원
}

// Give up the CPU for one scheduling round.
void
yield(void) // 현재 CPU 에서 실행 중인 프로세스를 중단하고 다른 프로세스가 실행되도록 하는 함수
{
  // 프로세스 테이블에 접근하기 위해 락 획득
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE; // 상태를 실행가능 상태로 설정
  sched(); // 스케줄러를 호출하여 CPU 을 다른 프로세스로 양보
  release(&ptable.lock); // 테이블 락 해제
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void  
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc(); // 현재 프로세스를 proc 구조체에 대입
  
  if(p == 0) // 프로세스의 값이 0, 실행 중인 프로세스가 없다면 panic() 함수 호출
    panic("sleep");

  if(lk == 0) // 테이블 락이 없으면 panic() 함수 호출
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  // 테이블 락이 아닌 경우
  // 테이블 락을 획득하고 원래 가지고 있던 락을 해제
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC제: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan; // 채널 설정
  p->state = SLEEPING; // 설정한 채널의 상태를 SLEEPING 으로 설정

  sched(); // 스케줄러 호출하여 현재 실행 중인 프로세스 중단하고 다른 프로세스 실행

  // Tidy up.
  p->chan = 0; // 채널을 0으로 초기화

  // Reacquire original lock.
  // 테이블 락이 아닌 경우
  // 해제하고 원래 락 가져온다
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan) // SLEEPING 상태의 프로세스를 깨워 실행가능한 상태로 전환하는 함수
{
  struct proc *p; // 프로세스 구조체 선언

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) // 테이블을 반복문을 통해 탐색하면서 SLEEPING 상태의 프로세스를 찾는다
    if(p->state == SLEEPING && p->chan == chan) // 상태가 SLEEPING 이고 채널이 같으면 프로세스의 상태를 실행가능한 상태로 전환
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void 
wakeup(void *chan) // chan 에 맞는 프로세스 중 SLEEPING 상태의 프로세스를 모두 깨우는 함수
{
  acquire(&ptable.lock); // 테이블 락 획득
  wakeup1(chan); // wakeup1 함수를 이용하여 채널에 맞는 프로세스를 깨운다
  release(&ptable.lock); // 락 해
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
