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

#define SCREEN_W 1280
#define SCREEN_H 800

struct gpixel {
  int x;
  int y;
  int color;
};

static struct spinlock gwin_lock;

void
gwininit(void)
{
  initlock(&gwin_lock, "gwin");
  // The framebuffer is already zeroed in BSS
  devsw[GWIN].read = gwinread;
  devsw[GWIN].write = gwinwrite;
}

int
gwinwrite(int user_src, uint64 src, int n, int off)
{
  int count = n / sizeof(struct gpixel);
  if(count <= 0) return -1;

  int min_x = SCREEN_W, min_y = SCREEN_H;
  int max_x = 0, max_y = 0;
  int drew = 0;

  for(int i = 0; i < count; i++){
    struct gpixel pix;
    if(either_copyin(&pix, user_src, src + (uint64)i*sizeof(struct gpixel), sizeof(struct gpixel)) == -1)
      break;

    if(pix.x < 0 || pix.x >= SCREEN_W || pix.y < 0 || pix.y >= SCREEN_H)
      continue;

    acquire(&gwin_lock);
    framebuffer[pix.y * SCREEN_W + pix.x] = pix.color;
    release(&gwin_lock);

    if(pix.x < min_x) min_x = pix.x;
    if(pix.y < min_y) min_y = pix.y;
    if(pix.x > max_x) max_x = pix.x;
    if(pix.y > max_y) max_y = pix.y;
    drew = 1;
  }

  if(drew){
    virtio_gpu_transfer(0, 0, 1280, 800);
    virtio_gpu_flush(0, 0, 1280, 800);
  }

  return n;
}

int
gwinread(int user_dst, uint64 dst, int n, int off)
{
  // Reading from gwin is not supported in Phase 1
  return -1;
}
