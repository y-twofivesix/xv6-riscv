#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"

// Sulu control device - allows userspace to suspend/resume the Sulu window manager
// Since xv6 doesn't have Unix-style signals, we use shared state that Sulu polls

static struct spinlock suluctl_lock;
static int sulu_pid = -1;  // PID of the Sulu process
static int suspend_requested = 0;  // 0=none, 1=suspend, 2=resume, 3=quit

void
suluctlinit(void)
{
  initlock(&suluctl_lock, "suluctl");
  devsw[SULUCTL].read = suluctlread;
  devsw[SULUCTL].write = suluctlwrite;
}

// Helper to find process by PID
static struct proc*
findproc(int pid)
{
  extern struct proc proc[NPROC];
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid && p->state != UNUSED){
      release(&p->lock);
      return p;
    }
    release(&p->lock);
  }
  return 0;
}

// Read returns the current state of Sulu
int
suluctlread(int user_dst, uint64 dst, int n, int off)
{
  acquire(&suluctl_lock);
  
  char *status;
  if(sulu_pid < 0) {
    status = "none";  // No Sulu registered
  } else {
    // Check if process still exists
    struct proc *p = findproc(sulu_pid);
    if(p == 0) {
      sulu_pid = -1;  // Process died
      status = "none";
    } else {
      if(suspend_requested == 1)
        status = "suspended";
      else
        status = "active";
    }
  }
  
  release(&suluctl_lock);
  
  int len = strlen(status);
  if(n > len) n = len;
  
  if(copyout(myproc()->pagetable, dst, status, n) < 0)
    return -1;
    
  return n;
}

// Write accepts commands: "register", "suspend", "resume", "quit"  
int
suluctlwrite(int user_src, uint64 src, int n, int off)
{
  char cmd[16];
  if(n > 15) n = 15;
  
  if(copyin(myproc()->pagetable, cmd, src, n) < 0)
    return -1;
  cmd[n] = 0;  // Null terminate
  
  acquire(&suluctl_lock);
  
  if(strncmp(cmd, "register", 8) == 0) {
    // Register the calling process as Sulu
    sulu_pid = myproc()->pid;
    suspend_requested = 0;
    release(&suluctl_lock);
    return n;
  }
  
  if(sulu_pid < 0) {
    release(&suluctl_lock);
    return -1;  // No Sulu registered
  }
  
  struct proc *p = findproc(sulu_pid);
  if(p == 0) {
    sulu_pid = -1;
    release(&suluctl_lock);
    return -1;  // Sulu process not found
  }
  
  // Set command for Sulu to read
  if(strncmp(cmd, "suspend", 7) == 0) {
    suspend_requested = 1;
    release(&suluctl_lock);
    return n;
  }
  else if(strncmp(cmd, "resume", 6) == 0) {
    suspend_requested = 2;
    release(&suluctl_lock);
    return n;
  }
  else if(strncmp(cmd, "quit", 4) == 0) {
    suspend_requested = 3;
    release(&suluctl_lock);
    // Use xv6's kill to mark process for termination
    kill(sulu_pid);
    sulu_pid = -1;
    return n;
  }
  
  release(&suluctl_lock);
  return -1;  // Unknown command
}

// Function for Sulu to poll - returns command and clears it
int
suluctl_poll(void)
{
  int cmd;
  acquire(&suluctl_lock);
  cmd = suspend_requested;
  if(cmd == 1 || cmd == 2) {
    suspend_requested = 0;  // Clear after reading
  }
  release(&suluctl_lock);
  return cmd;
}
