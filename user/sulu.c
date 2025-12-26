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
#define MAX_BTN_SIZE 14
#define MAX_BTN_NORMAL (uint)0xFF00AA00
#define MAX_BTN_HOVER (uint)0xFF00FF00
#define MIN_BTN_SIZE 14
#define MIN_BTN_NORMAL (uint)0xFFCCAA00
#define MIN_BTN_HOVER (uint)0xFFFFFF00

#define ENABLE_SHADOWS 1
#define ENABLE_OCCLUSION_CULLING 1
#define SHADOW_OFFSET 6
#define CURSOR_SHADOW_OFFSET 3
#define SHADOW_COLOR (uint)0xFF04081F  // Deep darkened color
#define FULLSCREEN_OVER_SYS_BAR 1

// System Bar
#define BAR_HEIGHT 30
#define BAR_BG_COLOR (uint)0x55040920
#define BAR_BTN_WIDTH 120
#define BAR_BTN_HEIGHT 22
#define BAR_BTN_MARGIN 5
#define BAR_BTN_ACTIVE (uint)0xFF00FFCB
#define BAR_BTN_INACTIVE (uint)0xFF120A8F
#define BAR_BTN_TEXT_ACTIVE (uint)0xFF000000
#define BAR_BTN_TEXT_INACTIVE (uint)0xFFDDDDDD

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

// Global Clipboard
char global_clipboard[2048];
int global_clip_len = 0;

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

  // Maximization state
  int is_maximized;
  int is_minimized;
  int old_x, old_y, old_w, old_h;
} Window;

// Widget Infrastructure
typedef struct Widget {
  int x, y, w, h;
  int type;
} Widget;

// Rectangle helper
typedef struct {
    int x, y, w, h;
} Rect;

#define WIDGET_TYPE_SYSBAR 1

// Globals
uint *fb;
Window *windows = 0;
Widget sysbar; // The system bar widget
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
void draw_system_bar(Rect *clip);

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
    
    // 1. Occlusion Culling Optimization
    Window *top_opaque = 0;
#if ENABLE_OCCLUSION_CULLING
    // Find the topmost window that completely covers this region
    // (Starting from front of the list, assuming windows[0] is top)
    Window *oc_curr = windows;
    while(oc_curr) {
        if(!oc_curr->is_minimized && 
           oc_curr->x <= rx && oc_curr->y <= ry &&
           oc_curr->x + oc_curr->w >= rx2 && oc_curr->y + oc_curr->h >= ry2) {
            top_opaque = oc_curr;
            break; 
        }
        oc_curr = oc_curr->next;
    }
#endif

    // 2. Draw background for this region (skip if occluded)
    if(!top_opaque) {
        for (int y = ry; y < ry2; y++) {
            for (int x = rx; x < rx2; x++) {
                fb[y * SCREEN_W + x] = BACK_COLOR;
            }
        }
    }
    
    // 3. Draw windows in z-order (linked list is back-to-front? Wait, I need to check list order)
    // Actually, Sulu usually builds list with newest at end or at head.
    // Let's check window_raise.
    
    // RE-EVALUATING: If windows list is front-to-back, we need to reverse it for Painter's Algorithm.
    // Let's assume list is back-to-front for now based on previous behavior.
    
    // 2.5 Draw System Bar (Order depends on FULLSCREEN macro)
#if FULLSCREEN_OVER_SYS_BAR
    // Draw bar BEFORE windows so windows can cover it
    draw_system_bar(r);
