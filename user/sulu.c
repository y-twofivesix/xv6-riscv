#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "user/font.h"
#include "user/sulu_client.h"

// Screen
#define SCREEN_W 1280
#define SCREEN_H 800

#define BACK_COLOR (uint)0xFF08113B
#define CURSOR_COLOR (uint)0xFFFF0000
#define ACTION_CURSOR_COLOR (uint)0xFF00A0FF
#define UNFOCUS_COLOR (uint)0xFF120A8F
#define FOCUS_COLOR (uint)0xFF00FFCB
#define TERM_BACK (uint)0x55040720
#define TITLE_BAR_HEIGHT 20
#define CLOSE_BTN_SIZE 14
#define CLOSE_BTN_NORMAL (uint)0xFF8B0000
#define CLOSE_BTN_HOVER (uint)0xFFFF0000

#define ENABLE_SHADOWS 1
#define SHADOW_OFFSET 6
#define SHADOW_COLOR (uint)0xFF04081F  // Deep darkened color

// Input Event Codes
#define EV_ABS 0x03
#define EV_KEY 0x01
#define EV_REL 0x02
#define ABS_X 0x00
#define ABS_Y 0x01
#define REL_WHEEL 0x08
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112

// Keyboard
#define KEY_ESC 1
#define KEY_1 2
#define KEY_E 18
#define KEY_LEFTCTRL 29
#define KEY_RIGHTCTRL 97
#define KEY_LEFTSHIFT 42
#define KEY_RIGHTSHIFT 54
#define KEY_CAPSLOCK 58
#define KEY_UP 103
#define KEY_DOWN 108
#define KEY_LEFT 105
#define KEY_RIGHT 106

struct input_event {
  uint16 type;
  uint16 code;
  uint32 value;
};

int shift_state = 0;
int ctrl_pressed = 0;
int capslock_state = 0;

// Window type (all windows are now client windows)
#define WIN_TYPE_CLIENT   1

typedef struct Window {
  int id;
  int x, y;
  int w, h;
  uint *buf; 
  struct Window *next;
  
  int type;                       // Always WIN_TYPE_CLIENT
  int client_pid;                 // PID of client process
  int shmid;                      // Shared memory ID
  struct sulu_window_shm *shm;    // Shared memory header
} Window;

// Globals
uint *fb;
Window *windows = 0;
int next_win_id = 1;
int mouse_x = SCREEN_W / 2;
int mouse_y = SCREEN_H / 2;
int mouse_btn = 0;
Window *drag_win = 0;
int drag_off_x, drag_off_y;
Window *focus_win = 0;

#define SULU_FRAME_CYCLES (10000000 / SULU_DEFAULT_FPS)
#define INPUT_DRAIN_LIMIT 20 // Max input events to process per loop
#define CLIENT_CMD_LIMIT  1 // Max client commands to process per loop per window

Window* find_window_at(int x, int y);

// ============================================================================
// Cursor Bitmaps (10x10)
// ============================================================================
static uint8 cursor_arrow[100] = {
  1,1,0,0,0,0,0,0,0,0,
  1,1,1,0,0,0,0,0,0,0,
  1,1,1,1,0,0,0,0,0,0,
  1,1,1,1,1,0,0,0,0,0,
  1,1,1,1,1,1,0,0,0,0,
  1,1,1,1,1,1,1,0,0,0,
  1,1,1,1,1,1,1,1,0,0,
  1,1,1,1,0,0,0,0,0,0,
  1,0,0,1,1,0,0,0,0,0,
  0,0,0,0,1,1,0,0,0,0,
};

static uint8 cursor_ibeam[100] = {
  0,1,1,1,1,1,1,1,1,0,
  0,0,0,0,1,1,0,0,0,0,
  0,0,0,0,1,1,0,0,0,0,
  0,0,0,0,1,1,0,0,0,0,
  0,0,0,0,1,1,0,0,0,0,
  0,0,0,0,1,1,0,0,0,0,
  0,0,0,0,1,1,0,0,0,0,
  0,0,0,0,1,1,0,0,0,0,
  0,0,0,0,1,1,0,0,0,0,
  0,1,1,1,1,1,1,1,1,0,
};

// ============================================================================
// Z-Index Based Compositing System
// ============================================================================

// Rectangle helper
typedef struct {
    int x, y, w, h;
} Rect;

