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
#include "user/font.h"

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
    char title[64];         // Window title (null-terminated)
    
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

// Create and attach SHM for a window. Returns pointer to SHM struct, or 0 on failure.
// Also stores the shmid in *out_shmid for later cleanup with sulu_detach().
static inline struct sulu_window_shm* sulu_attach(int shm_key, int width, int height, int *out_shmid) {
    int size = sulu_shm_size(width, height);
    int shmid = shmget(shm_key, size);
    if (shmid < 0) return 0;
    
    struct sulu_window_shm *shm = (struct sulu_window_shm*)shmat(shmid, 0);
    if (shm == (void*)-1) return 0;
    
    if (out_shmid) *out_shmid = shmid;
    return shm;
}

// ============================================================
// All-in-One Window Creation
// ============================================================

// Handle returned by sulu_init() for convenient window management
struct sulu_window {
    struct sulu_window_shm *shm;    // SHM pointer
    uint *pixels;                    // Pixel buffer
    int shmid;                       // SHM ID for cleanup
    int fd;                          // File descriptor from sulu_connect
    int width;                       // Window width
    int height;                      // Window height
};

// All-in-one window creation: attach SHM + connect to Sulu
// Returns 0 on success, -1 on failure
// Usage: struct sulu_window win; if(sulu_init(&win, 640, 480) == 0) { ... }
static inline int sulu_init(struct sulu_window *win, int width, int height) {
    int shm_key = getpid();  // Use PID as unique key
    
    win->shm = sulu_attach(shm_key, width, height, &win->shmid);
    if (!win->shm) return -1;
    
    win->fd = sulu_connect(shm_key, width, height);
    if (win->fd < 0) return -1;
    
    win->pixels = sulu_pixels(win->shm);
    win->width = width;
    win->height = height;
    
    // Store dimensions in SHM for Sulu
    win->shm->width = width;
    win->shm->height = height;
    
    return 0;
}

// Set the window title (max 63 chars)
static inline void sulu_set_title(struct sulu_window_shm *shm, const char *title) {
    int i;
    for (i = 0; i < 63 && title[i]; i++) {
        shm->title[i] = title[i];
    }
    shm->title[i] = '\0';
}

// ============================================================
// Drawing Helpers
// ============================================================

// Clear entire window to a single color
static inline void sulu_clear(struct sulu_window_shm *shm, uint color) {
    uint *pixels = sulu_pixels(shm);
    int total = shm->width * shm->height;
    for (int i = 0; i < total; i++) {
        pixels[i] = color;
    }
}

// Fill a rectangle with a color (x, y, w, h format)
static inline void sulu_fill_rect(struct sulu_window_shm *shm, int x, int y, int w, int h, uint color) {
    uint *pixels = sulu_pixels(shm);
    int stride = shm->width;
    int win_w = shm->width;
    int win_h = shm->height;
    
    // Clamp to window bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > win_w) w = win_w - x;
    if (y + h > win_h) h = win_h - y;
    if (w <= 0 || h <= 0) return;
    
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            pixels[py * stride + px] = color;
        }
    }
}

// Draw a rectangle outline (1px border)
static inline void sulu_draw_rect(struct sulu_window_shm *shm, int x, int y, int w, int h, uint color) {
    uint *pixels = sulu_pixels(shm);
    int stride = shm->width;
    int win_w = shm->width;
    int win_h = shm->height;
    
    // Draw top and bottom edges
    for (int px = x; px < x + w && px < win_w; px++) {
        if (px >= 0) {
            if (y >= 0 && y < win_h) pixels[y * stride + px] = color;
            if (y + h - 1 >= 0 && y + h - 1 < win_h) pixels[(y + h - 1) * stride + px] = color;
        }
    }
    
    // Draw left and right edges
    for (int py = y; py < y + h && py < win_h; py++) {
        if (py >= 0) {
            if (x >= 0 && x < win_w) pixels[py * stride + x] = color;
            if (x + w - 1 >= 0 && x + w - 1 < win_w) pixels[py * stride + (x + w - 1)] = color;
        }
    }
}

// ============================================================
// Text Rendering (using 8x8 bitmap font)
// ============================================================

#define SULU_FONT_WIDTH 8
#define SULU_FONT_HEIGHT 8

// Draw a single character at pixel position (x, y)
// Returns the width of the character (8 pixels)
static inline int sulu_draw_char(struct sulu_window_shm *shm, int x, int y, char ch, uint color) {
    uint *pixels = sulu_pixels(shm);
    int stride = shm->width;
    int win_w = shm->width;
    int win_h = shm->height;
    
    // Map ASCII to font index (font starts at space = 32)
    int idx = ch - 32;
    if (idx < 0 || idx >= 96) idx = 0;  // Default to space for invalid
    
    unsigned char *glyph = font_8x8[idx];
    
    for (int row = 0; row < 8; row++) {
        int py = y + row;
        if (py < 0 || py >= win_h) continue;
        
        unsigned char bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            int px = x + col;
            if (px < 0 || px >= win_w) continue;
            
            // Check if pixel is set (MSB first)
            if (bits & (0x80 >> col)) {
                pixels[py * stride + px] = color;
            }
        }
    }
    
    return SULU_FONT_WIDTH;
}

// Draw a text string at pixel position (x, y)
// Returns the total width drawn
static inline int sulu_draw_text(struct sulu_window_shm *shm, int x, int y, const char *str, uint color) {
    int start_x = x;
    while (*str) {
        if (*str == '\n') {
            x = start_x;
            y += SULU_FONT_HEIGHT;
        } else {
            sulu_draw_char(shm, x, y, *str, color);
            x += SULU_FONT_WIDTH;
        }
        str++;
    }
    return x - start_x;
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
