#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "user/font.h"

// Screen
#define SCREEN_W 1280
#define SCREEN_H 800

// Input Event Codes
#define EV_ABS 0x03
#define EV_KEY 0x01
#define ABS_X 0x00
#define ABS_Y 0x01
#define BTN_LEFT 0x110

// Keyboard
#define KEY_ESC 1
#define KEY_1 2

struct input_event {
  uint16 type;
  uint16 code;
  uint32 value;
};

// Terminal
#define COLS 60
#define ROWS 20
#define CHAR_W 8
#define CHAR_H 8
#define PADDING 4

typedef struct Terminal {
    int pid;
    int fd_in;  // Write here to send to shell
    int fd_out; // Read here from shell
    
    char display[ROWS][COLS];
    int cursor_row;
    int cursor_col;
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

// Text Drawing
void
draw_char(Window *w, int r, int c, char ch, uint color)
{
    if(ch < ' ' || ch > '~') return; // Skip non-printables
    
    int index = ch - ' ';
    int px = PADDING + c * CHAR_W;
    int py = 20 + PADDING + r * CHAR_H; // +20 for Titlebar
    
    for(int y=0; y<8; y++){
        for(int x=0; x<8; x++){
            if((font_8x8[index][y] >> (7-x)) & 1){
                 w->buf[(py+y)*w->w + (px+x)] = color;
            } else {
                 w->buf[(py+y)*w->w + (px+x)] = 0xFF000000; // Black bg
            }
        }
    }
}

void
term_putc(Window *w, char c)
{
    Terminal *t = &w->term;
    if(c == '\n'){
        t->cursor_col = 0;
        t->cursor_row++;
    } else if(c == '\b'){
        if(t->cursor_col > 0) t->cursor_col--;
    } else {
        t->display[t->cursor_row][t->cursor_col] = c;
        draw_char(w, t->cursor_row, t->cursor_col, c, 0xFFFFFFFF);
        t->cursor_col++;
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
  win->h = ROWS*CHAR_H + 2*PADDING + 20;
  win->buf = malloc(win->w * win->h * 4);
  win->next = 0;

  // Clear Buffer
  for(int i=0; i<win->w*win->h; i++) win->buf[i] = 0xFFCCCCCC;
  // Title Bar
  for(int py=0; py<20; py++)
    for(int px=0; px<win->w; px++) win->buf[py*win->w + px] = 0xFF3333AA;
  // Client Area Black
  for(int py=20; py<win->h; py++)
    for(int px=0; px<win->w; px++) win->buf[py*win->w + px] = 0xFF000000;

  // Setup Terminal
  win->term.cursor_row = 0;
  win->term.cursor_col = 0;
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
      printf("rio: fork failed\n");
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
  return win;
}

void
composite()
{
  for(int i=0; i<SCREEN_W*SCREEN_H; i++) fb[i] = 0xFF003333; // Clear
  
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
  
  // Draw Cursor
  int cx = mouse_x; int cy = mouse_y;
  if(cx > SCREEN_W-10) cx = SCREEN_W-10;
  if(cy > SCREEN_H-10) cy = SCREEN_H-10;
  for(int dy=0; dy<10; dy++)
      for(int dx=0; dx<10; dx++)
           fb[(cy+dy)*SCREEN_W + (cx+dx)] = 0xFFFFFFFF;
            
   gpu_flush();
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
  printf("rio: starting...\n");
  int shmid = shmget(SHM_FB, 0);
  if(shmid < 0) exit(1);
  fb = (uint*) shmat(shmid, 0);
  if((uint64)fb == -1) exit(1);
  int input_fd = open("/dev/input", O_RDONLY);
  if(input_fd < 0) exit(1);

  spawn_window(50, 50);
  spawn_window(600, 100);
  
  composite();
  
  struct input_event ev;
  while(1){
      // 1. Process Input
      int n = 0; 
      // Need non-blocking read on input too? 
      // No, inputread sleeps but we also need to update terminals.
      // IF we block on input, terminals will freeze until mouse moves.
      // So we need readavail on input_fd too!
      
      if(readavail(input_fd) > 0){
          n = read(input_fd, &ev, sizeof(ev));
          if(n == sizeof(ev)){
              if(ev.type == EV_ABS){
                  if(ev.code == ABS_X) mouse_x = (ev.value * SCREEN_W) / 32767;
                  if(ev.code == ABS_Y) mouse_y = (ev.value * SCREEN_H) / 32767;
                  if(mouse_btn && drag_win){
                      drag_win->x = mouse_x - drag_off_x;
                      drag_win->y = mouse_y - drag_off_y;
                      composite();
                  } else {
                      composite(); // Redraw cursor
                  }
              } else if(ev.type == EV_KEY){
                  if(ev.code == BTN_LEFT){
                      mouse_btn = (ev.value == 1);
                      if(mouse_btn){
                          Window *hit = find_window_at(mouse_x, mouse_y);
                          if(hit){
                              focus_win = hit;
                              if(mouse_y < hit->y + 20){
                                  drag_win = hit;
                                  drag_off_x = mouse_x - hit->x;
                                  drag_off_y = mouse_y - hit->y;
                              }
                          }
                      } else {
                          drag_win = 0;
                      }
                  } else {
                      // Keyboard
                      if(ev.value == 1 && focus_win){ // Key Press
                          char ch = 0;
                          if(ev.code < 128) ch = kbd_map[ev.code];
                          if(ch != 0){
                              write(focus_win->term.fd_in, &ch, 1);
                          }
                      }
                  }
              }
          }
      }
      
      // 2. Process Terminals
      int did_update = 0;
      Window *w = windows;
      while(w){
          int avail = readavail(w->term.fd_out);
          if(avail > 0){
               char buf[64];
               if(avail > 64) avail = 64;
               int r = read(w->term.fd_out, buf, avail);
               if(r > 0) {
                   for(int i=0; i<r; i++){
                       term_putc(w, buf[i]);
                   }
                   did_update = 1;
               }
          }
          w = w->next;
      }
      
      if(did_update) composite();
      
      // Avoid busy loop if idle?
      // Not ideal, but sleep(1) is too slow.
  }
}