// Cursor state (highest z-index, always on top)
static Rect cursor_rect = {640, 400, 10, 10};

// Dirty rectangle accumulator (union of all dirty regions)
static Rect dirty = {0, 0, 0, 0};
static int dirty_valid = 0;

// Check if two rectangles intersect
int rects_intersect(Rect *a, Rect *b) {
    return !(a->x + a->w <= b->x || b->x + b->w <= a->x ||
             a->y + a->h <= b->y || b->y + b->h <= a->y);
}

// Mark a region as dirty (will be composited later)
void mark_dirty(int x, int y, int w, int h) {
    if (!dirty_valid) {
        dirty.x = x;
        dirty.y = y;
        dirty.w = w;
        dirty.h = h;
        dirty_valid = 1;
    } else {
        // Union the rectangles
        int x2 = dirty.x + dirty.w;
        int y2 = dirty.y + dirty.h;
        if (x < dirty.x) dirty.x = x;
        if (y < dirty.y) dirty.y = y;
        if (x + w > x2) x2 = x + w;
        if (y + h > y2) y2 = y + h;
        dirty.w = x2 - dirty.x;
        dirty.h = y2 - dirty.y;
    }
}

// Composite a specific region in z-order: background -> windows -> cursor
void composite_region(Rect *r) {
    // Clamp to screen
    int rx = r->x < 0 ? 0 : r->x;
    int ry = r->y < 0 ? 0 : r->y;
    int rx2 = r->x + r->w;
    int ry2 = r->y + r->h;
    if (rx2 > SCREEN_W) rx2 = SCREEN_W;
    if (ry2 > SCREEN_H) ry2 = SCREEN_H;
    
    // 1. Draw background for this region
    for (int y = ry; y < ry2; y++) {
        for (int x = rx; x < rx2; x++) {
            fb[y * SCREEN_W + x] = BACK_COLOR;
        }
    }
    
    // 2. Draw windows in z-order (linked list order = z-order for now)
    Window *w = windows;
    while (w) {
        // Check if window intersects region
            if (!(w->x + w->w + (ENABLE_SHADOWS ? SHADOW_OFFSET : 0) <= rx || rx2 <= w->x ||
                  w->y + w->h + (ENABLE_SHADOWS ? SHADOW_OFFSET : 0) <= ry || ry2 <= w->y)) {
                // Draw overlapped portion (including shadow)
                int wx1 = (rx > w->x) ? rx : w->x;
                int wy1 = (ry > w->y) ? ry : w->y;
                int wx2 = (rx2 < w->x + w->w + (ENABLE_SHADOWS ? SHADOW_OFFSET : 0)) ? rx2 : w->x + w->w + (ENABLE_SHADOWS ? SHADOW_OFFSET : 0);
                int wy2 = (ry2 < w->y + w->h + (ENABLE_SHADOWS ? SHADOW_OFFSET : 0)) ? ry2 : w->y + w->h + (ENABLE_SHADOWS ? SHADOW_OFFSET : 0);
            
                // Render shadow first
#if ENABLE_SHADOWS
                for (int y = wy1; y < wy2; y++) {
                    for (int x = wx1; x < wx2; x++) {
                        int buf_x = x - w->x;
                        int buf_y = y - w->y;
                        
                        // Is this pixel in the shadow area (but not the window itself)?
                        if (buf_x >= SHADOW_OFFSET && buf_x < w->w + SHADOW_OFFSET &&
                            buf_y >= SHADOW_OFFSET && buf_y < w->h + SHADOW_OFFSET) {
                            // Only draw shadow if not already covered by this window
                            if (buf_x < SHADOW_OFFSET || buf_x >= w->w ||
                                buf_y < SHADOW_OFFSET || buf_y >= w->h) {
                                fb[y * SCREEN_W + x] = SHADOW_COLOR;
                            }
                        }
                    }
                }
#endif

                // Client window: TITLE_BAR_HEIGHT + client buffer
                for (int y = wy1; y < wy2; y++) {
                    for (int x = wx1; x < wx2; x++) {
                        int buf_x = x - w->x;
                        int buf_y = y - w->y;
                        
                        if (buf_x < 0 || buf_x >= w->w || buf_y < 0 || buf_y >= w->h)
                            continue;
                        
                        if(buf_y < TITLE_BAR_HEIGHT) {
                        // Title bar background
                        uint bar_color = (w == focus_win) ? FOCUS_COLOR : UNFOCUS_COLOR;
                        uint text_color = (w == focus_win) ? 0xFF000000 : 0xFFFFFFFF;
                        
                        // Check if this pixel is part of title text
                        int text_y = (TITLE_BAR_HEIGHT - 8) / 2;  // Center 8px font in title bar
                        if(buf_y >= text_y && buf_y < text_y + 8 && w->shm && w->shm->title[0]) {
                            int font_row = buf_y - text_y;
                            int text_x = 6;  // Left padding
                            int char_idx = buf_x - text_x;
                            if(char_idx >= 0) {
                                int char_num = char_idx / 8;
                                int pixel_col = char_idx % 8;
                                if(char_num < 60 && w->shm->title[char_num]) {
                                    int glyph_idx = w->shm->title[char_num] - 32;
                                    if(glyph_idx >= 0 && glyph_idx < 96) {
                                        if(font_8x8[glyph_idx][font_row] & (0x80 >> pixel_col)) {
                                            fb[y * SCREEN_W + x] = text_color;
                                            continue;
                                        }
                                    }
                                }
                            }
                            
                            // w:N p:N on right side (format: "w:XXX p:XXX")
                            char info[16];
                            int n = 0;
                            info[n++] = 'w'; info[n++] = ':';
                            int wid = w->id;
                            if(wid >= 100) info[n++] = '0' + (wid / 100) % 10;
                            if(wid >= 10) info[n++] = '0' + (wid / 10) % 10;
                            info[n++] = '0' + wid % 10;
                            info[n++] = ' '; info[n++] = 'p'; info[n++] = ':';
                            int pid = w->client_pid;
                            if(pid >= 100) info[n++] = '0' + (pid / 100) % 10;
                            if(pid >= 10) info[n++] = '0' + (pid / 10) % 10;
                            info[n++] = '0' + pid % 10;
                            info[n] = 0;
                            
                            int info_x = w->w - (n * 8) - 24;  // Shifted left to clear close button
                            int info_char_idx = buf_x - info_x;
                            if(info_char_idx >= 0 && info_char_idx < n * 8) {
                                int char_num = info_char_idx / 8;
                                int pixel_col = info_char_idx % 8;
                                if(char_num < n) {
                                    int glyph_idx = info[char_num] - 32;
                                    if(glyph_idx >= 0 && glyph_idx < 96) {
                                        if(font_8x8[glyph_idx][font_row] & (0x80 >> pixel_col)) {
                                            fb[y * SCREEN_W + x] = text_color;
                                            continue;
                                        }
                                    }
                                }
                            }
                        }
                        // Close button
                        int btn_x = w->x + w->w - CLOSE_BTN_SIZE - 4;
                        int btn_y = w->y + 3;
                        if (x >= btn_x && x < btn_x + CLOSE_BTN_SIZE &&
                            y >= btn_y && y < btn_y + CLOSE_BTN_SIZE) {
                            int is_hover = (mouse_x >= btn_x && mouse_x < btn_x + CLOSE_BTN_SIZE &&
                                            mouse_y >= btn_y && mouse_y < btn_y + CLOSE_BTN_SIZE);
                            fb[y * SCREEN_W + x] = is_hover ? CLOSE_BTN_HOVER : CLOSE_BTN_NORMAL;
                        } else {
                            fb[y * SCREEN_W + x] = bar_color;
                        }
                    } else {
                        // Client pixel buffer (offset by title bar)
                        int client_y = buf_y - TITLE_BAR_HEIGHT;
                        fb[y * SCREEN_W + x] = w->buf[client_y * w->w + buf_x];
                    }
                }
            }
            
        }
        w = w->next;
    }
    
        // 3. Draw cursor last (highest z-index)
        Window *hit = find_window_at(cursor_rect.x, cursor_rect.y);
        uint8 *bitmap = cursor_arrow;
        uint cursor_color = CURSOR_COLOR;

        if(hit && hit->type == WIN_TYPE_CLIENT && hit->shm) {
            int local_x = cursor_rect.x - hit->x;
            int local_y = cursor_rect.y - hit->y;
            
            // Check if hovering actionable area (close button)
            if(local_y < TITLE_BAR_HEIGHT && local_x >= hit->w - CLOSE_BTN_SIZE - 4 && local_x < hit->w - 4) {
                cursor_color = ACTION_CURSOR_COLOR;
            }

            if(local_y >= TITLE_BAR_HEIGHT) { // Only use client cursor in content area
                if(hit->shm->cursor_type == SULU_CURSOR_IBEAM) {
                    bitmap = cursor_ibeam;
                }
            }
        }

        if (!(cursor_rect.x + cursor_rect.w <= rx || rx2 <= cursor_rect.x ||
              cursor_rect.y + cursor_rect.h <= ry || ry2 <= cursor_rect.y)) {
            int cx1 = (rx > cursor_rect.x) ? rx : cursor_rect.x;
            int cy1 = (ry > cursor_rect.y) ? ry : cursor_rect.y;
            int cx2 = (rx2 < cursor_rect.x + 10) ? rx2 : cursor_rect.x + 10;
            int cy2 = (ry2 < cursor_rect.y + 10) ? ry2 : cursor_rect.y + 10;
            
            for (int y = cy1; y < cy2; y++) {
                for (int x = cx1; x < cx2; x++) {
                    int bx = x - cursor_rect.x;
                    int by = y - cursor_rect.y;
                    if(bitmap[by * 10 + bx]) {
                        fb[y * SCREEN_W + x] = cursor_color;
                    }
                }
            }
        }
}

