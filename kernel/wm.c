#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

#define C(x)  ((x)-'@')  // Control-x

#define N_WINDOWS 5  // Window 0 is control, 1-4 are actual windows
#define INPUT_BUF_SIZE 128
#define ROWS 20
#define COLS 60

struct vwindow {
  struct spinlock lock;
  char buf[INPUT_BUF_SIZE];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
  uint cursor; // Cursor position (relative to w)
  uint last_visual_len; // Number of characters printed in last redraw
  int used; // 1 if window is active/spanned, 0 otherwise
  
  // Terminal Display Buffer
  char display[ROWS][COLS];
  int cursor_row;
  int cursor_col;
  int ansi_state; // 0=Normal, 1=ESC, 2=CSI
  char ansi_buf[16];
  int ansi_idx;
};

struct gwindow {
  int x, y, w, h;       // Geometry
  char title[32];       // Window Title
  struct vwindow vwin;  // Terminal Content
  int used;             // Allocation flag
};

struct {
  struct spinlock lock;
  int active; // 1-4
  struct gwindow windows[N_WINDOWS];
  // Control channel (minor 0)
  char ctl_buf[INPUT_BUF_SIZE];
  uint ctl_r, ctl_w;
  struct spinlock ctl_lock;
} wm;

void wm_redraw(struct gwindow *gwin);

void
wminit(void)
{
  int i;
  initlock(&wm.lock, "wm");
  wm.active = 1;
  initlock(&wm.ctl_lock, "wmctrl");
  wm.ctl_r = wm.ctl_w = 0;

  for(i = 0; i < N_WINDOWS; i++){
    initlock(&wm.windows[i].vwin.lock, "vwindow");
    wm.windows[i].vwin.r = 0;
    wm.windows[i].vwin.w = 0;
    wm.windows[i].vwin.e = 0;
    wm.windows[i].vwin.cursor = 0;
    wm.windows[i].vwin.last_visual_len = 0;
    wm.windows[i].vwin.used = 0;
    
    // Initialize display buffer
    wm.windows[i].vwin.cursor_row = 0;
    wm.windows[i].vwin.cursor_col = 0;
    wm.windows[i].vwin.ansi_state = 0;
    wm.windows[i].vwin.ansi_idx = 0;
    for(int r = 0; r < ROWS; r++){
      for(int c = 0; c < COLS; c++){
        wm.windows[i].vwin.display[r][c] = ' '; // Fill with spaces
      }
    }

    // Default Tiled Geometry
    wm.windows[i].used = (i == 1 ? 1 : 0); // Window 1 is default
    
    // Default Geometry (Tiled for now)
    if (i > 0) {
        wm.windows[i].x = 50 + (i-1)*50;
        wm.windows[i].y = 50 + (i-1)*50;
        wm.windows[i].w = 1000;
        wm.windows[i].h = 360; // 20 rows * 16 = 320 + 40 headers/padding
    }
  }
  
  // Connect WM major device
  devsw[WM].read = wmread;
  devsw[WM].write = wmwrite;

  // Hide hardware cursor from startup
  uartputc_sync('\033'); uartputc_sync('['); uartputc_sync('?');
  uartputc_sync('2'); uartputc_sync('5'); uartputc_sync('l');
}

// Switch the active window (visible output focus and input consumer)
void
wm_mouse_intr(int x, int y, int btn, int scroll)
{
  static int last_btn = 0;
  // Left click (bit 0) -> Focus Check
  if((btn & 1) && !(last_btn & 1)){
    int found = -1;
    acquire(&wm.lock);
    // Iterate from top to bottom (highest index usually drawn last/on top)
    for(int i = N_WINDOWS-1; i >= 1; i--){
      struct gwindow *w = &wm.windows[i];
      if(w->used && 
         x >= w->x && x < w->x + w->w &&
         y >= w->y && y < w->y + w->h){
        found = i;
        break;
      }
    }
    release(&wm.lock);
    
    if(found != -1 && found != wm.active){
       wm_switch(found);
    }
  }

  // Right click (bit 1) -> Create NEW window
  if((btn & 2) && !(last_btn & 2)){
    acquire(&wm.ctl_lock);
    if(wm.ctl_w - wm.ctl_r < INPUT_BUF_SIZE){
      wm.ctl_buf[wm.ctl_w % INPUT_BUF_SIZE] = 'N';
      wm.ctl_w++;
      wakeup(&wm.ctl_r);
    }
    release(&wm.ctl_lock);
  }

  if((btn & 4) && !(last_btn & 4)){
    printf("[WM] Middle click at %d,%d\n", x, y);
  }

  if(scroll != 0){
    printf("[WM] Scroll: %d\n", scroll);
  }

  last_btn = btn;
}

