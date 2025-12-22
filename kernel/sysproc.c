#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

#define MAX_SHM 8
#define SHM_KEY_FB 0xFB00

// External from virtio_gpu.c
extern int gui_active;

struct {
  struct spinlock lock;
  void *pages[MAX_SHM];
  uint size[MAX_SHM];     // Size in bytes
  int keys[MAX_SHM];
  int used[MAX_SHM];
  int is_static[MAX_SHM]; // 1 = static kernel memory (no kref/kfree)
} shm_table;

void
shminit()
{
  initlock(&shm_table.lock, "shm");
}

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

uint64
sys_shmget(void)
{
  int key;
  int size; 
  
  argint(0, &key);
  argint(1, &size);

  acquire(&shm_table.lock);
  
  // 1. Search for existing key
  for(int i=0; i<MAX_SHM; i++){
    if(shm_table.used[i] && shm_table.keys[i] == key){
      release(&shm_table.lock);
      return i; // Return shmid
    }
  }
  
  // 2. Allocate new
  for(int i=0; i<MAX_SHM; i++){
    if(!shm_table.used[i]){
       int is_fb = (key == SHM_KEY_FB);
       
       // Only create framebuffer shared memory if GUI hardware exists
       if(is_fb && !gui_active){
          release(&shm_table.lock);
          return -1; // No GPU hardware available
       }
       
       void *mem;
       int segment_size;

       if(is_fb){
          // Framebuffer (Static)
          mem = (void*)framebuffer; // Defined in defs.h/virtio_gpu.c
          segment_size = 1280 * 800 * 4; 
       } else {
          // Standard Shared Page
          mem = kalloc();
          if(mem == 0){
             release(&shm_table.lock);
             return -1;
          }
          memset(mem, 0, PGSIZE);
          segment_size = PGSIZE;
       }
       
       shm_table.pages[i] = mem;
       shm_table.size[i] = segment_size;
       shm_table.keys[i] = key;
       shm_table.used[i] = 1;
       shm_table.is_static[i] = is_fb;
       
       release(&shm_table.lock);
       return i;
    }
  }
  
  release(&shm_table.lock);
  return -1; // Full
}

uint64
sys_shmat(void)
{
  struct proc *p = myproc();
  int shmid; // Index into shm_table
  uint64 addr;
  
  argint(0, &shmid);
  argaddr(1, &addr);
  
  if(shmid < 0 || shmid >= MAX_SHM){
      return -1;
  }
  
  if(addr == 0){
      addr = PGROUNDUP(p->sz);
  } else if(addr % PGSIZE != 0){
      return -1; // Must be page aligned
  }
  
  acquire(&shm_table.lock);
  if(!shm_table.used[shmid]){
      release(&shm_table.lock);
      return -1;
  }
  
  void *base_pa = shm_table.pages[shmid];
  uint seg_size = shm_table.size[shmid];
  int is_stat = shm_table.is_static[shmid];
  
  uint npages = PGROUNDUP(seg_size) / PGSIZE;
  
  for(int i = 0; i < npages; i++){
      void *pa = base_pa + i*PGSIZE;
      uint64 va = addr + i*PGSIZE;
      
      // 2. Increment Ref Count (only if not static)
      if(!is_stat) kref(pa);
      
      // 3. Map it
      // PTE_W | PTE_R | PTE_U | PTE_S
      if(mappages(p->pagetable, va, PGSIZE, (uint64)pa, PTE_W|PTE_R|PTE_U|PTE_S) != 0){
          // Rollback logic is complex for multi-page. simple panic/fail for now
          // We should ideally kfree what we allocated.
          if(!is_stat) kfree(pa); // Free current
          release(&shm_table.lock);
          return -1;
      }
  }
  
  // Update process size if we grew it (simple sbrk-like behavior)
  if(addr + seg_size > p->sz){
    p->sz = addr + seg_size;
  }
  
  release(&shm_table.lock);
  return addr;
}