// Composite and flush all dirty regions
void composite_dirty_and_flush() {
    if (!dirty_valid) return;
    
    // Composite the dirty region
    composite_region(&dirty);
    
    // Flush to GPU (partial flush now that kernel is fixed)
    int x = dirty.x < 0 ? 0 : dirty.x;
    int y = dirty.y < 0 ? 0 : dirty.y;
    int w = dirty.w;
    int h = dirty.h;
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w > 0 && h > 0) {
        gpu_flush_rect(x, y, w, h);
    }
    
    dirty_valid = 0;
}

// Move cursor - marks dirty and composites
void cursor_move(int new_x, int new_y) {
    // Clamp
    if (new_x > SCREEN_W - 10) new_x = SCREEN_W - 10;
    if (new_y > SCREEN_H - 10) new_y = SCREEN_H - 10;
    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    
    // Mark old position dirty
    mark_dirty(cursor_rect.x, cursor_rect.y, 10, 10);

    // If we were hovering a close button, mark it dirty so it transitions back
    Window *old_hit = find_window_at(cursor_rect.x, cursor_rect.y);
    if(old_hit && old_hit->type == WIN_TYPE_CLIENT) {
        int bx = cursor_rect.x - old_hit->x;
        int by = cursor_rect.y - old_hit->y;
        if(by < TITLE_BAR_HEIGHT && bx >= old_hit->w - CLOSE_BTN_SIZE - 4 && bx < old_hit->w - 4) {
             mark_dirty(old_hit->x + old_hit->w - CLOSE_BTN_SIZE - 4, old_hit->y + 3, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE);
        }
    }
    
    // Update position
    cursor_rect.x = new_x;
    cursor_rect.y = new_y;
    
    // Mark new position dirty
    mark_dirty(new_x, new_y, 10, 10);

    // If we are now hovering a close button, mark it dirty so it transitions to bright red
    Window *new_hit = find_window_at(new_x, new_y);
    if(new_hit && new_hit->type == WIN_TYPE_CLIENT) {
        int bx = new_x - new_hit->x;
        int by = new_y - new_hit->y;
        if(by < TITLE_BAR_HEIGHT && bx >= new_hit->w - CLOSE_BTN_SIZE - 4 && bx < new_hit->w - 4) {
             mark_dirty(new_hit->x + new_hit->w - CLOSE_BTN_SIZE - 4, new_hit->y + 3, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE);
        }
    }
    
    // Composite and flush removed (now centralized in main loop)
}

