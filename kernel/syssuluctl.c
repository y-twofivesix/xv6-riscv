#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// Syscall wrapper for suluctl_poll
uint64
sys_suluctl_poll(void)
{
  return suluctl_poll();
}

// Syscall wrapper for suluctl_get_request
uint64
sys_suluctl_get_request(void)
{
  uint64 pid_ptr, shm_ptr, w_ptr, h_ptr;
  argaddr(0, &pid_ptr);
  argaddr(1, &shm_ptr);
  argaddr(2, &w_ptr);
  argaddr(3, &h_ptr);
  
  int pid, shm_key, width, height;
  int result = suluctl_get_request(&pid, &shm_key, &width, &height);
  
  if(result) {
    struct proc *p = myproc();
    copyout(p->pagetable, pid_ptr, (char*)&pid, sizeof(pid));
    copyout(p->pagetable, shm_ptr, (char*)&shm_key, sizeof(shm_key));
    copyout(p->pagetable, w_ptr, (char*)&width, sizeof(width));
    copyout(p->pagetable, h_ptr, (char*)&height, sizeof(height));
  }
  return result;
}
