#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include <limits.h>
#include <math.h>

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

// queues for MLFQ
int queue[5][NPROC];
// int time_slice[5];             // time slice for each of the queues
int queue_tail[5];             // current tail index for each of the queues
int ageing_threshold[5];       // ageing threshold for each queue

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table at boot time.
void
procinit(void)
{
  struct proc *p;
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid() {
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  // printf("alloced %d \n", p->pid);
  p->state = USED;

  // Set process creation time to ticks
  p->ctime = ticks;
  p->rtime = 0;
  p->etime = 0;
  p->twtime = 0;

  // Set priority time scales over here
  p->stime_prev = 0;
  p->rtime_prev = 0;

  // Set the newness of a process to 1
  p->is_new = 1;
  p->ns = 0;

  // Set the default value of the static priority to 60
  p->pstatic = 60;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  p->qcount[0] = 0;
  p->qcount[1] = 0;
  p->qcount[2] = 0;
  p->qcount[3] = 0;
  p->qcount[4] = 0;

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  // printf("PROCESS: %d %d\n",p->pid, p->ctime);

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");
  
  p->state = RUNNABLE;
  #ifdef MLFQ
  p->cur_queue = 0;
  push_to(p->cur_queue, p->pid);
  p->mlfq_priority = 0;
  // printf("%d %d %d\n",p->pid, 0, p->ctime);
  #endif
  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // copy trace mask from parent to child
  np->mask = p->mask;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  #ifdef MLFQ
  np->cur_queue = 0;
  push_to(np->cur_queue,np->pid);
  // printf("%d %d %d\n",np->pid, 0, np->ctime);
  #endif
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);
  // printf("exit sequence generated for process with pid: %d \n", p->pid);
  #ifdef MLFQ
  // printf("%d %d %d\n",p->pid, p->cur_queue, ticks);
  #endif
  p->xstate = status;
  p->state = ZOMBIE;
  p->etime = ticks;
  /*
  pop_given(p->cur_queue, p->pid);
  */
  printf("Process %d finished \n", p->pid);
  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}


// Waitx for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
waitx(uint64 addr, uint *wtime, uint *rtime)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          *rtime = np->rtime;
          *wtime = np->etime - np->ctime - np->rtime;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                   sizeof(np->xstate)) < 0)
          {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Update time after each tick
// Update only for running processes
void
updatetime(void)
{
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->mlfq_priority != -1)
    { 
      p->qcount[p->cur_queue]++;
    }
    if (p->state == RUNNING)
    {
      p->rtime++;
      p->rtime_prev++;
    }
    if (p->state == SLEEPING)
    {
      p->stime_prev++;
    }
    if (p->state == RUNNABLE)
    {
      p->wtime++;
      p->twtime++;
      #ifdef MLFQ
      if (p->wtime > ageing_threshold[p->cur_queue] && p->cur_queue != 0)
      {
        // printf("ageing initiated %d %d \n", p->cur_queue, p->cur_queue-1);
        pop_specific(p->cur_queue, p->pid);
        p->mlfq_priority = -1;
        p->cur_queue -= 1;
        // printf("%d %d %d\n",p->pid, p->cur_queue, ticks);
        push_to(p->cur_queue, p->pid);
        p->mlfq_priority = p->cur_queue;
        p->wtime = 0;
      }
      #endif
    }
    release(&p->lock);
  }
}

// Set priority of process
int
setpriority(uint pid, uint priority)
{
  struct proc *p;
  int prev_priority = -1;
  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->pid == pid)
    {
      prev_priority = p->pstatic;
      p->pstatic = priority;
      p->is_new = 1;
      // p->ns = 0;
      p->stime_prev = 0;
      p->rtime_prev = 0;
    }
    release(&p->lock);
  }
  return prev_priority;
}

// Calculate dynamic priority of the newly set process
void 
calc_dpriority(int pid, int prev_priority)
{
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
     if (p->pid == pid && prev_priority > p->pdynamic)
     {
       // printf("scheding to reschedule process with pid: %d \n", p->pid);
       release(&p->lock);
       yield();
       break;
     }
    release(&p->lock);
  }
}

