#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "virtio.h"

// VirtIO Input Device ID is 18
#define VIRTIO_INPUT_DEV_ID 18

// Input event types
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03

// Absolute axis codes
#define ABS_X 0x00
#define ABS_Y 0x01

// Key codes
#define BTN_LEFT   0x110
#define BTN_RIGHT  0x111
#define BTN_MIDDLE 0x112

// Relative axis codes
#define REL_WHEEL  0x08

// Keyboard Scancodes
#define KEY_UP    103
#define KEY_LEFT  105
#define KEY_RIGHT 106
#define KEY_DOWN  108
#define KEY_LEFTSHIFT 42
#define KEY_RIGHTSHIFT 54
#define KEY_CAPSLOCK 58

struct virtio_input_event {
  uint16 type;
  uint16 code;
  uint32 value;
};

#define MAX_INPUTS 4

static struct input {
  int active;
  uint64 base;
  struct virtq_desc *desc;
  struct virtq_avail *avail;
  struct virtq_used *used;
  char free[NUM];
  uint16 used_idx;
  struct spinlock lock;

  // We need a pool of event structures for the device to fill
  struct virtio_input_event events[NUM];
} inputs[MAX_INPUTS];

// Input Event Buffer
#define INPUT_BUF_SIZE 64
struct {
  struct spinlock lock;
  struct virtio_input_event buf[INPUT_BUF_SIZE];
  uint r, w;
} input_buffer;

int inputread(int user_dst, uint64 dst, int n, int off);
void virtio_input_init(void);

// Linux Input Event codes (from linux/input-event-codes.h)
// KEY_1=2, KEY_Q=16, KEY_A=30, KEY_Z=44
// Parsing tables removed as logic moved to userspace (rio.c)

static int
alloc_desc(struct input *inp)
{
  for(int i = 0; i < NUM; i++){
    if(inp->free[i]){
      inp->free[i] = 0;
      return i;
    }
  }
  return -1;
}

