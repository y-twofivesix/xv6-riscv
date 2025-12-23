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
// Also handles client window registration for the Sulu API

static struct spinlock suluctl_lock;
static int sulu_pid = -1;  // PID of the Sulu process
static int suspend_requested = 0;  // 0=none, 1=suspend, 2=resume, 3=quit

// Client window request queue
#define MAX_PENDING_CLIENTS 8
struct client_request {
    int pid;        // Client PID
    int shm_key;    // SHM key the client allocated
    int width;
    int height;
    int valid;      // 1 if pending, 0 if empty
};
static struct client_request pending_clients[MAX_PENDING_CLIENTS];

void
suluctlinit(void)
{
  initlock(&suluctl_lock, "suluctl");
  devsw[SULUCTL].read = suluctlread;
  devsw[SULUCTL].write = suluctlwrite;
  for(int i = 0; i < MAX_PENDING_CLIENTS; i++)
    pending_clients[i].valid = 0;
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

// Parse a simple integer from string
static int
parse_int(char *s, int *val)
{
  int v = 0;
  while(*s >= '0' && *s <= '9') {
    v = v * 10 + (*s - '0');
    s++;
  }
  *val = v;
  return 0;
}

// Write accepts commands: "register", "suspend", "resume", "quit", "connect <shm_key> <w> <h>"
int
suluctlwrite(int user_src, uint64 src, int n, int off)
{
  char cmd[64];
  if(n > 63) n = 63;
  
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
  
  // Client window connection request: "connect <shm_key> <width> <height>"
  if(strncmp(cmd, "connect ", 8) == 0) {
    int shm_key, width, height;
    char *p = cmd + 8;
    
    
    // Parse shm_key
    parse_int(p, &shm_key);
    while(*p && *p != ' ') p++;
    while(*p == ' ') p++;
    
    // Parse width
    parse_int(p, &width);
    while(*p && *p != ' ') p++;
    while(*p == ' ') p++;
    
    // Parse height
    parse_int(p, &height);
    
    
    // Find empty slot in queue
    for(int i = 0; i < MAX_PENDING_CLIENTS; i++) {
      if(!pending_clients[i].valid) {
        pending_clients[i].pid = myproc()->pid;
        pending_clients[i].shm_key = shm_key;
        pending_clients[i].width = width;
        pending_clients[i].height = height;
        pending_clients[i].valid = 1;
        release(&suluctl_lock);
        return n;
      }
    }
    release(&suluctl_lock);
    return -1;  // Queue full
  }
  
  if(sulu_pid < 0) {
    release(&suluctl_lock);
    return -1;  // No Sulu registered
  }
  
  struct proc *pr = findproc(sulu_pid);
  if(pr == 0) {
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

// Function for Sulu to poll - returns suspend/resume command and clears it
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

// Function for Sulu to get pending client requests
// Returns: 0 if no request, 1 if request found
// Fills in pid, shm_key, width, height
int
suluctl_get_request(int *pid, int *shm_key, int *width, int *height)
{
  acquire(&suluctl_lock);
  for(int i = 0; i < MAX_PENDING_CLIENTS; i++) {
    if(pending_clients[i].valid) {
      *pid = pending_clients[i].pid;
      *shm_key = pending_clients[i].shm_key;
      *width = pending_clients[i].width;
      *height = pending_clients[i].height;
      pending_clients[i].valid = 0;  // Mark as consumed
      release(&suluctl_lock);
      return 1;
    }
  }
  release(&suluctl_lock);
  return 0;
}
