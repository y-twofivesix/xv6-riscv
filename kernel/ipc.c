#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

extern struct proc proc[NPROC];

// Send a message to a process. 
// Blocks if the receiver's queue is full.
int
ipc_send(int pid, char *data, int len)
{
  struct proc *p;
  struct proc *target = 0;

  if(len > 64)
    return -1;

  // Find the target process
  // We still need to iterate proc table.
  // We need safe access to p->pid and p->state?
  // p->pid is stable if p is used.
  // To verify target exists, we can lock p->lock briefly.
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state != UNUSED && p->pid == pid){
      target = p;
      release(&p->lock); // Release it immediately
      break;
    }
    release(&p->lock);
  }

  if(target == 0){
    return -1; // Target not found
  }

  acquire(&target->ipc_lock);
  
  // Wait while queue is full
  while(target->msg_qlen == 8){
    // We can't easily check target->state safely without target->lock
    // But if target dies, maybe we should just timeout or fail?
    // For now, let's just assume valid.
    // However, if target dies, we might sleep forever.
    // Ideally we check target->killed or state. 
    // Checking state without lock is racy but maybe acceptable for "process no longer exists" hint
    if(target->killed || target->state == ZOMBIE || target->state == UNUSED){
       release(&target->ipc_lock);
       return -1;
    }
    
    sleep(&target->msg_queue, &target->ipc_lock);
  }

  // Copy message
  int tail = target->msg_qtail;
  target->msg_queue[tail].sender_pid = myproc()->pid;
  memmove(target->msg_queue[tail].data, data, len);
  
  target->msg_qtail = (tail + 1) % 8;
  target->msg_qlen++;

  // Wake up the target if it is sleeping on its queue
  wakeup(&target->msg_queue);

  release(&target->ipc_lock);
  return 0;
}

// Receive a message.
// Blocks if queue is empty.
int
ipc_recv(int *sender_pid, char *buf, int maxlen)
{
  struct proc *p = myproc();
  
  acquire(&p->ipc_lock);

  while(p->msg_qlen == 0){
    if(p->killed){
      release(&p->ipc_lock);
      return -1;
    }
    sleep(&p->msg_queue, &p->ipc_lock);
  }

  int head = p->msg_qhead;
  struct ipc_msg *msg = &p->msg_queue[head];

  if(sender_pid)
    *sender_pid = msg->sender_pid;
  
  int len = 64;
  if(maxlen < 64) len = maxlen;
  
  memmove(buf, msg->data, len);

  p->msg_qhead = (head + 1) % 8;
  p->msg_qlen--;

  // Wake up any senders waiting for space
  wakeup(&p->msg_queue);

  release(&p->ipc_lock);
  return 0;
}