void
virtio_input_init(void)
{
  int idx = 0;

  printf("[INPUT] Starting scan...\n");

  initlock(&input_buffer.lock, "input");
  devsw[INPUT].read = inputread;
  devsw[INPUT].readavail = inputreadavail;

  // Scan for VirtIO Input devices (ID 18)
  for(int i = 0; i < 8; i++){
    if(idx >= MAX_INPUTS) break;
    
    uint64 b = VIRTIO0 + i*PGSIZE;
    if(*(uint32*)(b + VIRTIO_MMIO_MAGIC_VALUE) == 0x74726976 &&
       *(uint32*)(b + VIRTIO_MMIO_DEVICE_ID) == VIRTIO_INPUT_DEV_ID){
      
      struct input *inp = &inputs[idx];
      inp->base = b;
      inp->active = 1;
      initlock(&inp->lock, "virtio_input");
      
      printf("[INPUT] Found device %d at 0x%lx\n", idx, b);

      // Reset device
      *(uint32*)(b + VIRTIO_MMIO_STATUS) = 0;

      uint32 status = VIRTIO_CONFIG_S_ACKNOWLEDGE;
      *(uint32*)(b + VIRTIO_MMIO_STATUS) = status;

      status |= VIRTIO_CONFIG_S_DRIVER;
      *(uint32*)(b + VIRTIO_MMIO_STATUS) = status;

      // Finish negotiation (default features)
      status |= VIRTIO_CONFIG_S_FEATURES_OK;
      *(uint32*)(b + VIRTIO_MMIO_STATUS) = status;

      // Initializing Event Queue (Queue 0)
      *(uint32*)(b + VIRTIO_MMIO_QUEUE_SEL) = 0;
      uint32 max = *(uint32*)(b + VIRTIO_MMIO_QUEUE_NUM_MAX);
      if(max < NUM) panic("virtio input: queue too short");

      *(uint32*)(b + VIRTIO_MMIO_QUEUE_NUM) = NUM;

      inp->desc = kalloc();
      inp->avail = kalloc();
      inp->used = kalloc();
      memset(inp->desc, 0, PGSIZE);
      memset(inp->avail, 0, PGSIZE);
      memset(inp->used, 0, PGSIZE);

      *(uint32*)(b + VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)inp->desc;
      *(uint32*)(b + VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)inp->desc >> 32;
      *(uint32*)(b + VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)inp->avail;
      *(uint32*)(b + VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)inp->avail >> 32;
      *(uint32*)(b + VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)inp->used;
      *(uint32*)(b + VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)inp->used >> 32;

      *(uint32*)(b + VIRTIO_MMIO_QUEUE_READY) = 1;

      for(int j = 0; j < NUM; j++) inp->free[j] = 1;

      // Fill the event queue with buffers for the device to write into
      for(int j = 0; j < NUM; j++){
        int d = alloc_desc(inp);
        inp->desc[d].addr = (uint64)&inp->events[j];
        inp->desc[d].len = sizeof(struct virtio_input_event);
        inp->desc[d].flags = VRING_DESC_F_WRITE;
        inp->desc[d].next = 0;
        inp->avail->ring[inp->avail->idx % NUM] = d;
        inp->avail->idx++;
      }

      *(uint32*)(b + VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

      status |= VIRTIO_CONFIG_S_DRIVER_OK;
      *(uint32*)(b + VIRTIO_MMIO_STATUS) = status;
      
      idx++;
    }
  }

  if(idx == 0){
    printf("[INPUT] No input devices found.\n");
  } else {
    printf("[INPUT] Initialized %d input devices.\n", idx);
  }
}

// Global mouse state accessible by WM
int mouse_x = 0;
int mouse_y = 0;
int mouse_btn = 0;
int mouse_scroll = 0;

int
inputread(int user_dst, uint64 dst, int n, int off)
{
  struct virtio_input_event e;
  int count = 0;
  
  acquire(&input_buffer.lock);
  while(n >= sizeof(struct virtio_input_event)){
    while(input_buffer.r == input_buffer.w){
      if(killed(myproc())){
        release(&input_buffer.lock);
        return -1;
      }
      sleep(&input_buffer.r, &input_buffer.lock);
    }
    
    e = input_buffer.buf[input_buffer.r % INPUT_BUF_SIZE];
    input_buffer.r++;
    
    if(either_copyout(user_dst, dst, &e, sizeof(e)) == -1){
      break;
    }
    
    dst += sizeof(e);
    n -= sizeof(e);
    count += sizeof(e);
  }
  release(&input_buffer.lock);
  return count;
}

int
inputreadavail(void)
{
  int n;
  acquire(&input_buffer.lock);
  n = input_buffer.w - input_buffer.r;
  release(&input_buffer.lock);
  return n * sizeof(struct virtio_input_event);
}

void
virtio_input_intr(void)
{
  for(int i = 0; i < MAX_INPUTS; i++){
    struct input *inp = &inputs[i];
    if(!inp->active) continue;

    acquire(&inp->lock);


    
    while(inp->used_idx != inp->used->idx){
      __sync_synchronize();
      int id = inp->used->ring[inp->used_idx % NUM].id;
      struct virtio_input_event *e = (struct virtio_input_event *)inp->desc[id].addr;

      // Simplification: Just buffer EVERY event for userspace to handle.
      // No kernel-side parsing needed anymore.
      acquire(&input_buffer.lock);
      input_buffer.buf[input_buffer.w % INPUT_BUF_SIZE] = *e;
      input_buffer.w++;
      wakeup(&input_buffer.r);
      release(&input_buffer.lock);

      // Re-queue the descriptor
      inp->desc[id].flags = VRING_DESC_F_WRITE;
      inp->avail->ring[inp->avail->idx % NUM] = id;
      inp->avail->idx++;
      inp->used_idx++;
    }

    *(uint32*)(inp->base + VIRTIO_MMIO_QUEUE_NOTIFY) = 0;
    release(&inp->lock);
  }
}
