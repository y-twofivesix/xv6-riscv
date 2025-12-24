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

// Timing Constants
#define SULU_DEFAULT_FPS 60
#define SULU_DEFAULT_FRAME_USEC (1000000 / SULU_DEFAULT_FPS) // ~33.3ms

// Command types (client -> sulu)
#define SULU_CMD_NONE        0
#define SULU_CMD_BLIT        1   // Flush dirty region
#define SULU_CMD_CLOSE       2   // Close window
#define SULU_CMD_RESIZE      3   // Request resize
#define SULU_CMD_SWAP        5   // Swap front/back buffers (double buffering)

// Flags (shm->flags)
#define SULU_FLAG_DOUBLE_BUFFER (1 << 0)

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
    int front_buf;          // Index of front buffer (for double buffering)
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

// ============================================================
// Client API Functions
// ============================================================

// Request a new window via suluctl. Returns SHM key or -1.
// After this, client should: shmat(key) to get sulu_window_shm*
int sulu_request_window(int width, int height, const char *title);

// Get pointer to CURRENT BACK BUFFER within mapped SHM
static inline uint* sulu_pixels(struct sulu_window_shm *shm) {
    uint *base = (uint*)((char*)shm + sizeof(struct sulu_window_shm));
    if (!(shm->flags & SULU_FLAG_DOUBLE_BUFFER)) return base;
    
// Front buffer is what Sulu shows. Back buffer is what client writes to.
    if (shm->front_buf == 0) {
        return base + (shm->width * shm->height);
    } else {
        return base;
    }
}

// Get pointer to FRONT BUFFER (what is currently displayed)
static inline uint* sulu_front_pixels(struct sulu_window_shm *shm) {
    uint *base = (uint*)((char*)shm + sizeof(struct sulu_window_shm));
    if (!(shm->flags & SULU_FLAG_DOUBLE_BUFFER)) return base;
    
    if (shm->front_buf == 0) {
        return base;
    } else {
        return base + (shm->width * shm->height);
    }
}

// Calculate total SHM size needed for a window of given dimensions
static inline int sulu_shm_size(int width, int height, int flags) {
    int buf_size = width * height * 4;
    if (flags & SULU_FLAG_DOUBLE_BUFFER) buf_size *= 2;
    return sizeof(struct sulu_window_shm) + buf_size;
}