// Mark a window's bounds as dirty
void window_mark_dirty(Window *w) {
    if (!w) return;
    int extra = ENABLE_SHADOWS ? SHADOW_OFFSET : 0;
    mark_dirty(w->x, w->y, w->w + extra, w->h + extra);
}

// Close and free a window
void close_window(Window *w) {
    if(!w) return;

    // 1. Mark dirty (so area gets redrawn)
    int extra = ENABLE_SHADOWS ? SHADOW_OFFSET : 0;
    mark_dirty(w->x, w->y, w->w + extra, w->h + extra);

    // 2. Unlink from list
    if(windows == w) {
        windows = w->next;
    } else {
        Window *curr = windows;
        while(curr && curr->next != w) curr = curr->next;
        if(curr) curr->next = w->next;
    }

    // 3. Handle Focus
    if(focus_win == w) {
        focus_win = windows; // Focus top window
        if(focus_win) window_mark_dirty(focus_win); // Update its title bar
    }

    // 4. Cleanup
    if(w->shm) sulu_detach(w->shmid, w->shm); 
    free(w);
}

// Spawn a client window (for external programs using Sulu API)
Window*
spawn_client_window(int client_pid, int shm_key, int width, int height)
{
  printf("sulu: SPAWN pid=%d key=%d w=%d h=%d\n", client_pid, shm_key, width, height);
  
  // Map the client's shared memory
  int shmid = shmget(shm_key, 0);  // Use existing
  if(shmid < 0) {
  printf("sulu: failed to get client shm key=%d\n", shm_key);
    return 0;
  }
  
  struct sulu_window_shm *shm = (struct sulu_window_shm*)shmat(shmid, 0);
  printf("sulu: shmat returned %p for shmid=%d\n", shm, shmid);
  if(shm == (void*)-1) {
    printf("sulu: failed to attach client shm\n");
    return 0;
  }
  
  // Calculate total size including header
  int total_height = height + 20; // Add title bar
  
  Window *win = malloc(sizeof(Window));
  win->id = next_win_id++;
  win->x = 100 + (next_win_id * 30) % 400;  // Cascade position
  win->y = 100 + (next_win_id * 30) % 300;
  win->w = width;
  win->h = total_height;
  win->buf = sulu_front_pixels(shm);  // Use the FRONT buffer for display
  win->next = 0;
  win->type = WIN_TYPE_CLIENT;
  win->client_pid = client_pid;
  win->shmid = shmid;
  win->shm = shm;
  
  printf("sulu: window id=%d shm=%p buf=%p heap=%p\n", 
         win->id, shm, win->buf, sbrk(0));
  
  // Link into window list (metadata already set by client in many cases)
  shm->win_id = win->id;
  // If client didn't set width/height, do it now
  if(shm->width == 0) shm->width = width;
  if(shm->height == 0) shm->height = height;
  
  shm->cmd_ring.head = 0;
  shm->cmd_ring.tail = 0;
  shm->cmd_ring.size = SULU_CMD_RING_SIZE;
  shm->event_ring.head = 0;
  shm->event_ring.tail = 0;
  shm->event_ring.size = SULU_EVENT_RING_SIZE;
  
  // Clear the client's pixel buffer using their bgcolor (or black if not set)
  uint bgcolor = shm->bgcolor ? shm->bgcolor : 0xFF000000;
  for(int i = 0; i < width * height; i++)
    win->buf[i] = bgcolor;
  
  // Link into window list
  if(!windows) windows = win;
  else {
    struct Window *curr = windows;
    while(curr->next) curr = curr->next;
    curr->next = win;
  }
  focus_win = win;
  return win;
}

