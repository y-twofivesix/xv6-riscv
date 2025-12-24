// Sulu Client API - Shared Memory Based
// Zero-syscall runtime communication using ring buffers
//
// Usage:
//   1. Client calls sulu_connect() -> opens /dev/sulu
//   2. Client maps the returned SHM region
//   3. Client writes pixels to buffer, commands to cmd_ring
//   4. Client reads input events from event_ring
//   5. Sulu receives Kernel Event -> Maps Client -> Processes

#ifndef _SULU_CLIENT_H_
#define _SULU_CLIENT_H_

#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int
sulu_connect(int shm_key, int width, int height) {
    int fd = open("/dev/sulu", O_RDWR);
    if(fd < 0) return -1;
    
    // Send register command [CMD=1, key, w, h]
    // Packed command due to sulu_dev.c logic
    int cmd[4] = {1, shm_key, width, height};
    write(fd, cmd, sizeof(cmd));
    
    return fd;
}

// ============================================================
// Shared Memory Layout
// ============================================================
//
// The shared region contains:
// 1. Header (metadata)
// 2. Command ring buffer (client -> sulu)
// 3. Event ring buffer (sulu -> client)
// 4. Pixel buffer
//

#define SULU_CMD_RING_SIZE   16
#define SULU_EVENT_RING_SIZE 32

// Command types (client -> sulu)
#define SULU_CMD_NONE        0
#define SULU_CMD_BLIT        1   // Flush dirty region
#define SULU_CMD_CLOSE       2   // Close window
#define SULU_CMD_RESIZE      3   // Request resize

// Event types (sulu -> client)
#define SULU_EV_NONE         0
#define SULU_EV_KEY          1   // Keyboard event
#define SULU_EV_MOUSE_MOVE   2   // Mouse movement
#define SULU_EV_MOUSE_BTN    3   // Mouse button
#define SULU_EV_FOCUS        4   // Window focus gained/lost

// Command structure (client writes these)
struct sulu_cmd {
    int type;               // SULU_CMD_*
    union {
        struct { int x, y, w, h; } blit;  // Dirty region
        struct { int w, h; } resize;
    };
};

// Event structure (sulu writes these)
struct sulu_event {
    int type;               // SULU_EV_*
    int code;               // Key code or button
    int value;              // 1=press, 0=release, or delta
    int x, y;               // Mouse position (for mouse events)
};

// Ring buffer header (used for both cmd and event rings)
struct sulu_ring {
    volatile int head;      // Written by producer
    volatile int tail;      // Written by consumer
    int size;               // Ring capacity
};

// Window shared memory header
struct sulu_window_shm {
    // Metadata
    int win_id;
    int width;
    int height;
    int flags;              // SULU_FLAG_*
    uint bgcolor;           // Window background color (ARGB)
    
    // Command ring (client -> sulu)
    struct sulu_ring cmd_ring;
    struct sulu_cmd  cmd_buf[SULU_CMD_RING_SIZE];
    
    // Event ring (sulu -> client)
    struct sulu_ring event_ring;
    struct sulu_event event_buf[SULU_EVENT_RING_SIZE];
    
    // Pixel buffer starts after this header
    // Access via: (uint*)((char*)shm + sizeof(struct sulu_window_shm))
};

// ============================================================
// Client API Functions
// ============================================================

// Request a new window via suluctl. Returns SHM key or -1.
// After this, client should: shmat(key) to get sulu_window_shm*
int sulu_request_window(int width, int height, const char *title);

// Get pointer to pixel buffer within mapped SHM
static inline uint* sulu_pixels(struct sulu_window_shm *shm) {
    return (uint*)((char*)shm + sizeof(struct sulu_window_shm));
}

// Calculate total SHM size needed for a window of given dimensions
// Size = header struct + (width * height * 4 bytes per pixel)
static inline int sulu_shm_size(int width, int height) {
    return sizeof(struct sulu_window_shm) + (width * height * 4);
}

// ============================================================
// Ring Buffer Helpers (lock-free single producer/consumer)
// ============================================================

// Push a command to the ring. Returns 0 on success, -1 if full.
static inline int sulu_cmd_push(struct sulu_window_shm *shm, struct sulu_cmd *cmd) {
    struct sulu_ring *r = &shm->cmd_ring;
    int next = (r->head + 1) % SULU_CMD_RING_SIZE;
    if (next == r->tail) return -1; // Full
    shm->cmd_buf[r->head] = *cmd;
    __sync_synchronize(); // Memory barrier
    r->head = next;
    return 0;
}

// Pop an event from the ring. Returns 0 on success, -1 if empty.
static inline int sulu_event_pop(struct sulu_window_shm *shm, struct sulu_event *ev) {
    struct sulu_ring *r = &shm->event_ring;
    if (r->head == r->tail) return -1; // Empty
    *ev = shm->event_buf[r->tail];
    __sync_synchronize(); // Memory barrier
    r->tail = (r->tail + 1) % SULU_EVENT_RING_SIZE;
    return 0;
}

// Convenience: Push a BLIT command
static inline void sulu_blit(struct sulu_window_shm *shm, int x, int y, int w, int h) {
    struct sulu_cmd cmd = { .type = SULU_CMD_BLIT, .blit = {x, y, w, h} };
    sulu_cmd_push(shm, &cmd);
}

// Convenience: Push a CLOSE command
static inline void sulu_close(struct sulu_window_shm *shm) {
    struct sulu_cmd cmd = { .type = SULU_CMD_CLOSE };
    sulu_cmd_push(shm, &cmd);
}

// Convenience: Detach shared memory
static inline void sulu_detach(int shmid, void *shm) {
    shmdt(shmid, shm);
}

// Convenience: Fill a rectangle with color
static inline void sulu_fill(struct sulu_window_shm *shm, int x1, int y1, int x2, int y2, uint color) {
    uint *pixels = sulu_pixels(shm);
    int stride = shm->width;
    for(int y = y1; y < y2; y++) {
        for(int x = x1; x < x2; x++) {
            pixels[y * stride + x] = color;
        }
    }
}

// Check if events are available
static inline int sulu_event_available(struct sulu_window_shm *shm) {
    struct sulu_ring *r = &shm->event_ring;
    return r->head != r->tail;
}

// Push an event (server-side helper for Sulu)
static inline int sulu_event_push(struct sulu_window_shm *shm, struct sulu_event *ev) {
    struct sulu_ring *r = &shm->event_ring;
    int next = (r->head + 1) % SULU_EVENT_RING_SIZE;
    if (next == r->tail) return -1; // Full
    shm->event_buf[r->head] = *ev;
    __sync_synchronize();
    r->head = next;
    return 0;
}

#endif // _SULU_CLIENT_H_
