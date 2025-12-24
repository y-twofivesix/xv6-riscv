#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "virtio.h"

#define SCREEN_W 1280
#define SCREEN_H 800
// VirtIO-GPU Device ID is 16
#define VIRTIO_GPU_DEV_ID 16

// Mapping helper (similar to virtio_disk.c)
#define R(base, r) ((volatile uint32 *)((base) + (r)))

int gui_active = 0;

// static void virtio_gpu_cursor_init(void);
void virtio_gpu_cursor_move(uint32 x, uint32 y);

struct virtio_gpu_config {
  uint32 events_read;
  uint32 events_clear;
  uint32 num_scanouts;
  uint32 reserved;
};

// Control headers and commands
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO  0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF     0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT        0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH     0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_CMD_UPDATE_CURSOR     0x0300
#define VIRTIO_GPU_CMD_MOVE_CURSOR       0x0301

#define VIRTIO_GPU_RESP_OK_NODATA        0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO   0x1101

struct virtio_gpu_ctrl_hdr {
  uint32 type;
  uint32 flags;
  uint64 fence_id;
  uint32 ctx_id;
  uint32 padding;
};

struct virtio_gpu_display_one {
  uint32 x, y, width, height;
  uint32 enabled;
  uint32 flags;
};

struct virtio_gpu_resp_display_info {
  struct virtio_gpu_ctrl_hdr hdr;
  struct virtio_gpu_display_one pmodes[16];
};

struct virtio_gpu_resource_create_2d {
  struct virtio_gpu_ctrl_hdr hdr;
  uint32 resource_id;
  uint32 format;
  uint32 width;
  uint32 height;
};

struct virtio_gpu_mem_entry {
  uint64 addr;
  uint32 length;
  uint32 padding;
};

struct virtio_gpu_resource_attach_backing {
  struct virtio_gpu_ctrl_hdr hdr;
  uint32 resource_id;
  uint32 nr_entries;
};

struct virtio_gpu_set_scanout {
  struct virtio_gpu_ctrl_hdr hdr;
  uint32 x, y, width, height;
  uint32 scanout_id;
  uint32 resource_id;
};

struct virtio_gpu_transfer_to_host_2d {
  struct virtio_gpu_ctrl_hdr hdr;
  uint32 x, y, width, height;
  uint64 offset;
  uint32 resource_id;
  uint32 padding;
};

struct virtio_gpu_resource_flush {
  struct virtio_gpu_ctrl_hdr hdr;
  uint32 x, y, width, height;
  uint32 resource_id;
  uint32 padding;
};

struct virtio_gpu_cursor_pos {
  uint32 scanout_id;
  uint32 x;
  uint32 y;
  uint32 padding;
};

struct virtio_gpu_update_cursor {
  struct virtio_gpu_ctrl_hdr hdr;
  struct virtio_gpu_cursor_pos pos;
  uint32 resource_id;
  uint32 hot_x;
  uint32 hot_y;
  uint32 padding;
};

#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM 1

static struct gpu {
  uint64 base;
  struct virtq_desc *desc;
  struct virtq_avail *avail;
  struct virtq_used *used;
  char free[NUM];
  uint16 used_idx;
  uint16 used_idx1;
  struct spinlock lock;

  // Command/Response buffers for DMA
  struct virtio_gpu_ctrl_hdr req;
  struct virtio_gpu_resp_display_info resp;
  struct virtio_gpu_resource_create_2d create;
  struct {
    volatile uint32 type;
    uint32 padding[7];
  } success;
  
  // Phase 4 buffers
  struct {
    struct virtio_gpu_resource_attach_backing attach;
    struct virtio_gpu_mem_entry entry;
  } ba;
  struct virtio_gpu_set_scanout scan;
  struct virtio_gpu_transfer_to_host_2d xfer;
  struct virtio_gpu_resource_flush flush;
  struct virtio_gpu_update_cursor cursor;

  // Queue 1: cursorq
  struct virtq_desc *desc1;
  struct virtq_avail *avail1;
  struct virtq_used *used1;
  char free1[NUM];
} gpu;

// Real framebuffer back-end (640x480x4 bytes)
// We align it to a page boundary for DMA.
__attribute__((aligned(4096))) 
uint32 framebuffer[1280 * 800];

__attribute__((aligned(4096)))
uint32 cursor_buffer[64 * 64];