// Helper: Write character to display buffer with terminal logic
// vwin lock must be held
static void
wm_putc(struct vwindow *win, int c)
{
  // ANSI Parser
  if(win->ansi_state == 1){
      if(c == '[') {
          win->ansi_state = 2;
          win->ansi_idx = 0;
          win->ansi_buf[0] = 0;
          return;
      }
      win->ansi_state = 0; // Fallback
  } else if(win->ansi_state == 2){
      if(c >= '0' && c <= '9'){
          if(win->ansi_idx < 15) {
              win->ansi_buf[win->ansi_idx++] = c;
              win->ansi_buf[win->ansi_idx] = 0;
          }
      } else if(c == ';'){
          if(win->ansi_idx < 15) {
              win->ansi_buf[win->ansi_idx++] = c;
              win->ansi_buf[win->ansi_idx] = 0;
          }
      } else if(c >= 0x40 && c <= 0x7E){
          // Terminator
          if(c == 'J'){
              // Clear Screen. usually "2J"
              // For simplicity, any 'J' clears screen? Or check for '2'.
              if(win->ansi_buf[0] == '2' || win->ansi_buf[0] == 0){
                   for(int r=0; r<ROWS; r++)
                     for(int k=0; k<COLS; k++) 
                        win->display[r][k] = ' ';
                   // usually 2J doesn't move cursor, H does.
              }
          } else if(c == 'H'){
              // Home Cursor. "row;colH"
              // Simple parser: find semicolon
              int row = 0, col = 0;
              char *p = win->ansi_buf;
              while(*p && *p != ';'){
                  row = row * 10 + (*p - '0');
                  p++;
              }
              if(*p == ';') p++;
              while(*p){
                  col = col * 10 + (*p - '0');
                  p++;
              }
              if(row > 0) row--; // 1-based to 0-based
              if(col > 0) col--;
              
              if(row < ROWS) win->cursor_row = row;
              if(col < COLS) win->cursor_col = col;
          }
          win->ansi_state = 0;
      }
      return; // Ignore all chars in sequence
  } else if(c == 27){ // ESC
      win->ansi_state = 1;
      return;
  }

  if(c == '\n'){
    win->cursor_col = 0;
    win->cursor_row++;
  } else if(c == '\b' || c == 0x7F){ // Backspace
     if(win->cursor_col > 0) {
         win->cursor_col--;
         win->display[win->cursor_row][win->cursor_col] = ' ';
     } else if(win->cursor_row > 0) { // Wrap back
         win->cursor_row--;
         win->cursor_col = COLS - 1;
         win->display[win->cursor_row][win->cursor_col] = ' ';
     }
  } else {
    // Printable char
    win->display[win->cursor_row][win->cursor_col] = c;
    win->cursor_col++;
    if(win->cursor_col >= COLS){ // Auto wrap
       win->cursor_col = 0;
       win->cursor_row++;
    }
  }

  // Handle Scrolling
  while(win->cursor_row >= ROWS){
    // Shift up
    memmove(win->display[0], win->display[1], COLS * (ROWS - 1));
    // Clear last line
    memset(win->display[ROWS-1], ' ', COLS);
    win->cursor_row = ROWS - 1;
  }
}

