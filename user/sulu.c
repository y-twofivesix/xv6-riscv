#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "user/font.h"

// Screen
#define SCREEN_W 1280
#define SCREEN_H 800

#define BACK_COLOR (uint)0xFF08113B
#define CURSOR_COLOR (uint)0xFF00FFFFFF
#define UNFOCUS_COLOR (uint)0xFF120A8F
#define FOCUS_COLOR (uint)0xFF00FFCB
#define TERM_BACK (uint)0x55040720

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

// Maps (Linux Input Event codes)
char keymap[128] = {
  0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', // 0-14
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', // 15-28
  0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', // 29-43
  'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ' // 44-57
};

char keymap_shift[128] = {
  0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', // 0-14
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', // 15-28
  0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', // 29-43
  'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ' // 44-57
};

int shift_state = 0;
int ctrl_pressed = 0;
int capslock_state = 0;

// Terminal
#define COLS 60
#define ROWS 20
#define CHAR_W 8
#define CHAR_H 8
#define PADDING 8
#define LINE_SPACING 8  // Extra pixels between lines (like CSS line-height)

typedef struct Terminal {
    int pid;
    int fd_in;  // Write here to send to shell
    int fd_out; // Read here from shell
    
    char display[ROWS][COLS];
    int cursor_row;
    int cursor_col;
    int esc_state; // 0=Norm, 1=Esc, 2=Bracket
    int input_start_row;  // Where current input line started
    int input_start_col;  // Column where input started
    char ansi_buf[16];    // Buffer for ANSI parameters
    int ansi_idx;         // Index into ansi_buf
} Terminal;

