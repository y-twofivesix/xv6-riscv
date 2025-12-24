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

// Sulu Display Device Driver (/dev/sulu)
// Implements a "Single Connection" Event-Driven architecture

// Events sent from Kernel to Server
#define SULU_EVENT_CONNECT    1
#define SULU_EVENT_DISCONNECT 2
#define SULU_EVENT_DATA       3

struct sulu_msg {
    int type;
    int pid;      // Client PID
    int val1;     // e.g., shm_key or width
    int val2;     // e.g., height
};

#define QUEUE_SIZE 64
struct {
    struct spinlock lock;
    struct sulu_msg queue[QUEUE_SIZE];
    int head;
    int tail;
    int server_pid; // PID of the process acting as Window Server (Sulu)
} sulu_state;

void
sulu_dev_init(void)
{
  initlock(&sulu_state.lock, "sulu_dev");
  devsw[SULU_DEV].read = sulu_dev_read;
  devsw[SULU_DEV].write = sulu_dev_write;
  // devsw[SULU_DEV].close = sulu_dev_close; // NOTE: standard devsw doesn't have close, we hook file.c
}

// Enqueue an event for the server
void
sulu_queue_event(int type, int pid, int v1, int v2)
{
    acquire(&sulu_state.lock);
    int next = (sulu_state.head + 1) % QUEUE_SIZE;
    if(next != sulu_state.tail) {
        sulu_state.queue[sulu_state.head].type = type;
        sulu_state.queue[sulu_state.head].pid = pid;
        sulu_state.queue[sulu_state.head].val1 = v1;
        sulu_state.queue[sulu_state.head].val2 = v2;
        sulu_state.head = next;
        wakeup(&sulu_state); // Wake up server
    }
    release(&sulu_state.lock);
}

// Write: Clients send commands here (e.g., Register Window)
int
sulu_dev_write(int user_dst, uint64 dst, int n, int off)
{
    // If not a user pointer, we can't read it easily here without copyin
    // Assuming 'dst' is user virtual address
    
    struct proc *p = myproc();
    
    // Simple command structure: [cmd_type, val1, val2]
    int cmd[3];
    if(n < sizeof(cmd)) return -1;
    if(copyin(p->pagetable, (char*)cmd, dst, sizeof(cmd)) < 0) return -1;
    
    // Command 1: Register SHM Window
    if(cmd[0] == 1) {
        // cmd[1] = shm_key, cmd[2] = width (packed? need height too)
        // Let's assume clients send a struct { type, shm_key, width, height }
        int extended_cmd[4];
        if(n >= sizeof(extended_cmd)) {
             if(copyin(p->pagetable, (char*)extended_cmd, dst, sizeof(extended_cmd)) < 0) return -1;
             
             // Pack width/height into val2: (width << 16) | height
             int w = extended_cmd[2];
             int h = extended_cmd[3];
             sulu_queue_event(SULU_EVENT_CONNECT, p->pid, extended_cmd[1], (w << 16) | h);
        }
    }
    
    return n;
}

// Read: Server reads events
int
sulu_dev_read(int user_dst, uint64 dst, int n, int off)
{
    struct proc *p = myproc();
    acquire(&sulu_state.lock);
    
    // If this is a NEW server process, reset the queue (clears stale events from crashed server)
    if(sulu_state.server_pid != p->pid) {
        sulu_state.head = 0;
        sulu_state.tail = 0;
        sulu_state.server_pid = p->pid;
        console_flush();  // Also flush console input to prevent stale chars
    }
    
    // If queue empty, return 0 (Non-blocking for game loop)
    if(sulu_state.head == sulu_state.tail) {
        release(&sulu_state.lock);
        return 0;
    }
    
    // Dequeue one event
    struct sulu_msg msg = sulu_state.queue[sulu_state.tail];
    sulu_state.tail = (sulu_state.tail + 1) % QUEUE_SIZE;
    
    release(&sulu_state.lock);
    
    if(n < sizeof(msg)) return -1;
    if(copyout(p->pagetable, dst, (char*)&msg, sizeof(msg)) < 0) return -1;
    
    return sizeof(msg);
}

// Close: Hooked from fileclose() in file.c
// Called when a file descriptor referring to SULU_DEV is closed
int
sulu_dev_close(int minor, struct file *f)
{
    // We can identify who closed it by myproc()->pid
    struct proc *p = myproc();
    
    // Enqueue Disconnect Event
    // Note: We don't track *which* window explicitly yet, just that PID X is gone.
    // The server will close all windows owned by PID X.
    sulu_queue_event(SULU_EVENT_DISCONNECT, p->pid, 0, 0);
    
    return 0;
}

// Reset sulu_dev state - called when Sulu restarts to clear old events
void
sulu_dev_reset(void)
{
    acquire(&sulu_state.lock);
    sulu_state.head = 0;
    sulu_state.tail = 0;
    sulu_state.server_pid = 0;
    release(&sulu_state.lock);
}
  