/*
// Deprecate queue levels for MLFQ
void interrupt_procs()
{
  struct cpu *c = mycpu();
  if (c->proc == 0)
  {
    return;
  }
  // printf("timer in here \n");
  struct proc *p = myproc();
  acquire(&p->lock);
  if (p->cur_queue < 0)
  {
    release(&p->lock);
    return;
  }
  p->cpu_time++;
  if (time_slice[p->cur_queue] < p->cpu_time)
  {
    //preempt and place in lower queue
    if (p->cur_queue < 4)
    {
      //remove from cur_queue and place in lower queue and then schedule
      printf("timer pushing: %d %d %d\n", p->pid, p->cpu_time, p->cur_queue);
      p->cpu_time = 0;
      p->cur_queue += 1;
    }
    else
    {
      printf("timer pushing: %d %d %d\n", p->pid, p->cpu_time, p->cur_queue);
      p->cur_queue = 4;
    }
    release(&p->lock);
    printf("about to yield process: %d\n",p->pid);
    yield();
    printf("yielded process: %d\n", p->pid);
    return;
  }
  release(&p->lock);
}
*/
void push_to(int queue_number, int pid)
{
  if (pid < 0)
  {
    return;
  }
  queue_tail[queue_number]++;
  queue[queue_number][queue_tail[queue_number]] = pid;
}

/*
struct proc* pop_from(int queue_number)
{
  int p = NULL;
  if (queue_tail[queue_number] < 0)
  {
    return p;
  }
  p = queue[queue_number][0];
  for (int i = 1; i < queue_tail[queue_number] - 1; i++)
  {
    queue[queue_number][i - 1] = queue[queue_number][i];
  }
  queue_tail[queue_number]--;
  return p;
}
*/

void pop_given(int queue_number, int pid)
{
  if (queue_tail[queue_number] < 0)
  {
    return;
  }
  for (int i = 0; i <= queue_tail[queue_number] - 1; i++)
  {
    queue[queue_number][i] = queue[queue_number][i + 1];
  }
  queue[queue_number][queue_tail[queue_number]] = -1;
  queue_tail[queue_number]--;
  return;
}

void pop_specific(int queue_number, int pid)
{
  int idx = -1;
  for (int i = 0; i <= queue_tail[queue_number]; i++)
  {
    if (queue[queue_number][i] == pid)
    {
      idx = i;
      break;
    }
  }
  if (idx == -1)
  {
    return;
  }
  for (int i = idx; i < queue_tail[queue_number]; i++)
  {
    queue[queue_number][i] = queue[queue_number][i+1];
  }
  //printf("popping: %d %d \n", queue_tail[queue_number], queue[queue_tail[queue_number]][0]);
  queue_tail[queue_number]--;
  return;
}

// preempt current running process if new process has a better priority


// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct cpu *c = mycpu();
  c->proc = 0;
  for (;;)
  {
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
    #ifdef FCFS
      struct proc *p;
      // fcfs: check if process is runnable
      // select process with the least ctime
      // check if minp is null
      // switch context with that process
      // handle trap 
      struct proc *minp = NULL;
      for (p = proc; p < &proc[NPROC]; p++)
      {
        acquire(&p->lock);
        if (p->state == RUNNABLE)
        {
          if (minp != NULL)
          {
            if (p->ctime < minp->ctime)
            {
              // printf("------minp------- %d", p->ctime);
              minp = p;
            }
          }
          else
          {
            minp = p;
          }
        }
        release(&p->lock);
      }
    #endif
    #ifdef RR
      struct proc *p;
      // printf("RR chosen \n");
      for(p = proc; p < &proc[NPROC]; p++) 
      {
        acquire(&p->lock);
        if(p->state == RUNNABLE) 
        {
          // Switch to chosen process.  It is the process's job
          // to release its lock and then reacquire it
          // before jumping back to us.
          p->state = RUNNING;
          c->proc = p;
          swtch(&c->context, &p->context);

          // Process is done running for now.
          // It should have changed its p->state before coming back.
          c->proc = 0;
        }
        release(&p->lock);
      }
    #endif
    #ifdef PBS
      struct proc *p;
      // calculate the niceness
      struct proc *minp = NULL;
      for (p = proc; p < &proc[NPROC]; p++)
      {
        acquire(&p->lock);
        if (p->is_new == 1)
        {
          p->niceness = 5;
        }
        else
        {
          p->niceness = (int)(p->stime_prev*10) / (p->rtime_prev + p->stime_prev);
        }
        int fmin_val = 100;
        int fmax_val = 0;
        if (p->pstatic - p->niceness + 5 < 100)
        {
          fmin_val = p->pstatic - p->niceness + 5;
        }
        if (fmin_val > 0)
        {
          fmax_val = fmin_val;
        }
        p->pdynamic = fmax_val;
        if (p->state == RUNNABLE)
        {
          if (minp != NULL)
          {
            if (minp->pdynamic > p->pdynamic)
            {
              minp = p;
            }
            else if (minp->pdynamic == p->pdynamic)
            {
              if (minp->ns > p->ns)
              {
                minp = p;
              }
              else if (minp->ns == p->ns)
              {
                if (minp->ctime < p->ctime)
                {
                  minp = p;
                }
              }
            }
          }
          else
          {
            minp = p;
          }
        }
        release(&p->lock);
      }
    #endif
    #ifdef MLFQ
      struct proc *p;
      struct proc *minp = 0;
      int chosen_pid = -1;
      for (int i = 0; i < 5; i++)
      {
        if (queue_tail[i] >= 0)
        {
          chosen_pid = queue[i][0];
          break;
        }
      }
      if (chosen_pid == -1)
      {
        continue;
      }
      for (p = proc; p < &proc[NPROC]; p++)
      {
        acquire(&p->lock);
        if (p->pid == chosen_pid)
        {
          minp = p;
        }
        release(&p->lock);
      }
      if (minp != 0 && minp->pid > 0 && minp->state == RUNNABLE)
      {
        /*
        acquire(&minp->lock);
        minp->wtime = 0;
        minp->state = RUNNING;
        c->proc = minp;
        printf("in scheduler \n");
        print_queue(1);
        pop_given(minp->cur_queue, minp->pid);
        print_queue(0);
        print_queue(1);
        print_queue(2);
        print_queue(3);
        print_queue(4);
        printf("after %d \n", minp->pid);
        swtch(&c->context, &minp->context);
        printf("\n switch happened \n");
        // printf("process than ran is: %d from queue: %d \n", minp->pid, minp->cur_queue);
        c->proc = 0;
        release(&minp->lock);
        */
        acquire(&minp->lock);
        if (minp->state == RUNNABLE && minp->pid >= 0)
        {
          // printf("\n process executing: %d, level: %d \n", minp->pid, minp->cur_queue);
          minp->state = RUNNING;
          c->proc = minp;
          pop_given(minp->cur_queue, minp->pid);
          minp->mlfq_priority = -1;
          minp->ns++;
          // printf("process: %d %d \n", minp->pid, minp->ctime);
          // printf("------chosen------- %d \n", minp->pid);
          // printf("tail size: %d \n", queue_tail[minp->cur_queue]);
          // printf("MLFQ running %d \n", minp->pid);
          swtch(&c->context, &minp->context);
          c->proc = 0;
        }
        release(&minp->lock);
      }
    #endif
    #if defined(FCFS) || defined(PBS)
      if (minp != NULL)
      {
        // printf("chosen: %d %d \n", minp->pid, minp->state==RUNNABLE);
        acquire(&minp->lock);
        if (minp->state == RUNNABLE)
        {
          // printf("\n process executing: %d, level: %d \n", minp->pid, minp->cur_queue);
          minp->state = RUNNING;
          c->proc = minp;
          #ifdef PBS
            // if process has been scheduled, it is no more a new process. 
            // Niceness can be calculated for this process.
            minp->is_new = 0;
            minp->rtime_prev = 0;
            minp->stime_prev = 0;
            minp->ns += 1;
          #endif
          #ifdef MLFQ
            // minp->cur_queue = -1;
          #endif
          // printf("process: %d %d \n", minp->pid, minp->ctime);
          // printf("------chosen------- %d \n", minp->pid);
          // printf("tail size: %d \n", queue_tail[minp->cur_queue]);
          // printf("MLFQ running %d \n", minp->pid);
          swtch(&c->context, &minp->context);
          c->proc = 0;
        }
        release(&minp->lock);
      }
    #endif
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  // printf("yielding initiatied for the process with pid: %d\n", p->pid);
  p->state = RUNNABLE;
  #ifdef MLFQ
  push_to(p->cur_queue, p->pid);
  p->mlfq_priority = p->cur_queue;
  #endif
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  // printf("sleeping chan %d\n", p->pid);
  p->state = SLEEPING;
  sched();
  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) 
      {
        p->state = RUNNABLE;
        #ifdef MLFQ
        push_to(p->cur_queue, p->pid);
        p->mlfq_priority = p->cur_queue;
        #endif
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
        #ifdef MLFQ
        push_to(p->cur_queue, p->pid);
        p->mlfq_priority = p->cur_queue;
        #endif
      }
      release(&p->lock);
      return 0;
    }
    printf("killed \n");
    release(&p->lock);
  }
  return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