// Create and attach SHM for a window. Returns pointer to SHM struct, or 0 on failure.
// Also stores the shmid in *out_shmid for later cleanup with sulu_detach().
static inline struct sulu_window_shm* sulu_attach(int shm_key, int width, int height, int flags, int *out_shmid) {
    int size = sulu_shm_size(width, height, flags);
    int shmid = shmget(shm_key, size);
    if (shmid < 0) {
        printf("sulu: shmget failed for key %d size %d\n", shm_key, size);
        return 0;
    }
    
    struct sulu_window_shm *shm = (struct sulu_window_shm*)shmat(shmid, 0);
    if (shm == (void*)-1) {
        printf("sulu: shmat failed for shmid %d\n", shmid);
        return 0;
    }
    
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
static inline int sulu_init(struct sulu_window *win, int width, int height, int flags) {
    int shm_key = getpid();  // Use PID as unique key
    
    win->shm = sulu_attach(shm_key, width, height, flags, &win->shmid);
    if (!win->shm) return -1;
    
    win->fd = sulu_connect(shm_key, width, height);
    if (win->fd < 0) return -1;
    
    win->pixels = sulu_pixels(win->shm);
    win->width = width;
    win->height = height;
    
    // Store dimensions in SHM for Sulu
    win->shm->width = width;
    win->shm->height = height;
    win->shm->flags = flags;
    win->shm->front_buf = 0;
    
    return 0;
}

// Resize an existing window.
// This allocates a NEW shm segment, notifies Sulu, and updates the 'win' struct.
static inline int sulu_resize(struct sulu_window *win, int width, int height) {
    if (!win || win->fd < 0) return -1;
    
    // Allocate new SHM (preserve flags)
    int new_shmid;
    int new_key = 2000 + (win->fd * 10) + (win->width % 100); // Simple unique key
    struct sulu_window_shm *new_shm = sulu_attach(new_key, width, height, win->shm->flags, &new_shmid);
    if (!new_shm) return -1;
    
    // Initialize new SHM header
    new_shm->win_id = win->shm->win_id;
    new_shm->width = width;
    new_shm->height = height;
    new_shm->flags = win->shm->flags;
    strcpy(new_shm->title, win->shm->title);
    new_shm->cmd_ring.head = 0;
    new_shm->cmd_ring.tail = 0;
    new_shm->event_ring.head = 0;
    new_shm->event_ring.tail = 0;
    new_shm->front_buf = 0;

    // Send Resize Command to /dev/sulu
    // type=2, win_id, new_shmid
    int cmd[3] = { 2, win->shm->win_id, new_shmid };
    if (write(win->fd, cmd, sizeof(cmd)) < 0) {
        // Cleanup new SHM on failure
        shmdt(new_shmid, (void*)new_shm);
        return -1;
    }
    
    // Detach old SHM (Sulu will also detach it when it gets the message)
    shmdt(win->shmid, (void*)win->shm);
    
    // Update local window struct
    win->shm = new_shm;
    win->pixels = sulu_pixels(new_shm);
    win->shmid = new_shmid;
    win->width = width;
    win->height = height;
    
    return 0;
}

// Send SWAP command to Sulu (Double Buffering)
static inline void sulu_swap(struct sulu_window *win) {
    if (!win || !win->shm) return;
    struct sulu_cmd cmd = { .type = SULU_CMD_SWAP };
    sulu_cmd_push(win->shm, &cmd);
    
    // Update local pixel pointer to point to the NEXT back buffer
    // (Note: Sulu will update shm->front_buf internally when it receives the command)
    // For smoothness, we assume the swap will happen.
    // However, if we write too fast, we might overwrite.
    // In a real system, Sulu would send an event back or we'd check front_buf.
    // Here we just update the local pointer for the next frame.
    win->pixels = sulu_pixels(win->shm); 
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

// Draw a circle outline (cx, cy = center, r = radius)
static inline void sulu_draw_circle(struct sulu_window_shm *shm, int cx, int cy, int r, uint color) {
    uint *pixels = sulu_pixels(shm);
    int stride = shm->width;
    int win_w = shm->width;
    int win_h = shm->height;

    int x = r;
    int y = 0;
    int err = 0;

    while (x >= y) {
        int dx[8] = {x, x, -x, -x, y, y, -y, -y};
        int dy[8] = {y, -y, y, -y, x, -x, x, -x};
        for (int i = 0; i < 8; i++) {
            int px = cx + dx[i];
            int py = cy + dy[i];
            if (px >= 0 && px < win_w && py >= 0 && py < win_h)
                pixels[py * stride + px] = color;
        }

        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }
}

// Fill a circle (cx, cy = center, r = radius)
static inline void sulu_fill_circle(struct sulu_window_shm *shm, int cx, int cy, int r, uint color) {
    uint *pixels = sulu_pixels(shm);
    int stride = shm->width;
    int win_w = shm->width;
    int win_h = shm->height;

    int x = r;
    int y = 0;
    int err = 0;

    while (x >= y) {
        // Draw horizontal lines to fill the circle
        int rows[4] = {cy + y, cy - y, cy + x, cy - x};
        int half_widths[4] = {x, x, y, y};
        
        for (int i = 0; i < 4; i++) {
            int py = rows[i];
            if (py < 0 || py >= win_h) continue;
            int x1 = cx - half_widths[i];
            int x2 = cx + half_widths[i];
            if (x1 < 0) x1 = 0;
            if (x2 >= win_w) x2 = win_w - 1;
            for (int px = x1; px <= x2; px++) {
                pixels[py * stride + px] = color;
            }
        }

        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
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