static int
alloc_desc(int q)
{
  char *f = (q == 0) ? gpu.free : gpu.free1;
  for(int i = 0; i < NUM; i++){
    if(f[i]){
      f[i] = 0;
      return i;
    }
  }
  return -1;
}

static void
free_desc(int q, int i)
{
  if(i >= NUM) panic("v_gpu free_desc 1");
  struct virtq_desc *d = (q == 0) ? gpu.desc : gpu.desc1;
  char *f = (q == 0) ? gpu.free : gpu.free1;
  d[i].addr = 0;
  d[i].len = 0;
  d[i].flags = 0;
  d[i].next = 0;
  f[i] = 1;
}

static int
virtio_gpu_command(int q, uint32 type, void *cmd, uint32 cmd_len, void *resp, uint32 resp_len)
{
  struct virtq_desc *desc = (q == 0) ? gpu.desc : gpu.desc1;
  struct virtq_avail *avail = (q == 0) ? gpu.avail : gpu.avail1;
  struct virtq_used *used = (q == 0) ? gpu.used : gpu.used1;

  int idx[2];
  idx[0] = alloc_desc(q);
  idx[1] = alloc_desc(q);

  desc[idx[0]].addr = (uint64)cmd;
  desc[idx[0]].len = cmd_len;
  desc[idx[0]].flags = VRING_DESC_F_NEXT;
  desc[idx[0]].next = idx[1];

  desc[idx[1]].addr = (uint64)resp;
  desc[idx[1]].len = resp_len;
  desc[idx[1]].flags = VRING_DESC_F_WRITE;

  avail->ring[avail->idx % NUM] = idx[0];
  __sync_synchronize();
  avail->idx++;
  __sync_synchronize();
  *R(gpu.base, VIRTIO_MMIO_QUEUE_NOTIFY) = q;
  
  release(&gpu.lock);
  while(q == 0 && gpu.used_idx == used->idx)
    ;
  acquire(&gpu.lock);
  
  if(q == 0) gpu.used_idx++;
  
  free_desc(q, idx[0]);
  free_desc(q, idx[1]);

  return 0;
}

static int
virtio_gpu_command_3(int q, uint32 type, void *cmd1, uint32 len1, void *cmd2, uint32 len2, void *resp, uint32 resp_len)
{
  struct virtq_desc *desc = (q == 0) ? gpu.desc : gpu.desc1;
  struct virtq_avail *avail = (q == 0) ? gpu.avail : gpu.avail1;
  struct virtq_used *used = (q == 0) ? gpu.used : gpu.used1;

  int idx[3];
  idx[0] = alloc_desc(q);
  idx[1] = alloc_desc(q);
  idx[2] = alloc_desc(q);

  desc[idx[0]].addr = (uint64)cmd1;
  desc[idx[0]].len = len1;
  desc[idx[0]].flags = VRING_DESC_F_NEXT;
  desc[idx[0]].next = idx[1];

  desc[idx[1]].addr = (uint64)cmd2;
  desc[idx[1]].len = len2;
  desc[idx[1]].flags = VRING_DESC_F_NEXT;
  desc[idx[1]].next = idx[2];

  desc[idx[2]].addr = (uint64)resp;
  desc[idx[2]].len = resp_len;
  desc[idx[2]].flags = VRING_DESC_F_WRITE;

  avail->ring[avail->idx % NUM] = idx[0];
  __sync_synchronize();
  avail->idx++;
  __sync_synchronize();
  *R(gpu.base, VIRTIO_MMIO_QUEUE_NOTIFY) = q;

  release(&gpu.lock);
  while(q == 0 && gpu.used_idx == used->idx)
    ;
  acquire(&gpu.lock);
  
  if(q == 0) gpu.used_idx++;
  free_desc(q, idx[0]);
  free_desc(q, idx[1]);
  free_desc(q, idx[2]);

  return 0;
}

static int
virtio_gpu_cursor_command(void *cmd, uint32 cmd_len)
{
  int idx = alloc_desc(1);
  if(idx < 0) {
    // Reclaim finished descriptors
    while(gpu.used_idx1 != gpu.used1->idx) {
      int id = gpu.used1->ring[gpu.used_idx1 % NUM].id;
      free_desc(1, id);
      gpu.used_idx1++;
    }
    idx = alloc_desc(1);
    if(idx < 0) return -1; // Ring still full
  }

  gpu.desc1[idx].addr = (uint64)cmd;
  gpu.desc1[idx].len = cmd_len;
  gpu.desc1[idx].flags = 0;

  gpu.avail1->ring[gpu.avail1->idx % NUM] = idx;
  __sync_synchronize();
  gpu.avail1->idx++;
  __sync_synchronize();
  *R(gpu.base, VIRTIO_MMIO_QUEUE_NOTIFY) = 1;

  return 0;
}

