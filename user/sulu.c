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
#define UNFOCUS_COLOR (uint)0xFF120A8F
#define FOCUS_COLOR (uint)0xFF00FFCB
#define TERM_BACK (uint)0x55040720
#define TITLE_BAR_HEIGHT 20

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

// Rate limiting for high-frequency updates (drag, animations)
static int frame_counter = 0;
#define FRAME_RATE_LIMIT 1  // flush every Nth event

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
        if (!(w->x + w->w <= rx || rx2 <= w->x ||
              w->y + w->h <= ry || ry2 <= w->y)) {
            // Draw overlapping portion
            int wx1 = (rx > w->x) ? rx : w->x;
            int wy1 = (ry > w->y) ? ry : w->y;
            int wx2 = (rx2 < w->x + w->w) ? rx2 : w->x + w->w;
            int wy2 = (ry2 < w->y + w->h) ? ry2 : w->y + w->h;
        
            // Client window: TITLE_BAR_HEIGHT + client buffer
            for (int y = wy1; y < wy2; y++) {
                for (int x = wx1; x < wx2; x++) {
                    int buf_x = x - w->x;
                    int buf_y = y - w->y;
                    
                    if(buf_y < TITLE_BAR_HEIGHT) {
                        // Title bar
                        uint color = (w == focus_win) ? FOCUS_COLOR : UNFOCUS_COLOR;
                        fb[y * SCREEN_W + x] = color;
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
    if (!(cursor_rect.x + cursor_rect.w <= rx || rx2 <= cursor_rect.x ||
          cursor_rect.y + cursor_rect.h <= ry || ry2 <= cursor_rect.y)) {
        int cx1 = (rx > cursor_rect.x) ? rx : cursor_rect.x;
        int cy1 = (ry > cursor_rect.y) ? ry : cursor_rect.y;
        int cx2 = (rx2 < cursor_rect.x + 10) ? rx2 : cursor_rect.x + 10;
        int cy2 = (ry2 < cursor_rect.y + 10) ? ry2 : cursor_rect.y + 10;
        
        for (int y = cy1; y < cy2; y++) {
            for (int x = cx1; x < cx2; x++) {
                fb[y * SCREEN_W + x] = CURSOR_COLOR;
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
    
    // Update position
    cursor_rect.x = new_x;
    cursor_rect.y = new_y;
    
    // Mark new position dirty
    mark_dirty(new_x, new_y, 10, 10);
    
    // Composite and flush
    composite_dirty_and_flush();
}

// Mark a window's bounds as dirty
void window_mark_dirty(Window *w) {
    if (!w) return;
    mark_dirty(w->x, w->y, w->w, w->h);
}

// Spawn a client window (for external programs using Sulu API)
Window*
spawn_client_window(int client_pid, int shm_key, int width, int height)
{
  // Map the client's shared memory
  int shmid = shmget(shm_key, 0);  // Use existing
  if(shmid < 0) {
  printf("sulu: failed to get client shm key=%d\n", shm_key);
    return 0;
  }
  
  struct sulu_window_shm *shm = (struct sulu_window_shm*)shmat(shmid, 0);
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
  win->buf = sulu_pixels(shm);  // Use client's pixel buffer
  win->next = 0;
  win->type = WIN_TYPE_CLIENT;
  win->client_pid = client_pid;
  win->shm = shm;
  
  // Initialize the SHM header
  shm->win_id = win->id;
  shm->width = width;
  shm->height = height;
  shm->flags = 0;
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

int
main(int argc, char *argv[])
{

  int shmid = shmget(SHM_FB, 0);
  if(shmid < 0) exit(1);
  fb = (uint*) shmat(shmid, 0);
  if((uint64)fb == -1) exit(1);
  
  // Register with suluctl
  int suluctl_fd = open("/dev/suluctl", O_WRONLY);
  if(suluctl_fd >= 0) {
    write(suluctl_fd, "register", 8);
    close(suluctl_fd);
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
  
  struct input_event ev;
  while(1){
      
      // 0. Check for suspend/resume commands
      int cmd = suluctl_poll();
      if(cmd == 1) {  // Suspend
        suspended = 1;
        // Clear screen to black
        for(int i=0; i<SCREEN_W*SCREEN_H; i++) fb[i] = 0xFF000000;
        gpu_flush();
      } else if(cmd == 2) {  // Resume
        suspended = 0;
        // Full redraw (composite includes cursor)
        composite();
        gpu_flush();
      } else if(cmd == 3) {  // Quit
        exit(0);
      }
      
      // 0.5. Check for new client window requests
      {
        int cpid, shm_key, cwidth, cheight;
        if(suluctl_get_request(&cpid, &shm_key, &cwidth, &cheight)) {
          Window *new_win = spawn_client_window(cpid, shm_key, cwidth, cheight);
          if(new_win) {
            window_mark_dirty(new_win);
            composite_dirty_and_flush();
          }
        }
      }
      
      // 0.6. Process client cmd_rings (BLIT commands)
      {
        Window *w = windows;
        while(w) {
          if(w->type == WIN_TYPE_CLIENT && w->shm) {
            struct sulu_ring *r = &w->shm->cmd_ring;
            while(r->head != r->tail) {
              struct sulu_cmd *cmd = &w->shm->cmd_buf[r->tail];
              if(cmd->type == SULU_CMD_BLIT) {
                // Mark window region dirty and composite
                window_mark_dirty(w);
                composite_dirty_and_flush();
              } else if(cmd->type == SULU_CMD_CLOSE) {
                // TODO: Close window
              }
              r->tail = (r->tail + 1) % SULU_CMD_RING_SIZE;
            }
          }
          w = w->next;
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
        sleep(10);  // Don't burn CPU
        continue;
      }
      
      // 1. Process Input
      int n = 0; 
      
      int did_update = 0;
      if(readavail(input_fd) > 0){
          n = read(input_fd, &ev, sizeof(ev));
          if(n == sizeof(ev)){
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
                      if(++frame_counter >= FRAME_RATE_LIMIT) {
                          frame_counter = 0;
                          composite_dirty_and_flush();
                      }
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
                              Window *old_focus = focus_win;
                              focus_win = hit;
                              window_raise(focus_win); // Always bring to front on click
                              
                              // Mark both old and new focus windows dirty
                              if(old_focus && old_focus != hit){
                                  window_mark_dirty(old_focus);
                              }
                              window_mark_dirty(hit);
                              composite_dirty_and_flush();
                              
                              if(mouse_y < hit->y + 20){
                                  drag_win = hit;
                                  drag_off_x = mouse_x - hit->x;
                                  drag_off_y = mouse_y - hit->y;
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
                  if(ev.code == KEY_E && ev.value == 1 && ctrl_pressed){
                      // Ctrl+E -> Suspend
                      int fd = open("/dev/suluctl", O_WRONLY);
                      if(fd >= 0){
                          write(fd, "suspend", 7);
                          close(fd);
                      }
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
      
      if(did_update){
          // Use dirty region compositing instead of full screen
          composite_dirty_and_flush();
      }
      
      // Avoid busy loop if idle?
      // Not ideal, but sleep(1) is too slow.
  }
}