void
wm_switch(int n)
{
  if(n < 1 || n >= N_WINDOWS)
    return;
  
  acquire(&wm.lock);
  wm.active = n;
  wm.windows[n].vwin.last_visual_len = 0; // Reset visual state on switch
  release(&wm.lock);

  if(!gui_active){
    // Headless/UART Mode
    // Use sync output for switching to avoid interleaving and hide hardware cursor
    char *header = "\n--- Window X ---\n";
    for(char *s = header; *s; s++){
      char c = *s;
      if(c == 'X') c = '0' + n;
      uartputc_sync(c);
    }
  } else {
    // Graphical Mode
    // Full redraw of the new active window
    acquire(&wm.windows[n].vwin.lock);
    wm_redraw(&wm.windows[n]);
    release(&wm.windows[n].vwin.lock);
  }
}

// Redraw the current input line with the custom bracket cursor
// win->lock MUST be held by caller.
// Redraw the graphical window
// gwin->vwin.lock MUST be held by caller.
void
wm_redraw(struct gwindow *gwin)
{
  struct vwindow *win = &gwin->vwin;

  if(!gui_active){
      // Headless/UART Mode
      uint i;
      uint new_len = 0;

      // Aggressively hide hardware cursor to ensure only brackets are visible
      uartputc_sync('\033'); uartputc_sync('['); uartputc_sync('?');
      uartputc_sync('2'); uartputc_sync('5'); uartputc_sync('l');

      // 1. Move back exactly the number of characters we printed last time
      for(i = 0; i < win->last_visual_len; i++) uartputc_sync('\b');
      // 2. Clear them with spaces
      for(i = 0; i < win->last_visual_len; i++) uartputc_sync(' ');
      // 3. Move back again
      for(i = 0; i < win->last_visual_len; i++) uartputc_sync('\b');

      // 4. Draw the line with brackets and track new visual length
      for(i = win->w; i < win->e; i++){
        if(i == win->cursor){
          uartputc_sync('\033'); uartputc_sync('['); uartputc_sync('1'); uartputc_sync('m');
          uartputc_sync('{');
          uartputc_sync(win->buf[i % INPUT_BUF_SIZE]);
          uartputc_sync('}');
          uartputc_sync('\033'); uartputc_sync('['); uartputc_sync('2'); uartputc_sync('2'); uartputc_sync('m');
          new_len += 3;
        } else {
          uartputc_sync(win->buf[i % INPUT_BUF_SIZE]);
          new_len += 1;
        }
      }
      // 5. If cursor is at the end, draw {}
      if(win->cursor == win->e){
        uartputc_sync('{');
        uartputc_sync('}');
        new_len += 2;
      }
      
      win->last_visual_len = new_len;
      return;
  }

  // Graphical Mode
  int start_x = gwin->x + 4;
  int start_y = gwin->y + 24; // Below title bar

  // 1. Draw Title Bar & Border (Already handled in previous step, ensuring standard bg)
  // Header background
  for(int dy = 0; dy < 20; dy++){
    for(int dx = 0; dx < gwin->w; dx++){
      framebuffer[(gwin->y + dy) * 1280 + (gwin->x + dx)] = 0xFF3333FF;
    }
  }
  // Client Area background (Black)
  for(int dy = 20; dy < gwin->h; dy++){
    for(int dx = 0; dx < gwin->w; dx++){
      framebuffer[(gwin->y + dy) * 1280 + (gwin->x + dx)] = 0xFF000000;
    }
  }
  
  // Title Text
  char title[32];
  for(int k=0; k<31; k++) title[k] = gwin->title[k];
  title[31] = 0; 
  if(title[0] == 0) { 
      title[0] = 'W'; title[1]='i'; title[2]='n'; title[3]=0;
  }
  gui_draw_string(gwin->x + 4, gwin->y + 6, title, 0xFFFFFFFF, 2);

  // 2. Render Display Buffer
  for(int r = 0; r < ROWS; r++){
    for(int c = 0; c < COLS; c++){
      int px = start_x + (c * 16);
      int py = start_y + (r * 16);
      
      char ch = win->display[r][c];
      // Default color white, maybe dim for empty?
      gui_draw_char(px, py, ch, 0xFFFFFFFF, 2);
    }
  }
  
  // 3. Draw Hardware Cursor
  // The user requested '{}' style cursor. We can simulate this graphically.
  // We'll draw yellow brackets around the current character cell.
  int cx = start_x + (win->cursor_col * 16);
  int cy = start_y + (win->cursor_row * 16);
  
  // Left bracket '{' - shift left by 12 px
  gui_draw_char(cx - 12, cy, '{', 0xFFFFFF00, 2); 
  // Right bracket '}' - shift right by 12 px (so it starts at +12 relative to cell start?)
  // Cell is 16 wide. center is +8. 
  // Let's try cx + 12.
  gui_draw_char(cx + 12, cy, '}', 0xFFFFFF00, 2);
  
  // Flush entire window area
  virtio_gpu_transfer(gwin->x, gwin->y, gwin->w, gwin->h);
  virtio_gpu_flush(gwin->x, gwin->y, gwin->w, gwin->h);
}

