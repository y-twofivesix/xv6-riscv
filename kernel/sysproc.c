#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
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
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
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

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
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

  argint(0, &pid);
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

uint64
sys_send(void)
{
  int pid, len;
  uint64 buf_va;
  char buf[64];

  argint(0, &pid);
  argaddr(1, &buf_va);
  argint(2, &len);

  if(len < 0 || len > sizeof(buf))
    return -1;
  
  if(copyin(myproc()->pagetable, buf, buf_va, len) < 0)
    return -1;

  return ipc_send(pid, buf, len);
}

uint64
sys_recv(void)
{
  uint64 sender_pid_va;
  uint64 buf_va;
  int maxlen;
  char buf[64];
  int sender_pid;
  
  argaddr(0, &sender_pid_va);
  argaddr(1, &buf_va);
  argint(2, &maxlen);

  if(maxlen < 0)
    return -1;
  
  if(ipc_recv(&sender_pid, buf, maxlen) < 0)
    return -1;
  
  if(sender_pid_va != 0){
    if(copyout(myproc()->pagetable, sender_pid_va, (char*)&sender_pid, sizeof(sender_pid)) < 0)
      return -1;
  }
  
  if(copyout(myproc()->pagetable, buf_va, buf, maxlen > 64 ? 64 : maxlen) < 0)
    return -1;
    
  return 0;
}