#endif

    Window *w = windows;
    int skip = (top_opaque != 0); // If we found an occluder, skip until we hit it
    while (w) {
        if(skip) {
            if(w == top_opaque) skip = 0;
            else { w = w->next; continue; }
        }
        
        // Skip minimized windows
        if(w->is_minimized) {
            w = w->next;
            continue;
        }

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
            // If we are THE top_opaque window, don't draw our own shadow into the occluded region 
            // because we are opaque and cover it anyway. 
            // But shadows are offset, so we only skip if the shadow is also occluded.
            // For simplicity, let's just keep shadows for now unless perfectly maximized.
            for (int y = wy1; y < wy2; y++) {
                for (int x = wx1; x < wx2; x++) {
                    int buf_x = x - w->x;
                    int buf_y = y - w->y;
                    
                    if (buf_x >= SHADOW_OFFSET && buf_x < w->w + SHADOW_OFFSET &&
                        buf_y >= SHADOW_OFFSET && buf_y < w->h + SHADOW_OFFSET) {
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
                        
                        // Close button
                        int close_x = w->x + w->w - CLOSE_BTN_SIZE - 4;
                        int close_y = w->y + 3;
                        // Maximize button (to the left of close)
                        int max_x = close_x - MAX_BTN_SIZE - 4;
                        int max_y = close_y;
                        // Minimize button (to the left of maximize)
                        int min_x = max_x - MIN_BTN_SIZE - 4;
                        int min_y = close_y;

                        if (x >= close_x && x < close_x + CLOSE_BTN_SIZE &&
                            y >= close_y && y < close_y + CLOSE_BTN_SIZE) {
                            int is_hover = (mouse_x >= close_x && mouse_x < close_x + CLOSE_BTN_SIZE &&
                                            mouse_y >= close_y && mouse_y < close_y + CLOSE_BTN_SIZE);
                            fb[y * SCREEN_W + x] = is_hover ? CLOSE_BTN_HOVER : CLOSE_BTN_NORMAL;
                            continue;
                        } else if (x >= max_x && x < max_x + MAX_BTN_SIZE &&
                                   y >= max_y && y < max_y + MAX_BTN_SIZE) {
                            int is_hover = (mouse_x >= max_x && mouse_x < max_x + MAX_BTN_SIZE &&
                                            mouse_y >= max_y && mouse_y < max_y + MAX_BTN_SIZE);
                            fb[y * SCREEN_W + x] = is_hover ? MAX_BTN_HOVER : MAX_BTN_NORMAL;
                            continue;
                        } else if (x >= min_x && x < min_x + MIN_BTN_SIZE &&
                                   y >= min_y && y < min_y + MIN_BTN_SIZE) {
                            int is_hover = (mouse_x >= min_x && mouse_x < min_x + MIN_BTN_SIZE &&
                                            mouse_y >= min_y && mouse_y < min_y + MIN_BTN_SIZE);
                            fb[y * SCREEN_W + x] = is_hover ? MIN_BTN_HOVER : MIN_BTN_NORMAL;
                            continue;
                        }

                        // Check if this pixel is part of title text
                        int text_yt = (TITLE_BAR_HEIGHT - 8) / 2;
                        if(buf_y >= text_yt && buf_y < text_yt + 8 && w->shm && w->shm->title[0]) {
                            int font_row = buf_y - text_yt;
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
                            
                            int info_x = w->w - (n * 8) - 60;  // Shifted left to clear close, max, and min buttons
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
    
    // 2.5 Draw System Bar
#if !FULLSCREEN_OVER_SYS_BAR
    draw_system_bar(r);
#endif

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
        
        // Visual cursor position (Hotspot adjustment)
        int cx = cursor_rect.x;
        int cy = cursor_rect.y;
        
        if(bitmap == cursor_ibeam) {
            cx -= 4;
            cy -= 4;
        }

        int extra = ENABLE_SHADOWS ? CURSOR_SHADOW_OFFSET : 0;
        
        // use 20 for bounds check safety due to larger dirty rect
        if (!(cx + 10 + extra <= rx || rx2 <= cx ||
              cy + 10 + extra <= ry || ry2 <= cy)) {
            
            // Render cursor shadow first
#if ENABLE_SHADOWS
            int sx1 = (rx > cx + CURSOR_SHADOW_OFFSET) ? rx : cx + CURSOR_SHADOW_OFFSET;
            int sy1 = (ry > cy + CURSOR_SHADOW_OFFSET) ? ry : cy + CURSOR_SHADOW_OFFSET;
            int sx2 = (rx2 < cx + 10 + CURSOR_SHADOW_OFFSET) ? rx2 : cx + 10 + CURSOR_SHADOW_OFFSET;
            int sy2 = (ry2 < cy + 10 + CURSOR_SHADOW_OFFSET) ? ry2 : cy + 10 + CURSOR_SHADOW_OFFSET;
            for (int y = sy1; y < sy2; y++) {
                for (int x = sx1; x < sx2; x++) {
                    int bx = x - (cx + CURSOR_SHADOW_OFFSET);
                    int by = y - (cy + CURSOR_SHADOW_OFFSET);
                    if(bitmap[by * 10 + bx]) {
                        fb[y * SCREEN_W + x] = SHADOW_COLOR;
                    }
                }
            }
#endif

            // Render cursor itself
            int cx1 = (rx > cx) ? rx : cx;
            int cy1 = (ry > cy) ? ry : cy;
            int cx2 = (rx2 < cx + 10) ? rx2 : cx + 10;
            int cy2 = (ry2 < cy + 10) ? ry2 : cy + 10;
            
            for (int y = cy1; y < cy2; y++) {
                for (int x = cx1; x < cx2; x++) {
                    int bx = x - cx;
                    int by = y - cy;
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
    
    // Mark old position dirty (pad for hotspot offsets)
    int extra = ENABLE_SHADOWS ? CURSOR_SHADOW_OFFSET : 0;
    // Use larger dirty rect (-5 offset, 20 size) to cover I-beam offset
    mark_dirty(cursor_rect.x - 5, cursor_rect.y - 5, 20 + extra, 20 + extra);

    // If we were hovering a close button, mark it dirty so it transitions back
    Window *old_hit = find_window_at(cursor_rect.x, cursor_rect.y);
    if(old_hit && old_hit->type == WIN_TYPE_CLIENT) {
        int bx = cursor_rect.x - old_hit->x;
        int by = cursor_rect.y - old_hit->y;
        // Check if over any button area (Close, Max, Min). Total width roughly: Close(14+4) + Max(14+4) + Min(14+4)
        // Close starts at w-18. Max starts at w-36. Min starts at w-54. (Approx)
        // Let's just mark the wider area dirty if we were in the title bar right side logic.
        // Close_x = w - 18. Min_x = w - 18 - 18 - 18 = w - 54.
        if(by < TITLE_BAR_HEIGHT && bx >= old_hit->w - 60 && bx < old_hit->w - 4) {
             // Mark all buttons dirty
             mark_dirty(old_hit->x + old_hit->w - 60, old_hit->y + 3, 60, CLOSE_BTN_SIZE);
        }
    }
    
    // Update position
    cursor_rect.x = new_x;
    cursor_rect.y = new_y;
    
    // Use larger dirty rect (-5 offset, 20 size) to cover I-beam offset
    mark_dirty(new_x - 5, new_y - 5, 20 + extra, 20 + extra);

    // If we are now hovering a close button, mark it dirty so it transitions to bright red
    Window *new_hit = find_window_at(new_x, new_y);
    if(new_hit && new_hit->type == WIN_TYPE_CLIENT) {
        int bx = new_x - new_hit->x;
        int by = new_y - new_hit->y;
        if(by < TITLE_BAR_HEIGHT && bx >= new_hit->w - 60 && bx < new_hit->w - 4) {
             // Mark all buttons dirty
             mark_dirty(new_hit->x + new_hit->w - 60, new_hit->y + 3, 60, CLOSE_BTN_SIZE);
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

// Mark a partial region of a window as dirty (rx, ry, rw, rh are in window content space)
void window_mark_dirty_rect(Window *w, int rx, int ry, int rw, int rh) {
    if (!w) return;
    
    // Convert to screen space. Content starts after title bar.
    int sx = w->x + rx;
    int sy = w->y + TITLE_BAR_HEIGHT + ry;
    
    // Clamp to window content area
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > w->w) rw = w->w - rx;
    if (ry + rh > w->h - TITLE_BAR_HEIGHT) rh = (w->h - TITLE_BAR_HEIGHT) - ry;
    
    if (rw <= 0 || rh <= 0) return;
    
    mark_dirty(sx, sy, rw, rh);
}

// Close and free a window
void close_window(Window *w) {
    if(!w) return;

    // 1. Mark dirty (so area gets redrawn)
    window_mark_dirty(w);

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
//   printf("sulu: SPAWN pid=%d key=%d w=%d h=%d\n", client_pid, shm_key, width, height);
  
  // Map the client's shared memory
  int shmid = shmget(shm_key, 0);  // Use existing
  if(shmid < 0) {
  printf("sulu: failed to get client shm key=%d\n", shm_key);
    return 0;
  }
  
  struct sulu_window_shm *shm = (struct sulu_window_shm*)shmat(shmid, 0);
//   printf("sulu: shmat returned %p for shmid=%d\n", shm, shmid);
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
  
//   printf("sulu: window id=%d shm=%p buf=%p heap=%p\n", 
//          win->id, shm, win->buf, sbrk(0));
  
  // Link into window list (metadata already set by client in many cases)
  shm->win_id = win->id;
  // If client didn't set width/height, do it now
  if(shm->width == 0) shm->width = width;
  if(shm->height == 0) shm->height = height;
  shm->x = win->x;
  shm->y = win->y;
  
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
  
  // Handle focus transfer
  Window *old_focus = focus_win;
  focus_win = win;
  if(old_focus) window_mark_dirty(old_focus); // Redraw old window to show it's unfocused
  return win;
}

#define MAX_WINDOWS 64

// Helper to sort windows by ID for stable taskbar order
void sort_windows_by_id(Window **arr, int count) {
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (arr[j]->id > arr[j+1]->id) {
                Window *temp = arr[j];
                arr[j] = arr[j+1];
                arr[j+1] = temp;
            }
        }
    }
}

// Helper to get windows in generic order (for taskbar)
int get_all_windows(Window **arr, int max_count) {
    int count = 0;
    Window *curr = windows;
    while(curr && count < max_count) {
        if(curr->type == WIN_TYPE_CLIENT && curr->shm) {
            arr[count++] = curr;
        }
        curr = curr->next;
    }
    return count;
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
// Helper to draw char directly to screen
void sulu_draw_char_screen(uint *fb, int stride, int x, int y, char ch, uint color) {
    if(ch < 32 || ch > 127) ch = '?';
    uchar *bitmap = font_8x8[ch - 32];
    for(int r = 0; r < 8; r++) {
        for(int c = 0; c < 8; c++) {
            if(bitmap[r] & (1 << (7-c))) { // Render MSB as leftmost pixel
               int px = x + c;
               int py = y + r;
               if(px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H) {
                  fb[py * stride + px] = color;
               }
            }
        }
    }
}

// Clock state
char clock_buf[16] = "00:00:00";
int  last_time_sec = -1;

void update_clock() {
    int t = time();
    if(t == last_time_sec) return;
    last_time_sec = t;
    
    int s = t % 60;
    int m = (t / 60) % 60;
    int h = (t / 3600) % 24;
    
    // Simple snprintf replacement since we might not have it
    // Using manual formatting
    clock_buf[0] = '0' + (h / 10);
    clock_buf[1] = '0' + (h % 10);
    clock_buf[2] = ':';
    clock_buf[3] = '0' + (m / 10);
    clock_buf[4] = '0' + (m % 10);
    clock_buf[5] = ':';
    clock_buf[6] = '0' + (s / 10);
    clock_buf[7] = '0' + (s % 10);
    clock_buf[8] = 0;
    
    // Mark clock region dirty
    if(sysbar.type == WIDGET_TYPE_SYSBAR) {
       // Only partial redraw of clock area
       mark_dirty(SCREEN_W - 80, sysbar.y, 80, sysbar.h);
    }
}

// Helper to draw text directly to screen
void draw_screen_text(int x, int y, char *s, uint color) {
  while(*s) {
    sulu_draw_char_screen(fb, SCREEN_W, x, y, *s, color);
    x += 8;
    s++;
  }
}

// Draw the system bar (clipped)
void draw_system_bar(Rect *clip) {
  if (sysbar.type != WIDGET_TYPE_SYSBAR) return;
  
  int bar_y = sysbar.y;
  int bar_h = sysbar.h;
  
  // Default to full bar if no clip
  int cx = 0, cy = bar_y, cw = SCREEN_W, ch = bar_h;
  
  if(clip) {
    cx = clip->x;
    cy = clip->y;
    cw = clip->w;
    ch = clip->h;
  }
  
  // Intersect clip with bar
  int x = (cx > 0) ? cx : 0;
  int y = (cy > bar_y) ? cy : bar_y;
  int x2 = (cx + cw < SCREEN_W) ? cx + cw : SCREEN_W;
  int y2 = (cy + ch < bar_y + bar_h) ? cy + ch : bar_y + bar_h;
  
  if (x >= x2 || y >= y2) return; // No intersection
  
  // Background
  for(int j = y; j < y2; j++) {
    for(int i = x; i < x2; i++) {
      fb[j * SCREEN_W + i] = BAR_BG_COLOR;
    }
  }
  
  // Window buttons
  int btn_x = 10;
  
  Window *sorted_wins[MAX_WINDOWS];
  int win_count = get_all_windows(sorted_wins, MAX_WINDOWS);
  sort_windows_by_id(sorted_wins, win_count);

  for(int k = 0; k < win_count; k++) {
    Window *curr = sorted_wins[k];
    
      uint bg = (curr == focus_win) ? BAR_BTN_ACTIVE : BAR_BTN_INACTIVE;
      uint text = (curr == focus_win) ? BAR_BTN_TEXT_ACTIVE : BAR_BTN_TEXT_INACTIVE;
      // Button rect
      // Check if button intersects our draw area
      int bx = btn_x;
      int bw = BAR_BTN_WIDTH;
      int by = bar_y + 5;
      int bh = bar_h - 10;
      
      int draw_bx = (x > bx) ? x : bx;
      int draw_by = (y > by) ? y : by;
      int draw_bx2 = (x2 < bx + bw) ? x2 : bx + bw;
      int draw_by2 = (y2 < by + bh) ? y2 : by + bh;
      
      if(draw_bx < draw_bx2 && draw_by < draw_by2) {
          for(int j = draw_by; j < draw_by2; j++) {
            for(int i = draw_bx; i < draw_bx2; i++) {
              fb[j * SCREEN_W + i] = bg;
            }
          }
          
          // Title text (simple clipping: only draw if button fully visible or just draw anyway, 
          // sulu_draw_char_screen handles screen bounds but not clip bounds.
          // For simplicity, re-draw text if button is visible.
          if(curr->shm->title[0]) {
            char buf[16];
            memmove(buf, curr->shm->title, 14);
            buf[14] = 0;
            int len = strlen(buf);
            if(len > 12) { buf[12] = '.'; buf[13] = '.'; }
            // Only draw text if it might be within clip (heuristic)
            if(btn_x + 10 < x2 && btn_x + 10 + len*8 > x)
                draw_screen_text(btn_x + 10, bar_y + 11, buf, text);
          }
      }
      
      btn_x += BAR_BTN_WIDTH + BAR_BTN_MARGIN;
  }
  
  // Clock
  draw_screen_text(SCREEN_W - 80, bar_y + 11, clock_buf, BAR_BTN_TEXT_INACTIVE);
}

// Composite all windows AND cursor (using z-index: bg -> windows -> cursor)
void
composite()
{
    Rect r = {0, 0, SCREEN_W, SCREEN_H};
    composite_region(&r);
}

Window* find_window_at(int x, int y){
  Window *w = windows;
  Window *hit = 0;
  while(w){
      if(!w->is_minimized && x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h) hit = w;
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
    
    // Un-minimize if needed
    if(w->is_minimized) {
        w->is_minimized = 0;
        // Mark window dirty to redraw it
        window_mark_dirty(w);
    }
    
    // Raise to top of Z-order
    window_raise(w);
    
    // Mark windows dirty for repainting (border color change)
    if(old_focus && old_focus != w) window_mark_dirty(old_focus);
    window_mark_dirty(w);
    
    // Mark system bar dirty to update button highlights
    if(sysbar.type == WIDGET_TYPE_SYSBAR) {
        mark_dirty(sysbar.x, sysbar.y, sysbar.w, sysbar.h);
    }
}

int
main(int argc, char *argv[])
{

    int shmid = shmget(SHM_FB, 0);
  if(shmid < 0) exit(1);
  fb = (uint*) shmat(shmid, 0);
  if((uint64)fb == -1) exit(1);
  
  printf("sulu: main start\n");

  // Register as Server by opening /dev/sulu
  // Note: We use O_RDWR.
  int sulu_fd = open("/dev/sulu", O_RDWR);
  if(sulu_fd < 0) {
      printf("sulu: failed to open /dev/sulu\n");
      exit(1);
  }
  printf("sulu: /dev/sulu opened\n");
  
  // Suspension state
  int suspended = 0;
  
  // Cursor state for optimization
  int prev_mouse_x = -1, prev_mouse_y = -1;
  
  int input_fd = open("/dev/input", O_RDONLY);
  if(input_fd < 0) exit(1);
  printf("sulu: /dev/input opened\n");
  
  // Initialize System Bar Widget
  sysbar.x = 0;
  sysbar.y = SCREEN_H - BAR_HEIGHT;
  sysbar.w = SCREEN_W;
  sysbar.h = BAR_HEIGHT;
  sysbar.type = WIDGET_TYPE_SYSBAR;

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
  printf("sulu: terminal forked\n");
  
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

  printf("sulu: entering main loop\n");

  while(1){
      uint64 loop_start = rdtime();
      int did_work = 0;
      
      // 1. Process Window System Events (Connect/Disconnect)
      struct sulu_msg msg;
      while(readavail(sulu_fd) > 0) {
          int n = read(sulu_fd, &msg, sizeof(msg));
          if(n != sizeof(msg)) break;

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
                // Mark system bar dirty to show new button
                mark_dirty(sysbar.x, sysbar.y, sysbar.w, sysbar.h);
              }
          } else if(msg.type == 2) { // SULU_EVENT_DISCONNECT
              // Find and close all windows for this PID
              Window *w = windows;
              while(w) {
                  Window *next_w = w->next;
                  if(w->type == WIN_TYPE_CLIENT && w->client_pid == msg.pid) {
                      close_window(w);
                      // Mark system bar dirty to remove button
                      mark_dirty(sysbar.x, sysbar.y, sysbar.w, sysbar.h);
                  }
                  w = next_w;
              }
              // Force full redraw to be safe
              // composite(); 
              gpu_flush();
          }
 else if(msg.type == 4) { // SULU_EVENT_RESIZE
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
                          if (w->is_maximized) { w->x = 0; w->y = 0; }
                          if (w->shm) { w->shm->x = w->x; w->shm->y = w->y; }
                          
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
                if (cmd->blit.w > 0 && cmd->blit.h > 0) {
                    // Partial update
                    window_mark_dirty_rect(w, cmd->blit.x, cmd->blit.y, cmd->blit.w, cmd->blit.h);
                } else {
                    // Full update
                    window_mark_dirty(w);
                }
              } else if(cmd->type == SULU_CMD_CLOSE) {
                close_window(w);
                // Mark system bar dirty
                if(sysbar.type == WIDGET_TYPE_SYSBAR)
                    mark_dirty(sysbar.x, sysbar.y, sysbar.w, sysbar.h);
                closed = 1;
                break; // Window is gone
              } else if(cmd->type == SULU_CMD_SWAP) {
                if(w->shm->flags & SULU_FLAG_DOUBLE_BUFFER) {
                  // Buffer toggle is now handled CLIENT SIDE to fix flickering.
                  // We just update our local pointer and mark full dirty.
                  w->buf = sulu_front_pixels(w->shm);
                  window_mark_dirty(w);
                }
              } else if(cmd->type == SULU_CMD_CLIP_SET) {
                // Copy from client to global clipboard
                int len = w->shm->clipboard_len;
                if(len > 2048) len = 2048;
                if(len < 0) len = 0;
                memmove(global_clipboard, w->shm->clipboard, len);
                global_clip_len = len;
              } else if(cmd->type == SULU_CMD_CLIP_GET) {
                // Copy from global clipboard to client
                int len = global_clip_len;
                memmove(w->shm->clipboard, global_clipboard, len);
                w->shm->clipboard_len = len;
                
                // Push PASTE event
                struct sulu_ring *er = &w->shm->event_ring;
                int next = (er->head + 1) % SULU_EVENT_RING_SIZE;
                if(next != er->tail) {
                    w->shm->event_buf[er->head].type = SULU_EV_PASTE;
                    er->head = next;
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
                      if(drag_win->shm) {
                          drag_win->shm->x = drag_win->x;
                          drag_win->shm->y = drag_win->y;
                      }
                      
                      // Mark new position dirty
                      window_mark_dirty(drag_win);
                      
                      // Also update cursor
                       cursor_rect.x = mouse_x > SCREEN_W - 10 ? SCREEN_W - 10 : (mouse_x < 0 ? 0 : mouse_x);
                       cursor_rect.y = mouse_y > SCREEN_H - 10 ? SCREEN_H - 10 : (mouse_y < 0 ? 0 : mouse_y);
                       int extra = ENABLE_SHADOWS ? CURSOR_SHADOW_OFFSET : 0;
                       mark_dirty(cursor_rect.x, cursor_rect.y, 10 + extra, 10 + extra);
                      // Rate-limited composite of dirty regions
                      // Dragging window - mark regions as dirty for the global frame flush
                  } else {
                      // Just cursor movement - use efficient dirty region update
                      if(prev_mouse_x != mouse_x || prev_mouse_y != mouse_y){
                          cursor_move(mouse_x, mouse_y);
                          prev_mouse_x = mouse_x;
                          prev_mouse_y = mouse_y;
                          if(focus_win && focus_win->type == WIN_TYPE_CLIENT && focus_win->shm){
                              struct sulu_event sev = {
                                  .type = SULU_EV_MOUSE_MOVE,
                                  .x = mouse_x,
                                  .y = mouse_y
                              };
                              sulu_event_push(focus_win->shm, &sev);
                          }
                      }
                  }
              } else if(ev.type == EV_KEY){
                  // Mouse Buttons
                  if(ev.code == BTN_LEFT){
                      mouse_btn = (ev.value == 1);
                      if(mouse_btn){
                          // 1. Check Widgets (System Bar)
                          if(mouse_x >= sysbar.x && mouse_x < sysbar.x + sysbar.w &&
                             mouse_y >= sysbar.y && mouse_y < sysbar.y + sysbar.h) {
                               if(sysbar.type == WIDGET_TYPE_SYSBAR) {
                                  // Bar click logic (find button)
                                  int btn_x = 10;
                                  Window *sorted_wins[MAX_WINDOWS];
                                  int win_count = get_all_windows(sorted_wins, MAX_WINDOWS);
                                  sort_windows_by_id(sorted_wins, win_count);

                                  for(int k = 0; k < win_count; k++) {
                                      Window *curr = sorted_wins[k];
                                      
                                          if(mouse_x >= btn_x && mouse_x < btn_x + BAR_BTN_WIDTH) {
                                              focus_window(curr);
                                              break;
                                          }
                                          btn_x += BAR_BTN_WIDTH + BAR_BTN_MARGIN;
                                  }
                               }
                          } 
                          // 2. Check Windows
                          else {
                              Window *hit = find_window_at(mouse_x, mouse_y);
                              if(hit){
                                  // Raise/Focus
                                  focus_window(hit);
                                  
                                  // Title bar / Close / Maximize logic
                                  if(hit->type == WIN_TYPE_CLIENT && mouse_y < hit->y + TITLE_BAR_HEIGHT) {
                                       // Check close button
                                       int close_x = hit->x + hit->w - CLOSE_BTN_SIZE - 4;
                                       int close_y = hit->y + 3;
                                       int max_x = close_x - MAX_BTN_SIZE - 4;
                                       int max_y = close_y;

                                       if(mouse_x >= close_x && mouse_x < close_x + CLOSE_BTN_SIZE &&
                                          mouse_y >= close_y && mouse_y < close_y + CLOSE_BTN_SIZE) {
                                           if(hit->shm) {
                                               struct sulu_ring *r = &hit->shm->event_ring;
                                               int next = (r->head + 1) % SULU_EVENT_RING_SIZE;
                                               if(next != r->tail) {
                                                   hit->shm->event_buf[r->head].type = SULU_EV_CLOSE;
                                                   r->head = next;
                                               }
                                           }
                                           close_window(hit);
                                           // Mark system bar dirty
                                           if(sysbar.type == WIDGET_TYPE_SYSBAR)
                                               mark_dirty(sysbar.x, sysbar.y, sysbar.w, sysbar.h);
                                       } else if(mouse_x >= max_x && mouse_x < max_x + MAX_BTN_SIZE &&
                                                 mouse_y >= max_y && mouse_y < max_y + MAX_BTN_SIZE) {
                                           // Toggle maximization
                                           hit->is_maximized = !hit->is_maximized;
                                           if(hit->is_maximized) {
                                               hit->old_x = hit->x; hit->old_y = hit->y;
                                               hit->old_w = hit->w; hit->old_h = hit->h - TITLE_BAR_HEIGHT;
                                               hit->x = 0; hit->y = 0;
                                           } else {
                                               window_mark_dirty(hit);
                                               hit->x = hit->old_x; hit->y = hit->old_y;
                                           }
                                           if(hit->shm) {
                                               hit->shm->x = hit->x;
                                               hit->shm->y = hit->y;
                                               struct sulu_ring *r = &hit->shm->event_ring;
                                               int next = (r->head + 1) % SULU_EVENT_RING_SIZE;
                                               if(next != r->tail) {
                                                   hit->shm->event_buf[r->head].type = SULU_EV_MAXIMIZE;
                                                   hit->shm->event_buf[r->head].value = hit->is_maximized;
                                                   if(hit->is_maximized) {
                                                       hit->shm->event_buf[r->head].x = SCREEN_W;
#if FULLSCREEN_OVER_SYS_BAR
                                                       hit->shm->event_buf[r->head].y = SCREEN_H - TITLE_BAR_HEIGHT;
#else
                                                       hit->shm->event_buf[r->head].y = SCREEN_H - TITLE_BAR_HEIGHT - BAR_HEIGHT;
#endif
                                                   } else {
                                                       hit->shm->event_buf[r->head].x = hit->old_w;
                                                       hit->shm->event_buf[r->head].y = hit->old_h;
                                                   }
                                                   r->head = next;
                                               }
                                           }
                                       } else {
                                            // Check Minimize
                                            int min_x = max_x - MIN_BTN_SIZE - 4;
                                            int min_y = close_y;
                                            if(mouse_x >= min_x && mouse_x < min_x + MIN_BTN_SIZE &&
                                               mouse_y >= min_y && mouse_y < min_y + MIN_BTN_SIZE) {
                                                hit->is_minimized = 1;
                                                window_mark_dirty(hit); // Mark old area dirty to clear it
                                                focus_win = 0; // Drop focus
                                                if(sysbar.type == WIDGET_TYPE_SYSBAR)
                                                    mark_dirty(sysbar.x, sysbar.y, sysbar.w, sysbar.h); // Update taskbar highlight
                                            } else if (!hit->is_maximized) {
                                               drag_win = hit;
                                               drag_off_x = mouse_x - hit->x;
                                               drag_off_y = mouse_y - hit->y;
                                            }
                                       }
                                  } else if(hit->type == WIN_TYPE_CLIENT && hit->shm) {
                                      // Content area click
                                      struct sulu_event sev = { .type = SULU_EV_MOUSE_BTN, .code = BTN_LEFT, .value = 1, .x = mouse_x, .y = mouse_y };
                                      sulu_event_push(hit->shm, &sev);
                                  }
                              }
                          }
                      } else {
                          // Mouse Release
                          if(!drag_win && focus_win && focus_win->type == WIN_TYPE_CLIENT && focus_win->shm) {
                              struct sulu_event sev = { .type = SULU_EV_MOUSE_BTN, .code = BTN_LEFT, .value = 0, .x = mouse_x, .y = mouse_y };
                              sulu_event_push(focus_win->shm, &sev);
                          }
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
                   } else {
                       // Update Sulu internal state for modifiers
                       if(ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT){
                           shift_state = (ev.value == 1);
                       } else if(ev.code == KEY_LEFTCTRL || ev.code == KEY_RIGHTCTRL){
                           ctrl_pressed = (ev.value == 1);
                       } else if(ev.code == KEY_CAPSLOCK && ev.value == 1){
                           capslock_state = !capslock_state;
                       }

                       // Check for Sulu hotkeys
                       if(ev.code == KEY_E && ev.value == 1 && ctrl_pressed){
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
               } else if(ev.type == EV_REL){
                   if(ev.code == REL_WHEEL){
                       // Forward scroll to focused window
                       if(focus_win && focus_win->type == WIN_TYPE_CLIENT && focus_win->shm){
                           struct sulu_event sev = {
                               .type = SULU_EV_MOUSE_WHEEL,
                               .value = (int)ev.value, // Delta
                               .x = mouse_x,
                               .y = mouse_y
                           };
                           sulu_event_push(focus_win->shm, &sev);
                       }
                   }
               }
           }
      }
      

      // Update Clock every frame (jitter-free)
      update_clock();

      if(did_work || dirty_valid){
          // Use dirty region compositing instead of full screen
          composite_dirty_and_flush();
          
          // Cap frame rate
          uint64 elapsed = rdtime() - loop_start;
          if(elapsed < SULU_FRAME_CYCLES){
            usleep((SULU_FRAME_CYCLES - elapsed) / 10);
          }
      } else {
      }
      // Yield CPU if idle to allow clients to run
      if(!did_work) {
          yield();
      }
  }
}