int
wmwrite(int user_src, uint64 src, int n, int minor)
{
  int i;
  struct vwindow *win = &wm.windows[minor].vwin;
  
  if(minor == 0){
    char c[2];
    if(either_copyin(c, user_src, src, n > 2 ? 2 : n) == -1)
      return -1;
    if(c[0] >= '1' && c[0] <= '4') wm_switch(c[0] - '0');
    if(c[0] == 'U' && c[1] >= '1' && c[1] <= '4'){
      acquire(&wm.lock);
      wm.windows[c[1] - '0'].used = 1;
      release(&wm.lock);
    }
    return n;
  }

  acquire(&wm.lock);
  int is_active = (wm.active == minor);
  release(&wm.lock);

  if(is_active){
    acquire(&win->lock);
    
    // Headless Fallback
    if(!gui_active){
        // 1. Clear old bracket cursor ONLY if we have something to clear
        if(win->last_visual_len > 0){
          for(i = 0; i < win->last_visual_len; i++) uartputc_sync('\b');
          for(i = 0; i < win->last_visual_len; i++) uartputc_sync(' ');
          for(i = 0; i < win->last_visual_len; i++) uartputc_sync('\b');
          win->last_visual_len = 0;
        }

        // 2. Print data (Use uartputc_sync to maintain order with cursor redraws)
        for(i = 0; i < n; i++){
          char c;
          if(either_copyin(&c, user_src, src+i, 1) == -1)
            break;
          uartputc_sync(c); 
          if(c == '\n') win->last_visual_len = 0; // Reset after newline
        }
        
        // 3. Restore bracket cursor
        wm_redraw(&wm.windows[minor]);
        
        release(&win->lock);
        return i;
    } 
    
    // Graphical Terminal Mode
    for(i = 0; i < n; i++){
      char c;
      if(either_copyin(&c, user_src, src+i, 1) == -1)
        break;
      wm_putc(win, c);
    }
    
    wm_redraw(&wm.windows[minor]);
    
    release(&win->lock);
    return i;
  } else {
    return n;
  }
}

int
wmread(int user_dst, uint64 dst, int n, int minor)
{
  uint target;
  int c;
  char cbuf;
  struct vwindow *win;

  if(minor == 0){ // Control channel
    acquire(&wm.ctl_lock);
    while(wm.ctl_r == wm.ctl_w){
      if(killed(myproc())){
        release(&wm.ctl_lock);
        return -1;
      }
      sleep(&wm.ctl_r, &wm.ctl_lock);
    }
    c = wm.ctl_buf[wm.ctl_r++ % INPUT_BUF_SIZE];
    release(&wm.ctl_lock);
    cbuf = c;
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1)
      return -1;
    return 1;
  }

  if(minor < 1 || minor >= N_WINDOWS)
    return -1;

  win = &wm.windows[minor].vwin;
  target = n;
  acquire(&win->lock);
  while(n > 0){
    while(win->r == win->w){
      if(killed(myproc())){
        release(&win->lock);
        return -1;
      }
      sleep(&win->r, &win->lock);
    }

    c = win->buf[win->r++ % INPUT_BUF_SIZE];
    if(c == C('D')){
      if(n < target) win->r--;
      break;
    }

    cbuf = c;
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    if(c == '\n') break;
  }
  release(&win->lock);

  return target - n;
}