// Move window to the end of the list (top of z-order)
void
window_raise(Window *w)
{
  if(!windows || !w) return;
  if(w->next == 0) return; // Already at top

  // Remove from current position
  if(windows == w){
      windows = w->next;
  } else {
      Window *prev = windows;
      while(prev && prev->next != w) prev = prev->next;
      if(prev) prev->next = w->next;
  }
  
  // Append to end
  Window *curr = windows;
  while(curr->next) curr = curr->next;
  curr->next = w;
  w->next = 0;
}

// Composite all windows AND cursor (using z-index: bg -> windows -> cursor)
void
composite()
{
  // Clear to background
  for(int i = 0; i < SCREEN_W * SCREEN_H; i++) fb[i] = BACK_COLOR;
  
  // Draw windows
  Window *w = windows;
  while(w){
      int x_end = w->x + w->w;
      int y_end = w->y + w->h;
      if(x_end > SCREEN_W) x_end = SCREEN_W;
      if(y_end > SCREEN_H) y_end = SCREEN_H;
      
      for(int y = w->y, dy = 0; y < y_end; y++, dy++){
          if(y < 0) continue;
          for(int x = w->x, dx = 0; x < x_end; x++, dx++){
               if(x < 0) continue;
               if(w->type == WIN_TYPE_CLIENT) {
                   // Client window: title bar (TITLE_BAR_HEIGHT) + client buffer
                   if(dy < TITLE_BAR_HEIGHT) {
                       uint color = (w == focus_win) ? FOCUS_COLOR : UNFOCUS_COLOR;
                       fb[y * SCREEN_W + x] = color;
                   } else {
                       int client_y = dy - TITLE_BAR_HEIGHT;
                       fb[y * SCREEN_W + x] = w->buf[client_y * w->w + dx];
                   }
               } else {
                   fb[y * SCREEN_W + x] = w->buf[dy * w->w + dx];
               }
          }
      }
      w = w->next;
  }
  
  // Draw cursor on top (highest z-index)
  for(int dy = 0; dy < 10; dy++){
      for(int dx = 0; dx < 10; dx++){
          int px = cursor_rect.x + dx;
          int py = cursor_rect.y + dy;
          if(px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H){
              fb[py * SCREEN_W + px] = CURSOR_COLOR;
          }
      }
  }
}

