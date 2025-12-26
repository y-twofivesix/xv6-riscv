#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "user/sulu_client.h"

#define MAX_LINES 1000
#define MAX_LINE_LEN 128

// Colors (Solarized-ish)
#define COL_BG       0xFFFDF6E3
#define COL_FG       0xFF657B83
#define COL_GUTTER   0xFFEEE8D5
#define COL_GUTTER_FG 0xFF93A1A1
#define COL_STATUS   0xFF073642
#define COL_STATUS_FG 0xFF839496
#define COL_CURSOR   0xFFD33682
#define COL_SELECT   0xFFB58900 // Selection background

struct Line {
    char data[MAX_LINE_LEN];
    int len;
};

struct EditorState {
    struct Line lines[MAX_LINES];
    int num_lines;
    
    // Cursor
    int cx, cy; // cy is line index, cx is column index
    int wanted_cx; // For vertical movement
    
    // View
    int scroll_y; // Top visible line
    int view_h;   // Height in lines
    
    // Selection
    int sel_sx, sel_sy; // Start
    int sel_ex, sel_ey; // End
    int selecting;      // Boolean
    
    // File
    char filename[64];
    int dirty; // Has unsaved changes
    int shift_pressed;
} es;

// Font metrics
#define CHAR_W 8
#define CHAR_H 8
#define GUTTER_W 40
#define STATUS_H 20

// Sulu State
int shmid = -1;
struct sulu_window_shm *shm = 0;
int width = 600;
int height = 400;

// Forward Decls
void render();
void handle_input(struct sulu_event *e);
void load_file(char *path);
void save_file();


void main(int argc, char *argv[])
{
  // Initialize State
  memset(&es, 0, sizeof(es));
  es.num_lines = 1; // Start with 1 empty line
  
  // Load file if arg
  if(argc > 1) {
      load_file(argv[1]);
  } else {
      strcpy(es.filename, "Untitled");
  }

  struct sulu_window win;
  if(sulu_init(&win, width, height, COL_BG, 0) < 0){
      printf("editor: connection failed\n");
      exit(1);
  }
  shm = win.shm;
  strcpy(shm->title, "Text Editor");
  
  render();
  
  while(1){
      // Check for events
      if(shm->event_ring.head != shm->event_ring.tail){
          struct sulu_event *e = &shm->event_buf[shm->event_ring.tail % SULU_EVENT_RING_SIZE];
          
          if(e->type == SULU_EV_KEY){
              handle_input(e);
          } else if(e->type == SULU_EV_CLOSE){
               exit(0);
          }
          
          shm->event_ring.tail++;
      }
      
      sleep(1); 
  }
}

// Draw a simple rect
void fill_rect(int x, int y, int w, int h, uint32 color) {
    if(x < 0) { w += x; x = 0; }
    if(y < 0) { h += y; y = 0; }
    if(x + w > width) w = width - x;
    if(y + h > height) h = height - y;
    if(w <= 0 || h <= 0) return;

    uint *buf = sulu_pixels(shm);
    for(int j=0; j<h; j++){
        for(int i=0; i<w; i++){
            buf[(y+j)*width + (x+i)] = color;
        }
    }
}

// Draw char (using font from somewhere... wait, we don't have font in user space)
// Clients usually don't have font. Terminal uses 'sulu_draw_char' ??
// No, terminal.c does NOT draw chars. It writes to 'fb' using... wait.
// Looking at terminal.c: It has 'font8x16.h' included!
// I need to copy 'font8x16.h' or include it.
#include "user/font.h"

void draw_char(int x, int y, char c, uint32 color, uint32 bg) {
    if(c < 32 || c > 126) c = '?';
    const unsigned char *glyph = font_8x8[c - 32];
    
    uint *buf = sulu_pixels(shm);
    for(int row=0; row<8; row++){
        int py = y + row;
        if(py >= height) break;
        uint8 b = glyph[row];
        for(int col=0; col<8; col++){
            int px = x + col;
            if(px >= width) break;
            
            if((b >> (7-col)) & 1){
                buf[py*width + px] = color;
            } else {
                if(bg != 0) buf[py*width + px] = bg;
            }
        }
    }
}

void draw_text(int x, int y, char *str, uint32 color) {
    while(*str){
        draw_char(x, y, *str++, color, 0);
        x += 8;
    }
}