void
virtio_gpu_init(void)
{
  int found = 0;
  uint64 base = 0;

  for (int i = 0; i < 8; i++) {
    uint64 b = VIRTIO0 + (i * 0x1000);
    uint32 magic = *R(b, VIRTIO_MMIO_MAGIC_VALUE);
    uint32 dev_id = *R(b, VIRTIO_MMIO_DEVICE_ID);
    
    if (magic == 0x74726976 && dev_id != 0) {
      printf("[VirtIO] Slot %d at 0x%lx: ID %d\n", i, b, dev_id);
      if (dev_id == VIRTIO_GPU_DEV_ID) {
        base = b;
        found = 1;
        break;
      }
    }
  }

  if (!found) {
    printf("[GPU] VirtIO-GPU not found.\n");
    return;
  }

  gpu.base = base;
  initlock(&gpu.lock, "virtio_gpu");

  uint32 status = 0;
  *R(base, VIRTIO_MMIO_STATUS) = status; // Reset

  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *R(base, VIRTIO_MMIO_STATUS) = status;

  status |= VIRTIO_CONFIG_S_DRIVER;
  *R(base, VIRTIO_MMIO_STATUS) = status;

  // Features
  uint64 features = *R(base, VIRTIO_MMIO_DEVICE_FEATURES);
  *R(base, VIRTIO_MMIO_DRIVER_FEATURES) = features;

  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *R(base, VIRTIO_MMIO_STATUS) = status;

  if(!(*R(base, VIRTIO_MMIO_STATUS) & VIRTIO_CONFIG_S_FEATURES_OK))
    panic("virtio gpu features_ok failed");

  // Queue 0 (controlq)
  *R(base, VIRTIO_MMIO_QUEUE_SEL) = 0;
  uint32 max = *R(base, VIRTIO_MMIO_QUEUE_NUM_MAX);
  if(max < NUM) panic("virtio gpu queue too short");

  gpu.desc = kalloc();
  gpu.avail = kalloc();
  gpu.used = kalloc();
  memset(gpu.desc, 0, PGSIZE);
  memset(gpu.avail, 0, PGSIZE);
  memset(gpu.used, 0, PGSIZE);

  *R(base, VIRTIO_MMIO_QUEUE_NUM) = NUM;
  *R(base, VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)gpu.desc;
  *R(base, VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)gpu.desc >> 32;
  *R(base, VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)gpu.avail;
  *R(base, VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)gpu.avail >> 32;
  *R(base, VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)gpu.used;
  *R(base, VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)gpu.used >> 32;

  *R(base, VIRTIO_MMIO_QUEUE_READY) = 0x1;

  for(int i = 0; i < NUM; i++) gpu.free[i] = 1;

  // Queue 1 (cursorq)
  *R(base, VIRTIO_MMIO_QUEUE_SEL) = 1;
  max = *R(base, VIRTIO_MMIO_QUEUE_NUM_MAX);
  if(max < NUM) panic("virtio gpu cursor queue too short");

  gpu.desc1 = kalloc();
  gpu.avail1 = kalloc();
  gpu.used1 = kalloc();
  memset(gpu.desc1, 0, PGSIZE);
  memset(gpu.avail1, 0, PGSIZE);
  memset(gpu.used1, 0, PGSIZE);

  *R(base, VIRTIO_MMIO_QUEUE_NUM) = NUM;
  *R(base, VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)gpu.desc1;
  *R(base, VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)gpu.desc1 >> 32;
  *R(base, VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)gpu.avail1;
  *R(base, VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)gpu.avail1 >> 32;
  *R(base, VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)gpu.used1;
  *R(base, VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)gpu.used1 >> 32;

  *R(base, VIRTIO_MMIO_QUEUE_READY) = 0x1;

  for(int i = 0; i < NUM; i++) gpu.free1[i] = 1;

  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(base, VIRTIO_MMIO_STATUS) = status;

  printf("[GPU] Driver initialized at 0x%lx.\n", base);
  printf("[GPU] Starting handshake...\n");
  
  acquire(&gpu.lock);

  // 1. GET_DISPLAY_INFO
  memset(&gpu.req, 0, sizeof(gpu.req));
  gpu.req.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
  virtio_gpu_command(0, VIRTIO_GPU_CMD_GET_DISPLAY_INFO, &gpu.req, sizeof(gpu.req), &gpu.resp, sizeof(gpu.resp));
  
  if (gpu.resp.hdr.type == VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
    printf("[GPU] Display Info: %dx%d (Enabled: %d)\n", 
           gpu.resp.pmodes[0].width, gpu.resp.pmodes[0].height, gpu.resp.pmodes[0].enabled);
  }

  // 2. CREATE_2D Resource (1280x800)
  memset(&gpu.create, 0, sizeof(gpu.create));
  gpu.create.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
  gpu.create.resource_id = 1;
  gpu.create.format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
  gpu.create.width = 1280;
  gpu.create.height = 800;
  virtio_gpu_command(0, VIRTIO_GPU_CMD_RESOURCE_CREATE_2D, &gpu.create, sizeof(gpu.create), (void*)&gpu.success, sizeof(gpu.success));

  if (gpu.success.type == VIRTIO_GPU_RESP_OK_NODATA) {
    printf("[GPU] Resource 1 created (1280x800).\n");
  } else {
    printf("[GPU] Resource creation failed! Resp: 0x%x\n", gpu.success.type);
    release(&gpu.lock);
    return;
  }

  // 3. ATTACH_BACKING
  memset(&gpu.ba, 0, sizeof(gpu.ba));
  gpu.ba.attach.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
  gpu.ba.attach.resource_id = 1;
  gpu.ba.attach.nr_entries = 1;
  gpu.ba.entry.addr = (uint64)framebuffer;
  gpu.ba.entry.length = sizeof(framebuffer);
  virtio_gpu_command_3(0, VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING, &gpu.ba.attach, sizeof(gpu.ba.attach), &gpu.ba.entry, sizeof(gpu.ba.entry), (void*)&gpu.success, sizeof(gpu.success));

  if (gpu.success.type == VIRTIO_GPU_RESP_OK_NODATA) {
    printf("[GPU] Backing attached.\n");
  } else {
    printf("[GPU] Attach backing failed! Resp: 0x%x\n", gpu.success.type);
    return;
  }

  // 4. SET_SCANOUT
  memset(&gpu.scan, 0, sizeof(gpu.scan));
  gpu.scan.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
  gpu.scan.resource_id = 1;
  gpu.scan.scanout_id = 0;
  gpu.scan.width = 1280;
  gpu.scan.height = 800;
  virtio_gpu_command(0, VIRTIO_GPU_CMD_SET_SCANOUT, &gpu.scan, sizeof(gpu.scan), (void*)&gpu.success, sizeof(gpu.success));

  if (gpu.success.type == VIRTIO_GPU_RESP_OK_NODATA) {
    printf("[GPU] Scanout set. Initialization complete!\n");
  } else {
    printf("[GPU] Set scanout failed! Resp: 0x%x\n", gpu.success.type);
  }
  
  release(&gpu.lock);

  // Initial Clear and Flush (Charcoal background)
  for(int i = 0; i < 1280 * 800; i++) framebuffer[i] = 0xFF222222;
  virtio_gpu_transfer(0, 0, 1280, 800);
  virtio_gpu_flush(0, 0, 1280, 800);
  
  gui_active = 1;
  
  // Hide hardware cursor since we're in GUI mode
  uartputc_sync('\033'); uartputc_sync('['); uartputc_sync('?');
  uartputc_sync('2'); uartputc_sync('5'); uartputc_sync('l');
  
  printf("[GPU] VirtIO-GPU initialized and ready.\n");
  printf("[GPU] Initialization complete. Background set.\n");
  
  // virtio_gpu_cursor_init();
}