Window* find_window_at(int x, int y){
  Window *w = windows;
  Window *hit = 0;
  while(w){
      if(x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h) hit = w;
      w = w->next;
  }
  return hit;
}

// Helper to focus and raise a window
void focus_window(Window *w) {
    if(!w) return;
    
    // Changing focus?
    Window *old_focus = focus_win;
    focus_win = w;
    
    // Raise to top of Z-order
    window_raise(w);
    
    // Mark windows dirty for repainting (border color change)
    if(old_focus && old_focus != w) window_mark_dirty(old_focus);
    window_mark_dirty(w);
}

int
main(int argc, char *argv[])
{

  int shmid = shmget(SHM_FB, 0);
  if(shmid < 0) exit(1);
  fb = (uint*) shmat(shmid, 0);
  if((uint64)fb == -1) exit(1);
  
  // Register as Server by opening /dev/sulu
  // Note: We use O_RDWR.
  int sulu_fd = open("/dev/sulu", O_RDWR);
  if(sulu_fd < 0) {
      printf("sulu: failed to open /dev/sulu\n");
      exit(1);
  }
  
  // Suspension state
  int suspended = 0;
  
  // Cursor state for optimization
  int prev_mouse_x = -1, prev_mouse_y = -1;
  
  int input_fd = open("/dev/input", O_RDONLY);
  if(input_fd < 0) exit(1);
  
  // Initial full screen composite and flush
  composite();
  gpu_flush();
  
  // Auto-start initial terminal
  if(fork() == 0) {
    char *argv_term[] = { "terminal", 0 };
    exec("/bin/terminal", argv_term);
    printf("sulu: failed to start terminal\n");
    exit(1);
  }
  
  struct input_event ev;
  // Define message struct locally or via header? 
  // We need the struct definition from sulu_dev.c!
  // It's not in a header yet. Let's define it here matching the kernel one.
  struct sulu_msg {
    int type;
    int pid;
    int val1;
    int val2;
  };


  while(1){
      uint64 loop_start = rdtime();
      int did_work = 0;
      
      // 1. Process Window System Events (Connect/Disconnect)
      struct sulu_msg msg;
      while(read(sulu_fd, &msg, sizeof(msg)) == sizeof(msg)) {
          did_work = 1;
          if(msg.type == 1) { // SULU_EVENT_CONNECT
              int pid = msg.pid;
              int key = msg.val1;
              // Unpack val2: (width << 16) | height
              int w = (msg.val2 >> 16) & 0xFFFF;
              int h = msg.val2 & 0xFFFF;
              
              // printf("sulu: connect pid=%d key=%d w=%d h=%d\n", pid, key, w, h);
              Window *new_win = spawn_client_window(pid, key, w, h);
              if(new_win) {
                window_mark_dirty(new_win);
              }
          } else if(msg.type == 2) { // SULU_EVENT_DISCONNECT
              // Find and close all windows for this PID
              Window *w = windows;
              while(w) {
                  Window *next_w = w->next;
                  if(w->type == WIN_TYPE_CLIENT && w->client_pid == msg.pid) {
                      close_window(w);
                  }
                  w = next_w;
              }
              // Force full redraw to be safe
              composite(); 
              gpu_flush();
          } else if(msg.type == 4) { // SULU_EVENT_RESIZE
              int win_id = msg.val1;
              int new_shmid = msg.val2;
              
              Window *w = windows;
              while(w) {
                  if(w->id == win_id) {
                      struct sulu_window_shm *new_shm = (struct sulu_window_shm*)shmat(new_shmid, 0);
                      if(new_shm != (void*)-1) {
                          // Mark old area dirty
                          window_mark_dirty(w);
                          
                          // Detach old
                          shmdt(w->shmid, w->shm);
                          
                          // Update window from new SHM header
                          w->shmid = new_shmid;
                          w->shm = new_shm;
                          w->buf = sulu_pixels(new_shm);
                          w->w = new_shm->width;
                          w->h = new_shm->height + TITLE_BAR_HEIGHT;
                          
                          // Mark new area dirty
                          window_mark_dirty(w);
                      }
                      break;
                  }
                  w = w->next;
              }
          }
      } 
      
      // 0.6. Process client cmd_rings (BLIT commands)
      // Note: We still poll SHM command rings for high-perf rendering commands
      // This is the "Hybrid" part!
      {
        Window *w = windows;
        while(w) {
          Window *next_w = w->next; // Save next pointer in case w is freed
          if(w->type == WIN_TYPE_CLIENT && w->shm) {
            
            // Note: sys_exists check REMOVED (kernel notifies us via DISCONNECT event)

            // DEBUG: Print before accessing SHM (disabled - too noisy)
            // printf("sulu: win=%d shm=%p\n", w->id, w->shm);
            
            struct sulu_ring *r = &w->shm->cmd_ring;
            int closed = 0;
            int cmds_processed = 0;
            while(r->head != r->tail && cmds_processed++ < CLIENT_CMD_LIMIT) {
              did_work = 1;
              struct sulu_cmd *cmd = &w->shm->cmd_buf[r->tail];
              if(cmd->type == SULU_CMD_BLIT) {
                // Mark window region dirty and composite
                window_mark_dirty(w);
                // Batch flush: Don't flush here, wait for end of loop
                // composite_dirty_and_flush();
              } else if(cmd->type == SULU_CMD_CLOSE) {
                close_window(w);
                closed = 1;
                break; // Window is gone
              } else if(cmd->type == SULU_CMD_SWAP) {
                if(w->shm->flags & SULU_FLAG_DOUBLE_BUFFER) {
                  // Toggle front buffer index
                  w->shm->front_buf = (w->shm->front_buf == 0) ? 1 : 0;
                  // Update Sulu's view to the new front buffer
                  w->buf = sulu_front_pixels(w->shm);
                  // Mark full window dirty to redraw from new buffer
                  window_mark_dirty(w);
                }
              }
              r->tail = (r->tail + 1) % SULU_CMD_RING_SIZE;
            }
            if(closed) {
               w = next_w;
               continue;
            }
          }
          w = next_w;
        }
      }
      
      // If suspended, don't process input normally
      // BUT allow ESC key to resume as emergency fallback
      if(suspended) {
        // Check for ESC key to resume
        if(readavail(input_fd) > 0){
          struct input_event ev;
          int n = read(input_fd, &ev, sizeof(ev));
          if(n == sizeof(ev) && ev.type == EV_KEY && ev.code == KEY_ESC && ev.value == 1){
            // ESC pressed - emergency resume!
            suspended = 0;
            printf("sulu: emergency resume via ESC key\n");
            composite();
            gpu_flush();
          }
        }
        sleep(10);  // sleep for 100 ticks (approx 1 second)
        continue;
      }
      
      // 1. Process Input
      int n = 0; 

      // Drain input queue (up to a limit to prevent livelock)
      int input_limit = INPUT_DRAIN_LIMIT; 
      while(readavail(input_fd) > 0 && input_limit-- > 0){
          n = read(input_fd, &ev, sizeof(ev));
          if(n == sizeof(ev)){
              did_work = 1;
              if(ev.type == EV_ABS){
                  if(ev.code == ABS_X) mouse_x = (ev.value * SCREEN_W) / 32767;
                  if(ev.code == ABS_Y) mouse_y = (ev.value * SCREEN_H) / 32767;
                  if(mouse_btn && drag_win){
                      // Dragging window - mark old position dirty
                      window_mark_dirty(drag_win);
                      
                      // Update window position
                      drag_win->x = mouse_x - drag_off_x;
                      drag_win->y = mouse_y - drag_off_y;
                      
                      // Mark new position dirty
                      window_mark_dirty(drag_win);
                      
                      // Also update cursor
                      cursor_rect.x = mouse_x > SCREEN_W - 10 ? SCREEN_W - 10 : (mouse_x < 0 ? 0 : mouse_x);
                      cursor_rect.y = mouse_y > SCREEN_H - 10 ? SCREEN_H - 10 : (mouse_y < 0 ? 0 : mouse_y);
                      mark_dirty(cursor_rect.x, cursor_rect.y, 10, 10);
                      // Rate-limited composite of dirty regions
                      // Dragging window - mark regions as dirty for the global frame flush
                  } else {
                      // Just cursor movement - use efficient dirty region update
                      if(prev_mouse_x != mouse_x || prev_mouse_y != mouse_y){
                          cursor_move(mouse_x, mouse_y);
                          prev_mouse_x = mouse_x;
                          prev_mouse_y = mouse_y;
                      }
                  }
              } else if(ev.type == EV_KEY){
                  // Mouse Buttons
                  if(ev.code == BTN_LEFT){
                      mouse_btn = (ev.value == 1);
                      if(mouse_btn){
                          Window *hit = find_window_at(mouse_x, mouse_y);
                          if(hit){
                              // Raise window/Focus
                              focus_window(hit);
                              
                              // Start Drag (if on title bar)
                              if(hit->type == WIN_TYPE_CLIENT && mouse_y < hit->y + TITLE_BAR_HEIGHT) {
                                // Check if close button was clicked
                                int btn_x = hit->x + hit->w - CLOSE_BTN_SIZE - 4;
                                int btn_y = hit->y + 3;
                                if(mouse_x >= btn_x && mouse_x < btn_x + CLOSE_BTN_SIZE &&
                                   mouse_y >= btn_y && mouse_y < btn_y + CLOSE_BTN_SIZE) {
                                    // Send close event to client
                                    if(hit->shm) {
                                        struct sulu_ring *r = &hit->shm->event_ring;
                                        int next = (r->head + 1) % SULU_EVENT_RING_SIZE;
                                        if(next != r->tail) {
                                            hit->shm->event_buf[r->head].type = SULU_EV_CLOSE;
                                            r->head = next;
                                        }
                                    }
                                    close_window(hit);
                                } else {
                                    drag_win = hit;
                                    drag_off_x = mouse_x - hit->x;
                                    drag_off_y = mouse_y - hit->y;
                                }
                              }
                          }
                      } else {
                          drag_win = 0;
                      }
                  }
                  // Right click - forward to client
                  else if(ev.code == BTN_RIGHT && ev.value == 1){
                      if(focus_win && focus_win->type == WIN_TYPE_CLIENT && focus_win->shm){
                          struct sulu_event sev = {
                              .type = SULU_EV_MOUSE_BTN,
                              .code = BTN_RIGHT,
                              .value = 1
                          };
                          sulu_event_push(focus_win->shm, &sev);
                      }
                  }
                  // Keyboard State
                  if(ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT){
                      shift_state = (ev.value == 1);
                  } else if(ev.code == KEY_LEFTCTRL || ev.code == KEY_RIGHTCTRL){
                      ctrl_pressed = (ev.value == 1);
                  } else if(ev.code == KEY_CAPSLOCK){
                      if(ev.value == 1) capslock_state = !capslock_state;
                  }
                  // Hotkeys
                  // Hotkeys
                  if(ev.code == KEY_E && ev.value == 1 && ctrl_pressed){
                      // Ctrl+E -> Suspend
                      suspended = 1;
                      printf("sulu: suspended\n");
                  }
                  // Forward keyboard events to client windows
                  else if(focus_win && focus_win->type == WIN_TYPE_CLIENT && focus_win->shm){
                      struct sulu_event sev = {
                          .type = SULU_EV_KEY,
                          .code = ev.code,
                          .value = ev.value
                      };
                      sulu_event_push(focus_win->shm, &sev);
                  }
              }
          }
      }
      
      if(did_work){
          // Use dirty region compositing instead of full screen
          composite_dirty_and_flush();
          
          // Cap frame rate
          uint64 elapsed = rdtime() - loop_start;
          if(elapsed < SULU_FRAME_CYCLES) {
            usleep((SULU_FRAME_CYCLES - elapsed) / 10);
          }
      }
      
      // Yield CPU if idle to allow clients to run
      if(!did_work) {
          yield();
      }
  }
}