void render() {
    // 1. Background
    fill_rect(0, 0, width, height, COL_BG);
    
    // 2. Gutter
    fill_rect(0, 0, GUTTER_W, height - STATUS_H, COL_GUTTER);
    
    // 3. Status Bar
    fill_rect(0, height - STATUS_H, width, STATUS_H, COL_STATUS);
    
    // Draw Status Text
    // Draw Status Text
    char status[64];
    // Custom formatting without snprintf
    strcpy(status, es.filename);
    int len = strlen(status);
    strcpy(status + len, "   Ln ");
    len = strlen(status);
    // itoa-ish
    int l = es.cy + 1;
    if(l >= 100) { status[len++] = '0' + (l/100); l %= 100; }
    if(l >= 10)  { status[len++] = '0' + (l/10); l %= 10; }
    status[len++] = '0' + l;
    
    strcpy(status + len, " Col ");
    len = strlen(status);
    int c = es.cx + 1;
    if(c >= 100) { status[len++] = '0' + (c/100); c %= 100; }
    if(c >= 10)  { status[len++] = '0' + (c/10); c %= 10; }
    status[len++] = '0' + c;
    status[len] = 0;

    draw_text(10, height - 16, status, COL_STATUS_FG);
    
    // 4. Lines
    int visible_lines = (height - STATUS_H) / CHAR_H;
    es.view_h = visible_lines;
    
    for(int i=0; i<visible_lines; i++) {
        int line_idx = es.scroll_y + i;
        if(line_idx >= es.num_lines) break;
        
        int py = i * CHAR_H;
        
        // Draw Line Number
        // TODO: int to str
        // char lnbuf[8]; itoa(line_idx + 1, lnbuf); 
        // draw_text(2, py, lnbuf, COL_GUTTER_FG);
        
        // Draw Line Content
        struct Line *l = &es.lines[line_idx];
        for(int c=0; c<l->len; c++){
             int px = GUTTER_W + 5 + c * CHAR_W;
             uint32 bg = 0;
             uint32 fg = COL_FG;
             
             // Selection Highlight Check
             // Normalize selection
             int s_y = es.sel_sy;
             int s_x = es.sel_sx;
             int e_y = es.sel_ey;
             int e_x = es.sel_ex;
             
             // Swap if start > end
             if(s_y > e_y || (s_y == e_y && s_x > e_x)){
                 int ty = s_y; s_y = e_y; e_y = ty;
                 int tx = s_x; s_x = e_x; e_x = tx;
             }
             
             int in_sel = 0;
             if(es.selecting) {
                 if(line_idx > s_y && line_idx < e_y) in_sel = 1;
                 else if(line_idx == s_y && line_idx == e_y){
                     if(c >= s_x && c < e_x) in_sel = 1;
                 }
                 else if(line_idx == s_y){
                     if(c >= s_x) in_sel = 1;
                 }
                 else if(line_idx == e_y){
                     if(c < e_x) in_sel = 1;
                 }
             }
             
             if(in_sel) {
                 bg = COL_SELECT;
                 fg = COL_BG;
             }
             
             // Cursor
             if(line_idx == es.cy && c == es.cx) {
                 bg = COL_CURSOR;
                 fg = 0xFFFFFFFF;
             }
             
             draw_char(px, py, l->data[c], fg, bg);
        }
        
        // Draw Cursor if at end of line
        if(line_idx == es.cy && es.cx == l->len) {
             int px = GUTTER_W + 5 + l->len * CHAR_W;
             fill_rect(px, py, CHAR_W, CHAR_H, COL_CURSOR);
        }
    }
    
    sulu_blit(shm, 0, 0, width, height);
}

// Buffer Ops
void insert_char(char c) {
    struct Line *l = &es.lines[es.cy];
    if(l->len >= MAX_LINE_LEN - 1) return;
    
    // Shift right
    for(int i = l->len; i > es.cx; i--) {
        l->data[i] = l->data[i-1];
    }
    l->data[es.cx] = c;
    l->len++;
    es.cx++;
    es.dirty = 1;
}

void delete_char() {
    struct Line *l = &es.lines[es.cy];
    if(es.cx >= l->len) return; // End of line
    
    // Shift left
    for(int i = es.cx; i < l->len - 1; i++){
        l->data[i] = l->data[i+1];
    }
    l->len--;
    es.dirty = 1;
}

void split_line() {
    if(es.num_lines >= MAX_LINES - 1) return;
    
    struct Line *curr = &es.lines[es.cy];
    struct Line *next = &es.lines[es.cy + 1];
    
    // Shift lines down
    for(int i = es.num_lines; i > es.cy + 1; i--){
        es.lines[i] = es.lines[i-1];
    }
    
    // Copy content to next line
    int move_len = curr->len - es.cx;
    if(move_len > 0){
        memmove(next->data, curr->data + es.cx, move_len);
    }
    next->len = move_len;
    curr->len = es.cx;
    
    es.num_lines++;
    es.cy++;
    es.cx = 0;
    es.dirty = 1;
}

