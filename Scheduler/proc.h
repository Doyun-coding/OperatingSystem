// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;
// proc.h
extern void set_proc_info(int *q_level, int *cpu_burst, int *cpu_wait, int *io_wait_time, int *end_time);
extern struct proc* Q0[64];
extern struct proc* Q1[64];
extern struct proc* Q2[64];
extern struct proc* Q3[64];
extern int qP0;
extern int qP1;
extern int qP2;
extern int qP3;
extern int sigQ;
extern int sig_level;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  int q_level; // 총 4개의 큐 중에서 프로세스가 어떤 큐에 있는지 나타내는 변수
  int cpu_burst; // 프로세스당 time quantum 내에서 cpu 사용시간을 나타내는 변수 
  int cpu_wait; // 프로세스 당 RUNNABLE 상태에서 큐에서 대기하는 시간을 나타내는 변수
  int io_wait_time; // 프로세스 당 해당 큐에서 SLEEPING 상태 시간을 나타내는 변수
  int end_time; // 응용 프로그램의 총 CPU 사용 할당량을 나타내는 변수
  int ticks;
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
