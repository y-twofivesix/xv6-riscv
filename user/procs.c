// procs.c - Graphical Process Viewer for Sulu
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "user/sulu_client.h"

#define WIN_W 400
#define WIN_H 300
#define ROW_HEIGHT 16
#define HEADER_HEIGHT 20
#define MAX_PROCS 32
#define TITLE_H 20  // Title bar height

// Colors
#define BG_COLOR     0xFF1A1A2E
#define HEADER_BG    0xFF16213E
#define ROW_BG1      0xFF1A1A2E
#define ROW_BG2      0xFF262640
#define TEXT_COLOR   0xFFE0E0E0
#define HEADER_TEXT  0xFF00D9FF
#define KILL_BTN_BG  0xFFAA2222
#define SELECTED_BG  0xFF3A3A6E

static struct sulu_window win;
static struct sulu_window_shm *shm;
static struct procinfo procs[MAX_PROCS];
static int proc_count = 0;
static int selected = -1;

static char *state_names[] = {"unused", "used", "sleep", "ready", "run", "zombie"};

// Draw a filled rectangle directly to pixel buffer
void draw_rect(int x, int y, int w, int h, uint32 color) {
  uint32 *fb = sulu_pixels(shm);
  int width = shm->width;
  int height = shm->height;
  for(int j = y; j < y + h && j < height; j++){
    for(int i = x; i < x + w && i < width; i++){
      if(i >= 0 && j >= 0)
        fb[j * width + i] = color;
    }
  }
}

// Draw string using sulu font
void draw_str(int x, int y, char *s, uint32 color) {
  while(*s){
    sulu_draw_char(shm, x, y, *s, color);
    x += 8;
    s++;
  }
}

// Draw the table header
void draw_header(void) {
  draw_rect(0, 0, WIN_W, HEADER_HEIGHT, HEADER_BG);
  draw_str(10, 6, "PID", HEADER_TEXT);
  draw_str(50, 6, "NAME", HEADER_TEXT);
  draw_str(160, 6, "STATE", HEADER_TEXT);
  draw_str(240, 6, "MEM", HEADER_TEXT);
  draw_str(320, 6, "KILL", HEADER_TEXT);
}

// Draw a single process row
void draw_row(int row, struct procinfo *p, int is_selected) {
  int y = HEADER_HEIGHT + row * ROW_HEIGHT;
  uint32 bg = is_selected ? SELECTED_BG : (row % 2 == 0 ? ROW_BG1 : ROW_BG2);
  
  draw_rect(0, y, WIN_W, ROW_HEIGHT, bg);
  
  char buf[32];
  
  // PID
  memset(buf, 0, sizeof(buf));
  sprintf(buf, "%d", p->pid);
  draw_str(10, y + 4, buf, TEXT_COLOR);
  
  // Name
  draw_str(50, y + 4, p->name, TEXT_COLOR);
  
  // State
  if(p->state >= 0 && p->state <= 5)
    draw_str(160, y + 4, state_names[p->state], TEXT_COLOR);
  
  // Memory (in KB or MB)
  memset(buf, 0, sizeof(buf));
  int kb = (int)(p->sz / 1024);
  if(kb > 999)
    sprintf(buf, "%dM", kb / 1024);
  else
    sprintf(buf, "%dK", kb);
  draw_str(240, y + 4, buf, TEXT_COLOR);
  
  // Kill button (only for non-essential processes)
  if(p->pid > 2) {
    draw_rect(320, y + 2, 40, ROW_HEIGHT - 4, KILL_BTN_BG);
    draw_str(326, y + 4, "KILL", 0xFFFFFFFF);
  }
}

// Refresh process list
void refresh_procs(void) {
  proc_count = procinfo(procs, MAX_PROCS);
  if(proc_count < 0) proc_count = 0;
}

// Render the full window
void render(void) {
  draw_rect(0, 0, WIN_W, WIN_H, BG_COLOR);
  draw_header();
  for(int i = 0; i < proc_count && i < (WIN_H - HEADER_HEIGHT) / ROW_HEIGHT; i++){
    draw_row(i, &procs[i], i == selected);
  }
}

// Handle click
void handle_click(int x, int y) {
  if(y < HEADER_HEIGHT) return;
  
  int row = (y - HEADER_HEIGHT) / ROW_HEIGHT;
  if(row >= 0 && row < proc_count){
    selected = row;
    if(x >= 320 && x < 360 && procs[row].pid > 2){
      kill(procs[row].pid);
      sleep(10);
      refresh_procs();
    }
  }
}

int
main(int argc, char *argv[])
{
  if(sulu_init(&win, WIN_W, WIN_H, BG_COLOR, 0) < 0){
    printf("procs: sulu_init failed\n");
    exit(1);
  }
  
  shm = win.shm;
  
  refresh_procs();
  render();
  sulu_blit(shm, 0, 0, WIN_W, WIN_H);
  
  uint64 last_refresh = uptime();
  
  while(1){
    struct sulu_event ev;
    while(sulu_event_available(shm)){
      if(sulu_event_pop(shm, &ev) < 0) break;
      
      if(ev.type == SULU_EV_CLOSE){
        sulu_close(shm);
        exit(0);
      }
      
      if(ev.type == SULU_EV_MOUSE_BTN && ev.code == 0x110 && ev.value == 1){
        // Convert screen coords to window-local coords
        int local_x = ev.x - shm->x;
        int local_y = ev.y - shm->y - TITLE_H;
        handle_click(local_x, local_y);
        render();
        sulu_blit(shm, 0, 0, WIN_W, WIN_H);
      }
    }
    
    if(uptime() - last_refresh >= 100){
      refresh_procs();
      render();
      sulu_blit(shm, 0, 0, WIN_W, WIN_H);
      last_refresh = uptime();
    }
    
    yield();
  }
  
  return 0;
}