void merge_line() {
    if(es.cy == 0) return;
    
    struct Line *prev = &es.lines[es.cy - 1];
    struct Line *curr = &es.lines[es.cy];
    
    if(prev->len + curr->len >= MAX_LINE_LEN) return; // Too long
    
    es.cx = prev->len; // New cursor pos
    
    // Append current to prev
    memmove(prev->data + prev->len, curr->data, curr->len);
    prev->len += curr->len;
    
    // Shift lines up
    for(int i = es.cy; i < es.num_lines - 1; i++){
        es.lines[i] = es.lines[i+1];
    }
    es.num_lines--;
    es.cy--;
    es.dirty = 1;
}

// Input Handling
void handle_input(struct sulu_event *e) {
    int key = e->code;
    
    // Control Keys
    if(key == 42 || key == 54) { // Shift
        es.shift_pressed = (e->value == 1 || e->value == 2);
        return;
    }
    
    // Reset selection if moving without shift
    if((key == KEY_UP || key == KEY_DOWN || key == KEY_LEFT || key == KEY_RIGHT) && !es.shift_pressed && es.selecting){
        es.selecting = 0;
    }
    
    // Start selection if moving with shift
    if((key == KEY_UP || key == KEY_DOWN || key == KEY_LEFT || key == KEY_RIGHT) && es.shift_pressed){
        if(!es.selecting){
            es.selecting = 1;
            es.sel_sx = es.cx;
            es.sel_sy = es.cy;
        }
    }
    
    if(key == KEY_UP) {
        if(es.cy > 0) es.cy--;
        struct Line *l = &es.lines[es.cy];
        if(es.cx > l->len) es.cx = l->len; 
    }
    else if(key == KEY_DOWN) {
        if(es.cy < es.num_lines - 1) es.cy++;
        struct Line *l = &es.lines[es.cy];
        if(es.cx > l->len) es.cx = l->len;
    }
    else if(key == KEY_LEFT) {
        if(es.cx > 0) es.cx--;
        else if(es.cy > 0) {
            es.cy--;
            es.cx = es.lines[es.cy].len;
        }
    }
    else if(key == KEY_RIGHT) {
        if(es.cx < es.lines[es.cy].len) es.cx++;
        else if(es.cy < es.num_lines - 1) {
            es.cy++;
            es.cx = 0;
        }
    }
    
    // Update selection end
    if(es.selecting){
        es.sel_ex = es.cx;
        es.sel_ey = es.cy;
    }
    else if(key == '\n' || key == '\r' || key == KEY_ENTER) {
        split_line();
    }
    else if(key == 127 || key == 8) { // Backspace
        if(es.cx > 0) {
            es.cx--;
            delete_char();
        } else if(es.cy > 0) {
            merge_line();
        }
    }
    else if(key == 17) { // Ctrl+Q
        exit(0);
    }
    else if(key == 19) { // Ctrl+S
        save_file();
    }
    else if(key >= 32 && key <= 126) {
        insert_char((char)key);
    }
    
    // Scroll View
    if(es.cy < es.scroll_y) es.scroll_y = es.cy;
    if(es.cy >= es.scroll_y + es.view_h) es.scroll_y = es.cy - es.view_h + 1;
    
    render();
}

void save_file() {
    if(strlen(es.filename) == 0) return;
    int fd = open(es.filename, O_WRONLY | O_CREATE | O_TRUNC);
    if(fd < 0) return;
    
    for(int i=0; i<es.num_lines; i++){
        struct Line *l = &es.lines[i];
        if(l->len > 0) write(fd, l->data, l->len);
        write(fd, "\n", 1);
    }
    close(fd);
    es.dirty = 0;
}

void load_file(char *path) {
    int len = strlen(path);
    if(len > 63) len = 63;
    memmove(es.filename, path, len);
    es.filename[len] = 0;
    int fd = open(path, O_RDONLY);
    if(fd < 0) return;
    
    // Read buffer
    char buf[1024]; 
    int n;
    int line_idx = 0;
    int char_idx = 0;
    
    // Very simple parser
    while((n = read(fd, buf, sizeof(buf))) > 0){
        for(int i=0; i<n; i++){
            char c = buf[i];
            if(c == '\n'){
                es.lines[line_idx].len = char_idx;
                line_idx++;
                char_idx = 0;
                if(line_idx >= MAX_LINES) break;
            } else {
                if(char_idx < MAX_LINE_LEN){
                    es.lines[line_idx].data[char_idx++] = c;
                }
            }
        }
    }
    close(fd);
    es.num_lines = line_idx + 1;
}


