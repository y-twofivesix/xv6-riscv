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

int shm_add_proc(struct proc *p, int shmid);
void shm_remove_proc(struct proc *p, int shmid);
void shm_detach(struct proc *p, int shmid, uint64 addr);

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

#include "memlayout.h"

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
  
  // 1. Search for existing key (unless key is 0, which means IPC_PRIVATE)
  if(key != 0) {
    for(int i=0; i<MAX_SHM; i++){
      if(shm_table.used[i] && shm_table.keys[i] == key){
        release(&shm_table.lock);
        return i; // Return shmid
      }
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
  
  // Check per-process limit and add to list
  if(shm_add_proc(p, shmid) < 0){
      release(&shm_table.lock);
      return -1; // Process limit reached
  }

  shm_table.refcount[shmid]++; // Attached
  
  release(&shm_table.lock);
  return addr;
}

// Detach shared memory segment
uint64
sys_shmdt(void)
{
  int shmid;
  uint64 addr;
  
  // Custom arg parsing since shmdt takes (int shmid, void* addr) in our userspace lib,
  // but standard shmdt is (void* addr).
  // Wait, usys.pl says shmdt is entry("shmdt"). 
  // Let's check ulib.c or user code. 
  // sulu_client.h calls shmdt(shmid, shm).
  // So syscall takes 2 args.
  
  argint(0, &shmid);
  argaddr(1, &addr);
  
  if(shmid < 0 || shmid >= MAX_SHM) return -1;
  
  struct proc *p = myproc();
  
  // Remove from process list
  shm_remove_proc(p, shmid);
  
  // Create mask for uvmunmap
  if(addr > 0 && addr < MAXVA) {
      acquire(&shm_table.lock); // Need lock to access shm_table.npages
      if(!shm_table.used[shmid]){ // Check if shmid is still valid
          release(&shm_table.lock);
          return -1;
      }
      int npages = shm_table.npages[shmid];
      release(&shm_table.lock);

      // We must manual unmap here because shm_detach assumes address knowledge or not.
      // But shm_detach logic was: "if addr > 0 ... uvmunmap"
      // Let's call shm_detach.
      
      // Wait, uvmunmap requires page alignment.
      // shm_detach has the logic commented out in previous step.
      // I should duplicate the uvmunmap call here for correctness before calling detach.
      uvmunmap(p->pagetable, addr, npages, 0); 
  }

  // Call internal detach (decrements refcount)
  shm_detach(p, shmid, 0); // addr=0 primarily to skip internal unmap if we did it here
  
  return 0;
}

uint64
sys_flush_console(void)
{
  console_flush();
  return 0;
}

// Process info structure (must match user/user.h)
struct procinfo {
  int pid;
  char name[16];
  int state;      // 0=unused, 1=used, 2=sleeping, 3=runnable, 4=running, 5=zombie
  uint64 sz;      // Memory size
};

uint64
sys_procinfo(void)
{
  uint64 addr;
  int nmax;
  
  argaddr(0, &addr);
  argint(1, &nmax);
  
  if(nmax <= 0)
    return -1;
  
  struct proc *p;
  struct procinfo info;
  int count = 0;
  
  for(p = proc; p < &proc[NPROC] && count < nmax; p++){
    acquire(&p->lock);
    if(p->state != UNUSED){
      info.pid = p->pid;
      memmove(info.name, p->name, sizeof(info.name));
      info.state = p->state;
      info.sz = p->sz;
      release(&p->lock);
      
      if(copyout(myproc()->pagetable, addr + count * sizeof(info), 
                  (char*)&info, sizeof(info)) < 0)
        return -1;
      count++;
    } else {
      release(&p->lock);
    }
  }
  
  return count;
}

uint64
sys_time(void)
{
  return rtctime();
}
// Helper: Add SHM ID to process tracking
int
shm_add_proc(struct proc *p, int shmid)
{
  for(int i=0; i<MAX_SHM_PER_PROC; i++){
    if(p->shm[i] == -1){
      p->shm[i] = shmid;
      return 0;
    }
  }
  return -1; // Process wide limit reached
}

// Helper: Remove SHM ID from process tracking
void
shm_remove_proc(struct proc *p, int shmid)
{
  for(int i=0; i<MAX_SHM_PER_PROC; i++){
    if(p->shm[i] == shmid){
      p->shm[i] = -1;
      return;
    }
  }
}

// Internal detach logic (called by sys_shmdt and shm_exit)
// Must hold shm_table.lock? No, it acquires it.
void
shm_detach(struct proc *p, int shmid, uint64 addr)
{
  acquire(&shm_table.lock);
  
  if(!shm_table.used[shmid]){
      release(&shm_table.lock);
      return;
  }
  
  // Refcount check? We assume caller knows p has it attached.
  
  // Unmap from user page table if addr matches (for explicit shmdt)
  // For shm_exit, addr might be 0 (unknown), so we might skip unmap 
  // and rely on proc_freepagetable doing the bulk unmap later?
  // Actually, proc_freepagetable cleans up the whole user address space, 
  // so we arguably don't need to uvmunmap here for exit(), 
  // blocking refcount decrements is the main issue.
  
  // However, sys_shmdt calls this with specific addr.
  
  // if(addr > 0 && addr < MAXVA) {
  //    int npages = shm_table.npages[shmid];
  //    // uvmunmap(p->pagetable, addr, npages, 0); // Already handled by sys_shmdt logic before
  // }
  
  // Decrement refcount
  if(shm_table.refcount[shmid] > 0)
      shm_table.refcount[shmid]--;
      
  // Free backing pages if refcount hits 0 (and not static)
  if(shm_table.refcount[shmid] == 0 && !shm_table.is_static[shmid]){
      int npages = shm_table.npages[shmid];
      for(int i = 0; i < npages; i++){
          if(shm_table.pages[shmid][i])
              kfree(shm_table.pages[shmid][i]);
          shm_table.pages[shmid][i] = 0;
      }
      shm_table.used[shmid] = 0;
      shm_table.keys[shmid] = 0;
  }
  
  release(&shm_table.lock);
}

// Inherit SHM segments on fork
void
shm_fork(struct proc *p, struct proc *np)
{
  acquire(&shm_table.lock);
  
  // Copy attached segments
  for(int i=0; i<MAX_SHM_PER_PROC; i++){
    int shmid = p->shm[i];
    if(shmid != -1){
      // Increment refcount
      if(shm_table.used[shmid]){
         shm_table.refcount[shmid]++;
         np->shm[i] = shmid;
      } else {
         np->shm[i] = -1; // Should not happen
      }
    } else {
      np->shm[i] = -1;
    }
  }
  
  release(&shm_table.lock);
}

// Detach all SHM segments on exit
void
shm_exit(struct proc *p)
{
  // Note: We don't need to unmap pages (uvmunmap) because
  // proc_freepagetable() will reclaim the user VA space completely.
  // We ONLY need to update the global SHM refcounts.
  
  for(int i=0; i<MAX_SHM_PER_PROC; i++){
    int shmid = p->shm[i];
    if(shmid != -1){
       shm_detach(p, shmid, 0); // addr=0 implies "just decrement refcount"
       p->shm[i] = -1;
    }
  }
}

uint64
sys_shutdown(void)
{
    printf("System Shutdown...\n");
    *(volatile uint32 *)VIRT_TEST = 0x5555;
    return 0;
}

uint64
sys_reboot(void)
{
    printf("System Reboot...\n");
    *(volatile uint32 *)VIRT_TEST = 0x7777;
    return 0;
}

uint64
sys_kill_child(void)
{
  int pid;
  argint(0, &pid);
  return kill_child(pid);
}