typedef struct Window {
  int id;
  int x, y;
  int w, h;
  uint *buf; 
  struct Window *next;
  
  // Terminal State
  Terminal term;
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

void update_title_colors();

// Rate limiting for drag operations to reduce jitter
static int composite_skip_counter = 0;
#define COMPOSITE_RATE_LIMIT 2  // composite every Nth event (2 = 50% rate)

// Helper: find last non-empty character position in a row
int
find_line_end(Terminal *t, int row)
{
    for(int c = COLS - 1; c >= 0; c--){
        if(t->display[row][c] != 0 && t->display[row][c] != ' '){
            return c + 1; // Return position after last char (where cursor can be)
        }
    }
    // Empty line - cursor can be at start
    if(row == t->input_start_row){
        return t->input_start_col;
    }
    return 0;
}
// Text Drawing
// Helper to draw char at specific pixel location
void
draw_char_at(Window *w, int px, int py, char ch, uint color)
{
    if(ch < ' ' || ch > '~') return;
    int index = ch - ' ';
    for(int y=0; y<8; y++){
        for(int x=0; x<8; x++){
            if((font_8x8[index][y] >> (7-x)) & 1){
                 w->buf[(py+y)*w->w + (px+x)] = color;
            } else {
                 w->buf[(py+y)*w->w + (px+x)] = TERM_BACK;
            }
        }
    }
}

void
redraw_line(Window *w, int r)
{
    Terminal *t = &w->term;
    int base_py = 20 + PADDING + r * (CHAR_H + LINE_SPACING);
    
    // Clear line first
    for(int y=0; y<CHAR_H; y++){
        for(int x=0; x<w->w; x++){
            w->buf[(base_py+y)*w->w + x] = TERM_BACK;
        }
    }

    int px = PADDING;
    for(int c=0; c<COLS; c++){
        int py = base_py;
        char ch = t->display[r][c];
        
        // Determine colors based on focus
        uint text_color = (w == focus_win) ? 0xFFFFFFFF : 0xFF888888; // White or grey
        uint cursor_color = (w == focus_win) ? FOCUS_COLOR : 0xFF888888; // Cyan or grey
        
        if(r == t->cursor_row && c == t->cursor_col){
             // Draw Brace Cursor { ch }
             draw_char_at(w, px, py, '{', cursor_color);
             px += CHAR_W;
             if(ch != 0 && ch != ' ') {
                 draw_char_at(w, px, py, ch, text_color);
                 px += CHAR_W;
             } else {
                 // Cursor on empty space
             }
             draw_char_at(w, px, py, '}', cursor_color);
             px += CHAR_W;
        } else {
             if(ch != 0) draw_char_at(w, px, py, ch, text_color);
             px += CHAR_W;
        }
    }
}

// Legacy wrapper if needed, but we will update term_putc to use redraw_line
void
draw_char(Window *w, int r, int c, char ch, uint color)
{
    // Redirect to redraw_line to ensure consistency
    // But update buffer first
    w->term.display[r][c] = ch;
    redraw_line(w, r);
}

void
term_putc(Window *w, char c)
{
    Terminal *t = &w->term;
    
    // State Machine
    if(t->esc_state == 0){
        // Normal
        if(c == 27){
            t->esc_state = 1;
        } else if(c == '\n'){
            int old_r = t->cursor_row;
            t->cursor_col = 0;
            t->cursor_row++;
            redraw_line(w, old_r);   // Clear cursor from old line
            redraw_line(w, t->cursor_row); // Draw cursor on new line
        } else if(c == '\r'){
            t->cursor_col = 0;
            redraw_line(w, t->cursor_row);
        } else if(c == '\b'){
            // Only allow backspace if we're after the input boundary
            int can_backspace = 0;
            if(t->cursor_row == t->input_start_row && t->cursor_col > t->input_start_col){
                can_backspace = 1;
            } else if(t->cursor_row > t->input_start_row && t->cursor_col > 0){
                can_backspace = 1;
            }
            
            if(can_backspace){
                // Shift all characters after cursor one position left
                for(int c = t->cursor_col; c < COLS - 1; c++){
                    t->display[t->cursor_row][c - 1] = t->display[t->cursor_row][c];
                }
                // Clear the last character in the row
                t->display[t->cursor_row][COLS - 1] = ' ';
                
                // Move cursor back
                t->cursor_col--;
                
                // Redraw the entire line to show the shift
                redraw_line(w, t->cursor_row);
            }
        } else {
            t->display[t->cursor_row][t->cursor_col] = c;
            t->cursor_col++;
            redraw_line(w, t->cursor_row);
        }
    } else if(t->esc_state == 1){
        // Saw ESC
        if(c == '['){
            t->esc_state = 2; // CSI
            t->ansi_idx = 0;
            t->ansi_buf[0] = 0;
        } else {
            t->esc_state = 0; // Fallback
        }
    } else if(t->esc_state == 2){
        // CSI Parameter bytes (0-9, ;)
        if(c >= '0' && c <= '9'){
            if(t->ansi_idx < 15){
                t->ansi_buf[t->ansi_idx++] = c;
                t->ansi_buf[t->ansi_idx] = 0;
            }
        } else if(c == ';'){
            if(t->ansi_idx < 15){
                t->ansi_buf[t->ansi_idx++] = c;
                t->ansi_buf[t->ansi_idx] = 0;
            }
        } else if(c >= 0x40 && c <= 0x7E){
            // Final byte - execute command
            if(c == 'J'){
                // Clear screen (usually ESC[2J)
                if(t->ansi_buf[0] == '2' || t->ansi_buf[0] == 0){
                    // Clear entire display
                    for(int r = 0; r < ROWS; r++){
                        for(int col = 0; col < COLS; col++){
                            t->display[r][col] = ' ';
                        }
                        redraw_line(w, r);
                    }
                }
            } else if(c == 'H'){
                // Cursor position (ESC[row;colH)
                // Parse row;col from ansi_buf
                int row = 0, col = 0;
                char *p = t->ansi_buf;
                while(*p && *p != ';'){
                    row = row * 10 + (*p - '0');
                    p++;
                }
                if(*p == ';') p++;
                while(*p){
                    col = col * 10 + (*p - '0');
                    p++;
                }
                // Convert 1-based to 0-based
                if(row > 0) row--;
                if(col > 0) col--;
                
                // Set cursor position
                int old_row = t->cursor_row;
                if(row < ROWS) t->cursor_row = row;
                if(col < COLS) t->cursor_col = col;
                
                // Redraw affected lines
                redraw_line(w, old_row);
                if(t->cursor_row != old_row) redraw_line(w, t->cursor_row);
            }
            // Reset state
            t->esc_state = 0;
        }
    }
    
    if(t->cursor_col >= COLS){
        t->cursor_col = 0;
        t->cursor_row++;
    }
    
    if(t->cursor_row >= ROWS){
        // Scroll Up
        for(int r=1; r<ROWS; r++){
            for(int co=0; co<COLS; co++){
                t->display[r-1][co] = t->display[r][co];
                draw_char(w, r-1, co, t->display[r-1][co], 0xFFFFFFFF);
            }
        }
        // Clear last row
        for(int co=0; co<COLS; co++){
             t->display[ROWS-1][co] = ' ';
             draw_char(w, ROWS-1, co, ' ', 0xFFFFFFFF); // Clear
        }
        t->cursor_row = ROWS-1;
    }
}

// Window Management
Window*
spawn_window(int x, int y)
{
  Window *win = malloc(sizeof(Window));
  win->id = next_win_id++;
  win->x = x; win->y = y;
  win->w = COLS*CHAR_W + 2*PADDING;
  win->h = ROWS*(CHAR_H + LINE_SPACING) + 2*PADDING + 20;
  win->buf = malloc(win->w * win->h * 4);
  win->next = 0;

  // Clear Buffer
  for(int i=0; i<win->w*win->h; i++) win->buf[i] = 0xFFCCCCCC;
  // Title Bar - will be cyan when focused, blue otherwise
  uint title_color = UNFOCUS_COLOR; // Blue for unfocused (will update on focus)
  for(int py=0; py<20; py++)
    for(int px=0; px<win->w; px++) win->buf[py*win->w + px] = title_color;
  // Client Area Black
  for(int py=20; py<win->h; py++)
    for(int px=0; px<win->w; px++) win->buf[py*win->w + px] = TERM_BACK;

  // Setup Terminal
  win->term.cursor_row = 0;
  win->term.cursor_col = 0;
  win->term.input_start_row = 0;
  win->term.input_start_col = 0;
  for(int r=0; r<ROWS; r++)
      for(int c=0; c<COLS; c++)
          win->term.display[r][c] = ' ';

  // Spawn Shell
  int p_in[2];
  int p_out[2];
  pipe(p_in);
  pipe(p_out);
  
  int pid = fork();
  if(pid < 0){
      printf("sulu: fork failed\n");
      return 0;
  }
  if(pid == 0){
      // Child
      close(p_in[1]);
      close(p_out[0]);
      
      close(0); dup(p_in[0]);
      close(1); dup(p_out[1]);
      close(2); dup(p_out[1]);
      
      close(p_in[0]);
      close(p_out[1]);
      
      int fd = open("/bin/sh", O_RDONLY);
      if(fd < 0) exit(1);
      close(fd);

      char *argv[] = { "sh", 0 };
      exec("/bin/sh", argv); 
      exit(1);
  }
  
  // Parent
  close(p_in[0]);
  close(p_out[1]);
  
  win->term.pid = pid;
  win->term.fd_in = p_in[1];
  win->term.fd_out = p_out[0];
  
  // Link
  if(!windows) windows = win;
  else {
      struct Window *curr = windows;
      while(curr->next) curr = curr->next;
      curr->next = win;
  }
  focus_win = win;
  update_title_colors();
  return win;
}

// Update title bar colors based on focus
void
update_title_colors()
{
  Window *w = windows;
  while(w){
      // Update title bar color
      uint color = (w == focus_win) ? 0xFF00FFCB : 0xFF3333AA;
      for(int py=0; py<20; py++)
          for(int px=0; px<w->w; px++)
              w->buf[py*w->w + px] = color;
      
      // Redraw all terminal lines to update text/cursor colors
      for(int r=0; r<ROWS; r++)
          redraw_line(w, r);
          
      w = w->next;
  }
}

// Draw cursor at current mouse position
// Also erases previous cursor by restoring background
void
draw_cursor(int x, int y)
{
  static int last_x = -1, last_y = -1;
  
  // Erase old cursor by restoring background from windows
  if(last_x >= 0 && last_y >= 0){
      int cx = last_x;
      int cy = last_y;
      if(cx > SCREEN_W-10) cx = SCREEN_W-10;
      if(cy > SCREEN_H-10) cy = SCREEN_H-10;
      if(cx < 0) cx = 0;
      if(cy < 0) cy = 0;
      
      // Restore background for old cursor area
      for(int dy=0; dy<10; dy++){
          for(int dx=0; dx<10; dx++){
              int sx = cx + dx;
              int sy = cy + dy;
              if(sx >= SCREEN_W || sy >= SCREEN_H) continue;
              
              // Restore from background or window
              fb[sy*SCREEN_W + sx] = BACK_COLOR; // Background color
              
              // Check if any window covers this pixel
              Window *w = windows;
              while(w){
                  if(sx >= w->x && sx < w->x + w->w && 
                     sy >= w->y && sy < w->y + w->h){
                      int wx = sx - w->x;
                      int wy = sy - w->y;
                      fb[sy*SCREEN_W + sx] = w->buf[wy*w->w + wx];
                      break;
                  }
                  w = w->next;
              }
          }
      }
  }
  
  // Draw new cursor
  int cx = x;
  int cy = y;
  if(cx > SCREEN_W-10) cx = SCREEN_W-10;
  if(cy > SCREEN_H-10) cy = SCREEN_H-10;
  if(cx < 0) cx = 0;
  if(cy < 0) cy = 0;
  
  for(int dy=0; dy<10; dy++)
      for(int dx=0; dx<10; dx++)
           fb[(cy+dy)*SCREEN_W + (cx+dx)] = CURSOR_COLOR;
           
  last_x = x;
  last_y = y;
  
  // TODO: Implement dirty region tracking to only flush changed screen areas
  // instead of entire 1280x800 framebuffer. Would significantly reduce GPU
  // transfer overhead, especially for cursor-only updates (10x10 vs 1024000 pixels)
}

// Composite all windows (without cursor)
void
composite()
{
  // TODO: Optimize to only redraw windows that changed position/content
  // Currently clears and redraws all windows even if only one moved
  // Future: Track dirty regions and skip unchanged windows
  
  for(int i=0; i<SCREEN_W*SCREEN_H; i++) fb[i] = BACK_COLOR; // Clear
  
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
               fb[y*SCREEN_W + x] = w->buf[dy*w->w + dx];
          }
      }
      w = w->next;
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