//print queue
void
print_queue(int queue_number)
{
  printf("queue printing initiated \n");
  for (int i = 0; i <= queue_tail[queue_number]; i++)
  {
    printf("%d ", queue[queue_number][i]);
  }
  printf("\n");
  printf("queue printing done \n");
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  #ifdef PBS
    char *row[6] = {"PID", "PRIORITY", "STATE", "rtime", "wtime", "nrun"};
    printf("%s \t| %s \t| %s \t| %s \t| %s \t| %s \t\n", row[0], row[1], row[2], row[3], row[4], row[5]);
  #endif
  #ifdef MLFQ
    char *row[11] = {"PID", "PRIORITY", "STATE", "rtime", "wtime", "nrun", "q0", "q1", "q2", "q3", "q4"};
    printf("%s \t| %s \t| %s \t| %s \t| %s \t| %s \t\t|%s \t\t|%s \t\t|%s \t\t|%s \t\t|%s \t\t\n", row[0], row[1], row[2], row[3], row[4], row[5], row[6], row[7], row[8], row[9], row[10]);
  #endif
  for(p = proc; p < &proc[NPROC]; p++){
    if (p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    #if defined(RR) || defined(FCFS)
      printf("pid: %d state: %s name: %s create-time: %d run-time: %d", p->pid, state, p->name, p->ctime, p->rtime);
    #endif
    #if defined(PBS)
      printf("%d \t %d \t\t %s \t %d \t\t %d \t\t %d \t\n", p->pid, p->pdynamic, state, p->rtime, ticks-p->ctime-p->rtime, p->ns);
    #endif
    #if defined(MLFQ)
      printf("%d \t %d\t\t %s \t  %d \t\t  %d \t\t  %d \t\t  %d \t\t  %d \t\t  %d \t\t  %d \t\t %d\n", p->pid, p->mlfq_priority, state, p->rtime, ticks-p->ctime-p->rtime, p->ns, p->qcount[0], p->qcount[1], p->qcount[2], p->qcount[3], p->qcount[4]);
    #endif
    printf("\n");
  }
}

void queue_init(void)
{
    #ifdef MLFQ
      for (int i = 0; i < 5; i++)
      {
        queue_tail[i] = -1;
      }
      ageing_threshold[0] = -1;
      ageing_threshold[1] = 10;
      ageing_threshold[2] = 20;
      ageing_threshold[3] = 30;
      ageing_threshold[4] = 40;
      /*
      time_slice[0] = 1;
      time_slice[1] = 2;
      time_slice[2] = 4;
      time_slice[3] = 8;
      time_slice[4] = 16;
      */
#endif
}