// Window manager interrupt handler - distributes input
void
wmintr(int c)
{
  struct vwindow *win;
  static int state = 0;

  acquire(&wm.lock);
  win = &wm.windows[wm.active].vwin;
  release(&wm.lock);

  acquire(&win->lock);
  if(c == C('Q') || c == C('W')){
    acquire(&wm.lock);
    int next = wm.active;
    for(int i = 0; i < N_WINDOWS; i++){
      next = (next % (N_WINDOWS - 1)) + 1;
      if(wm.windows[next].used) break;
    }
    release(&wm.lock);
    wm_switch(next);
    release(&win->lock);
    return;
  }
  if(state == 0 && c == ESCAPE){
    state = 1;
    release(&win->lock);
    return;
  } else if(state == 1 && c == '['){
    state = 2;
    release(&win->lock);
    return;
  } else if(state == 2){
    if(c == 'D'){ // Left
      if(win->cursor > win->w) {
          win->cursor--;
          if(win->cursor_col > 0) {
              win->cursor_col--;
          } else if(win->cursor_row > 0) {
              win->cursor_row--;
              win->cursor_col = COLS - 1;
          }
      }
    } else if(c == 'C'){ // Right
      if(win->cursor < win->e) {
          win->cursor++;
          win->cursor_col++;
          if(win->cursor_col >= COLS){
              win->cursor_col = 0;
              win->cursor_row++;
          }
      }
    }
    wm_redraw(&wm.windows[wm.active]);
    state = 0;
    release(&win->lock);
    return;
  }
  state = 0;

  switch(c){
  case C('P'):
    procdump();
    break;
  case C('U'):
    while(win->e != win->w && win->buf[(win->e-1) % INPUT_BUF_SIZE] != '\n'){
      win->e--;
      wm_putc(win, '\b'); // Visual backspace
    }
    win->cursor = win->e;
    wm_redraw(&wm.windows[wm.active]);
    break;
  case C('H'):
  case '\x7f':
    if(win->cursor > win->w){
      // Shift everything after cursor left
      for(uint i = win->cursor; i < win->e; i++){
        win->buf[(i-1) % INPUT_BUF_SIZE] = win->buf[i % INPUT_BUF_SIZE];
      }
      win->e--;
      win->cursor--;
      
      wm_putc(win, '\b'); // Visual backspace
      wm_redraw(&wm.windows[wm.active]);
    }
    break;
  case '\n':
  case '\r':
    c = '\n';
    win->buf[win->e++ % INPUT_BUF_SIZE] = c;
    win->w = win->e;
    win->cursor = win->e;
    win->last_visual_len = 0; // Legacy field, can ignore
    
    wm_putc(win, '\n'); // Echo newline
    wm_redraw(&wm.windows[wm.active]);
    wakeup(&win->r);
    break;
  default:
    if(c != 0 && win->e-win->r < INPUT_BUF_SIZE){
      // Insert at cursor
      for(uint i = win->e; i > win->cursor; i--){
        win->buf[i % INPUT_BUF_SIZE] = win->buf[(i-1) % INPUT_BUF_SIZE];
      }
      win->buf[win->cursor % INPUT_BUF_SIZE] = c;
      win->e++;
      win->cursor++;
      
      wm_putc(win, c); // Echo character
      wm_redraw(&wm.windows[wm.active]);
      if(c == C('D') || win->e-win->r == INPUT_BUF_SIZE){
        win->w = win->e;
        wakeup(&win->r);
      }
    }
    break;
  }
  release(&win->lock);
}
