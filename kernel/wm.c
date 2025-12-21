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

struct vwindow {
  struct spinlock lock;
  char buf[INPUT_BUF_SIZE];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
  uint cursor; // Cursor position (relative to w)
  uint last_visual_len; // Number of characters printed in last redraw
};

struct {
  struct spinlock lock;
  int active; // 1-4
  struct vwindow windows[N_WINDOWS];
} wm;

void
wminit(void)
{
  int i;
  initlock(&wm.lock, "wm");
  wm.active = 1;
  for(i = 0; i < N_WINDOWS; i++){
    initlock(&wm.windows[i].lock, "window");
    wm.windows[i].r = 0;
    wm.windows[i].w = 0;
    wm.windows[i].e = 0;
    wm.windows[i].cursor = 0;
    wm.windows[i].last_visual_len = 0;
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
wm_switch(int n)
{
  if(n < 1 || n >= N_WINDOWS)
    return;
  
  acquire(&wm.lock);
  wm.active = n;
  wm.windows[n].last_visual_len = 0; // Reset visual state on switch
  release(&wm.lock);
  
  // Use sync output for switching to avoid interleaving and hide hardware cursor
  char *header = "\033[2J\033[H\033[?25l\n--- Window X ---\n";
  for(char *s = header; *s; s++){
    char c = *s;
    if(c == 'X') c = '0' + n;
    uartputc_sync(c);
  }
}

// Redraw the current input line with the custom bracket cursor
// win->lock MUST be held by caller.
void
wm_redraw(struct vwindow *win)
{
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

  // 3. Draw the line with brackets and track new visual length
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
  // 4. If cursor is at the end, draw {}
  if(win->cursor == win->e){
    // uartputc_sync('\033'); uartputc_sync('['); uartputc_sync('1'); uartputc_sync('m');
    uartputc_sync('{');
    uartputc_sync('}');
    // uartputc_sync('\033'); uartputc_sync('['); uartputc_sync('2'); uartputc_sync('2'); uartputc_sync('m');
    new_len += 2;
  }
  
  win->last_visual_len = new_len;
}

int
wmwrite(int user_src, uint64 src, int n, int minor)
{
  int i;
  struct vwindow *win = &wm.windows[minor];
  
  if(minor == 0){
    char c;
    if(either_copyin(&c, user_src, src, 1) == -1)
      return -1;
    if(c >= '1' && c <= '4') wm_switch(c - '0');
    return n;
  }

  acquire(&wm.lock);
  int is_active = (wm.active == minor);
  release(&wm.lock);

  if(is_active){
    acquire(&win->lock);
    
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
    wm_redraw(win);
    
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

  if(minor < 1 || minor >= N_WINDOWS)
    return -1;

  win = &wm.windows[minor];
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
  win = &wm.windows[wm.active];
  release(&wm.lock);

  acquire(&win->lock);
  if(c == C('Q') || c == C('W')){
    wm_switch((wm.active % 4) + 1);
    release(&win->lock);
    return;
  }

  // ANSI state machine
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
      if(win->cursor > win->w) win->cursor--;
    } else if(c == 'C'){ // Right
      if(win->cursor < win->e) win->cursor++;
    }
    wm_redraw(win);
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
    }
    win->cursor = win->e;
    wm_redraw(win);
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
      wm_redraw(win);
    }
    break;
  case '\n':
  case '\r':
    // Finalize the line: remove the bracket cursor before going to the next line
    for(uint i = 0; i < win->last_visual_len; i++) uartputc_sync('\b');
    for(uint i = 0; i < win->last_visual_len; i++) uartputc_sync(' ');
    for(uint i = 0; i < win->last_visual_len; i++) uartputc_sync('\b');
    
    // Print the raw line (shell/command text) without brackets
    for(uint i = win->w; i < win->e; i++){
      uartputc_sync(win->buf[i % INPUT_BUF_SIZE]);
    }

    c = '\n';
    win->buf[win->e++ % INPUT_BUF_SIZE] = c;
    win->w = win->e;
    win->cursor = win->e;
    win->last_visual_len = 0;
    uartputc_sync('\n');
    wm_redraw(win); // Show {} on the new line
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
      wm_redraw(win);
      if(c == C('D') || win->e-win->r == INPUT_BUF_SIZE){
        win->w = win->e;
        wakeup(&win->r);
      }
    }
    break;
  }
  release(&win->lock);
}