// Transfer a rectangular region from guest memory to the GPU resource.
void
virtio_gpu_transfer(uint32 x, uint32 y, uint32 w, uint32 h)
{
  acquire(&gpu.lock);
  memset(&gpu.xfer, 0, sizeof(gpu.xfer));
  gpu.xfer.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
  gpu.xfer.resource_id = 1;
  gpu.xfer.x = x;
  gpu.xfer.y = y;
  gpu.xfer.width = w;
  gpu.xfer.height = h;
  // Offset into the backing buffer: (y * stride + x) * bytes_per_pixel
  // Stride = 1280 pixels, bytes_per_pixel = 4 (BGRA)
  gpu.xfer.offset = (y * 1280 + x) * 4;

  virtio_gpu_command(0, VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D, &gpu.xfer, sizeof(gpu.xfer), (void*)&gpu.success, sizeof(gpu.success));
  if(gpu.success.type != VIRTIO_GPU_RESP_OK_NODATA)
    printf("[GPU] Transfer failed! 0x%x\n", gpu.success.type);
  release(&gpu.lock);
}

void
virtio_gpu_flush(uint32 x, uint32 y, uint32 w, uint32 h)
{
  acquire(&gpu.lock);
  memset(&gpu.flush, 0, sizeof(gpu.flush));
  gpu.flush.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
  gpu.flush.resource_id = 1;
  gpu.flush.x = x;
  gpu.flush.y = y;
  gpu.flush.width = w;
  gpu.flush.height = h;

  virtio_gpu_command(0, VIRTIO_GPU_CMD_RESOURCE_FLUSH, &gpu.flush, sizeof(gpu.flush), (void*)&gpu.success, sizeof(gpu.success));
  if(gpu.success.type != VIRTIO_GPU_RESP_OK_NODATA)
    printf("[GPU] Flush failed! 0x%x\n", gpu.success.type);
  release(&gpu.lock);
}
#if 0
void
virtio_gpu_cursor_init(void)
{
  uint32 resource_id = 2; // Resource 1 is framebuffer, 2 is cursor
  
  // 1. Create 64x64 Resource
  memset(&gpu.create, 0, sizeof(gpu.create));
  gpu.create.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
  gpu.create.resource_id = resource_id;
  gpu.create.format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
  gpu.create.width = 64;
  gpu.create.height = 64;
  virtio_gpu_command(0, VIRTIO_GPU_CMD_RESOURCE_CREATE_2D, &gpu.create, sizeof(gpu.create), (void*)&gpu.success, sizeof(gpu.success));

  // 2. Attach Backing
  memset(&gpu.ba, 0, sizeof(gpu.ba));
  gpu.ba.attach.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
  gpu.ba.attach.resource_id = resource_id;
  gpu.ba.attach.nr_entries = 1;
  gpu.ba.entry.addr = (uint64)cursor_buffer;
  gpu.ba.entry.length = sizeof(cursor_buffer);
  virtio_gpu_command_3(0, VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING, &gpu.ba.attach, sizeof(gpu.ba.attach), &gpu.ba.entry, sizeof(gpu.ba.entry), (void*)&gpu.success, sizeof(gpu.success));

  // 3. Draw a white arrow cursor (simple triangle)
  memset(cursor_buffer, 0, sizeof(cursor_buffer));
  for(int y = 0; y < 20; y++){
    for(int x = 0; x <= y; x++){
      cursor_buffer[y * 64 + x] = 0xFFFFFFFF; // White
    }
  }

  // 4. Transfer to Host
  memset(&gpu.xfer, 0, sizeof(gpu.xfer));
  gpu.xfer.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
  gpu.xfer.resource_id = resource_id;
  gpu.xfer.x = 0; gpu.xfer.y = 0;
  gpu.xfer.width = 64; gpu.xfer.height = 64;
  virtio_gpu_command(0, VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D, &gpu.xfer, sizeof(gpu.xfer), (void*)&gpu.success, sizeof(gpu.success));

  // 5. Update Cursor (Enable it)
  memset(&gpu.cursor, 0, sizeof(gpu.cursor));
  gpu.cursor.hdr.type = VIRTIO_GPU_CMD_UPDATE_CURSOR;
  gpu.cursor.resource_id = resource_id;
  gpu.cursor.pos.scanout_id = 0;
  gpu.cursor.pos.x = 200; // Start somewhere visible
  gpu.cursor.pos.y = 200;
  virtio_gpu_cursor_command(&gpu.cursor, sizeof(gpu.cursor));
  
  printf("[GPU] Hardware cursor initialized.\n");
}
#endif

void
virtio_gpu_cursor_move(uint32 x, uint32 y)
{
  acquire(&gpu.lock);
  memset(&gpu.cursor, 0, sizeof(gpu.cursor));
  gpu.cursor.hdr.type = VIRTIO_GPU_CMD_MOVE_CURSOR;
  gpu.cursor.resource_id = 2; // Fixed Resource ID for cursor
  gpu.cursor.pos.scanout_id = 0;
  gpu.cursor.pos.x = x;
  gpu.cursor.pos.y = y;
  
  virtio_gpu_cursor_command(&gpu.cursor, sizeof(gpu.cursor));
  release(&gpu.lock);
}
