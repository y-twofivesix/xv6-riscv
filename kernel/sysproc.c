#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

#define MAX_SHM 16
#define SHM_KEY_FB 0xFB00
#define MAX_SHM_PAGES 2048 // Max pages per SHM segment (8MB max per segment)

// External from virtio_gpu.c
extern int gui_active;
extern struct proc proc[NPROC];

struct {
  struct spinlock lock;
  void *pages[MAX_SHM][MAX_SHM_PAGES]; // Array of page pointers for each segment
  int npages[MAX_SHM];                  // Number of pages in each segment
  uint size[MAX_SHM];                   // Size in bytes
  int keys[MAX_SHM];
  int used[MAX_SHM];
  int refcount[MAX_SHM];
  int is_static[MAX_SHM];               // 1 = static kernel memory (no kref/kfree)
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
sys_yield(void)
{
  yield();
  return 0;
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
       
       int segment_size;
       int npages_needed;

       if(is_fb){
          // Framebuffer (Static) - special case
          segment_size = 1280 * 800 * 4;
          npages_needed = (segment_size + PGSIZE - 1) / PGSIZE;
          // Store framebuffer as single entry (it's already contiguous)
          shm_table.pages[i][0] = (void*)framebuffer;
          shm_table.npages[i] = 1;  // Treat as single large region
          shm_table.is_static[i] = 1;
       } else {
          // Multi-page shared memory allocation
          npages_needed = (size + PGSIZE - 1) / PGSIZE; // Round up
          if(npages_needed <= 0) npages_needed = 1;
          if(npages_needed > MAX_SHM_PAGES){
             release(&shm_table.lock);
             return -1; // Too large
          }
          
          // Allocate each page and store in array
          for(int p = 0; p < npages_needed; p++){
             void *page = kalloc();
             if(page == 0){
                // Rollback: free previously allocated pages
                for(int r = 0; r < p; r++){
                   kfree(shm_table.pages[i][r]);
                }
                release(&shm_table.lock);
                return -1;
             }
             memset(page, 0, PGSIZE);
             shm_table.pages[i][p] = page;
          }
          shm_table.npages[i] = npages_needed;
          shm_table.is_static[i] = 0;
          segment_size = size;
       }
       
       shm_table.size[i] = segment_size;
       shm_table.keys[i] = key;
       shm_table.used[i] = 1;
       shm_table.refcount[i] = 0; // Initialize refcount
       
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
  
  uint seg_size = shm_table.size[shmid];
  int is_stat = shm_table.is_static[shmid];
  int stored_npages = shm_table.npages[shmid];
  
  // For static (framebuffer), calculate npages from size
  // For dynamic SHM, use stored npages
  uint npages = is_stat ? (PGROUNDUP(seg_size) / PGSIZE) : stored_npages;
  
  
  for(int i = 0; i < npages; i++){
      void *pa;
      uint64 va = addr + i*PGSIZE;
      
      if(is_stat){
        // Framebuffer: contiguous, compute from base
        pa = shm_table.pages[shmid][0] + i*PGSIZE;
      } else {
        // Dynamic SHM: use stored page pointers
        pa = shm_table.pages[shmid][i];
      }
      
      // Increment Ref Count (only if not static)
      if(!is_stat) kref(pa);
      
      // Map it
      if(mappages(p->pagetable, va, PGSIZE, (uint64)pa, PTE_W|PTE_R|PTE_U|PTE_S) != 0){
          // Rollback: unmap and unref previously mapped pages
          for(int r = 0; r < i; r++){
            void *rpa = is_stat ? (shm_table.pages[shmid][0] + r*PGSIZE) : shm_table.pages[shmid][r];
            uvmunmap(p->pagetable, addr + r*PGSIZE, 1, 0);
            if(!is_stat) kfree(rpa);
          }
          release(&shm_table.lock);
          return -1;
      }
  }
  
  // Update process size if we grew it
  // IMPORTANT: Use page-aligned size, not content size!
  // Otherwise sbrk() may allocate heap memory that overlaps with SHM page slack.
  uint64 shm_end = addr + npages * PGSIZE;
  if(shm_end > p->sz){
    p->sz = shm_end;
  }
  
  shm_table.refcount[shmid]++; // Attached
  
  release(&shm_table.lock);
  return addr;
}

uint64
sys_shmdt(void)
{
  int shmid; 
  uint64 addr;
  
  argint(0, &shmid);
  argaddr(1, &addr);
  
  if(shmid < 0 || shmid >= MAX_SHM){
      return -1;
  }
  
  acquire(&shm_table.lock);
  
  if(!shm_table.used[shmid]){
      release(&shm_table.lock);
      return -1;
  }
  
  // Unmap from user page table
  struct proc *p = myproc();
  int npages = shm_table.npages[shmid];
  
  // Unmap ONLY if addr is valid
  if(addr > 0 && addr < MAXVA) {
      // uvmunmap requires page aligned. 0 => do not free physical pages.
      uvmunmap(p->pagetable, addr, npages, 0); 
  }
  
  // Decrement refcount
  if(shm_table.refcount[shmid] > 0)
      shm_table.refcount[shmid]--;
      
  // Decrement page refcount for each page (mirrors kref in shmat)
  // kfree decrements refcount; only actually frees when refcount hits 0
  if(!shm_table.is_static[shmid]){
      for(int i = 0; i < npages; i++){
          if(shm_table.pages[shmid][i])
              kfree(shm_table.pages[shmid][i]);
      }
  }
  
  // Clean up segment metadata only when no more attachments
  if(shm_table.refcount[shmid] == 0 && !shm_table.is_static[shmid]){
      for(int i = 0; i < npages; i++){
          shm_table.pages[shmid][i] = 0;
      }
      shm_table.used[shmid] = 0;
      shm_table.keys[shmid] = 0;
  }
  
  release(&shm_table.lock);
  return 0;
}

uint64
sys_flush_console(void)
{
  console_flush();
  return 0;
}