// Keyboard Map (Partial)
char kbd_map[128] = { 0, 0, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' ' };

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

  spawn_window(50, 50);
  spawn_window(600, 100);
  
  composite();
  
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
        // Full redraw
        composite();
        draw_cursor(mouse_x, mouse_y);
        gpu_flush();
      } else if(cmd == 3) {  // Quit
        exit(0);
      }
      
      // If suspended, don't process input or terminals normally
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
            draw_cursor(mouse_x, mouse_y);
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
                      // Dragging - rate limit composite to reduce jitter
                      drag_win->x = mouse_x - drag_off_x;
                      drag_win->y = mouse_y - drag_off_y;
                      
                      // Only composite every Nth event (50% rate for smoothness)
                      if(++composite_skip_counter >= COMPOSITE_RATE_LIMIT){
                          composite_skip_counter = 0;
                          composite();
                          draw_cursor(mouse_x, mouse_y);
                          gpu_flush();
                      } else {
                          // Skip composite, just update cursor
                          draw_cursor(mouse_x, mouse_y);
                          gpu_flush();
                      }
                  } else {
                      if(prev_mouse_x != mouse_x || prev_mouse_y != mouse_y){
                          draw_cursor(mouse_x, mouse_y);
                          gpu_flush();
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
                              focus_win = hit;
                              update_title_colors();
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
                  // Keyboard State
                  else if(ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT){
                      shift_state = (ev.value == 1);
                  } else if(ev.code == KEY_LEFTCTRL || ev.code == KEY_RIGHTCTRL){
                      ctrl_pressed = (ev.value == 1);
                  } else if(ev.code == KEY_CAPSLOCK){
                      if(ev.value == 1) capslock_state = !capslock_state;
                  }
                  // Hotkeys
                  else if(ev.code == KEY_E && ev.value == 1 && ctrl_pressed){
                      // Ctrl+E -> Suspend
                      int fd = open("/dev/suluctl", O_WRONLY);
                      if(fd >= 0){
                          write(fd, "suspend", 7);
                          close(fd);
                      }
                  }
                  // Arrow Keys - Local Cursor Movement
                  else if(ev.value == 1 || ev.value == 2){ // Press or Repeat
                      if(focus_win){
                          Terminal *t = &focus_win->term;
                          int old_row = t->cursor_row;
                          
                          if(ev.code == KEY_LEFT){
                              // Don't move left past the input boundary
                              if(t->cursor_row == t->input_start_row && t->cursor_col > t->input_start_col){
                                  t->cursor_col--;
                                  redraw_line(focus_win, old_row);
                                  did_update = 1;
                              } else if(t->cursor_row > t->input_start_row && t->cursor_col > 0){
                                  t->cursor_col--;
                                  redraw_line(focus_win, old_row);
                                  did_update = 1;
                              }
                          } else if(ev.code == KEY_RIGHT){
                              // Don't move past the end of actual content
                              int line_end = find_line_end(t, t->cursor_row);
                              if(t->cursor_col < line_end && t->cursor_col < COLS - 1){
                                  t->cursor_col++;
                                  redraw_line(focus_win, old_row);
                                  did_update = 1;
                              }
                          } else if(ev.code == KEY_UP){
                              // Don't move up past the input boundary row
                              if(t->cursor_row > t->input_start_row){
                                  t->cursor_row--;
                                  redraw_line(focus_win, old_row);
                                  redraw_line(focus_win, t->cursor_row);
                                  did_update = 1;
                              }
                          } else if(ev.code == KEY_DOWN){
                              if(t->cursor_row < ROWS - 1){
                                  t->cursor_row++;
                                  redraw_line(focus_win, old_row);
                                  redraw_line(focus_win, t->cursor_row);
                                  did_update = 1;
                              }
                          }
                          // Character Input
                          else if(ev.code < 128){
                              char ch = 0;
                              int is_alpha = 0;
                              // Alpha ranges: 16-25 (q-p), 30-38 (a-l), 44-50 (z-m)
                              if((ev.code >= 16 && ev.code <= 25) || (ev.code >= 30 && ev.code <= 38) || (ev.code >= 44 && ev.code <= 50))
                                  is_alpha = 1;

                              if(is_alpha){
                                  if(shift_state ^ capslock_state) ch = keymap_shift[ev.code];
                                  else ch = keymap[ev.code];
                              } else {
                                  if(shift_state) ch = keymap_shift[ev.code];
                                  else ch = keymap[ev.code];
                              }
                              
                              if(ch != 0){
                                  // printf("key: %d -> %c\n", ev.code, ch);
                                  
                                  // Local Echo
                                  term_putc(focus_win, ch);
                                  did_update = 1;
                                  
                                  // Send to Shell
                                  write(focus_win->term.fd_in, &ch, 1);
                              }
                          }
                      }
                  }
              }
          }
      }
      
      // 2. Process Terminals
      Window *w = windows;
      while(w){
          int avail = readavail(w->term.fd_out);
          if(avail > 0){
               // printf("shell output: %d bytes\n", avail);
               char buf[64];
               if(avail > 64) avail = 64;
               int r = read(w->term.fd_out, buf, avail);
               if(r > 0) {
                   for(int i=0; i<r; i++){
                       term_putc(w, buf[i]);
                   }
                   // Mark where user input starts (after shell output)
                   w->term.input_start_row = w->term.cursor_row;
                   w->term.input_start_col = w->term.cursor_col;
                   did_update = 1;
               }
          }
          w = w->next;
      }
      
      if(did_update){
          composite();
          draw_cursor(mouse_x, mouse_y);
          gpu_flush();
      }
      
      // Avoid busy loop if idle?
      // Not ideal, but sleep(1) is too slow.
  }
}
