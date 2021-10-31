#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_waitx(void)
{
  uint64 p, addr1, addr2;
  uint wtime, rtime;
  if (argaddr(0, &p) < 0)
    return -1;
  if (argaddr(1, &addr1) < 0)
    return -1;
  if (argaddr(2, &addr2) < 0)
    return -1;
  int ret = waitx(p, &wtime, &rtime);
  struct proc *p1 = myproc();
  if (copyout(p1->pagetable, addr1, (char *)&wtime, sizeof(int)) < 0)
    return -1;
  if (copyout(p1->pagetable, addr2, (char *)&rtime, sizeof(int)) < 0)
    return -1;
  return ret;
}

uint64
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

uint64
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

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// trace process
uint64
sys_strace(void)
{
  if (argint(0, &myproc()->mask) < 0) //assign the process its mask from the user space
  {
    return -1;
  }
  return 0;
}

// set process priority
uint64
sys_setpriority(void)
{
  int pid;
  int priority;
  if (argint(0, &priority) < 0) //priority is arg1
  {
    return -1;
  }
  if (argint(1, &pid) < 0) //pid is arg2
  {
    return -1;
  }
  int ret = setpriority(pid, priority);
  printf("Priority of process with pid: %d changed from %d to %d\n", pid, ret, priority);
  calc_dpriority(pid, ret); // calculate dynamic priority for this process
  return ret;
}
