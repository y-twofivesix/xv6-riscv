// Terminal Emulator - Sulu Client
// A standalone terminal that connects to Sulu via the client API

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "user/font.h"
#include "user/sulu_client.h"

// Terminal dimensions (default)
int g_cols = 60;
int g_rows = 20;

#define CHAR_W 8
#define CHAR_H 8
#define PADDING 8
#define LINE_SPACING 8
#define TITLE_HEIGHT 20

// Initial window size calculation
#define INITIAL_WIDTH  (g_cols * CHAR_W + 2 * PADDING)
#define INITIAL_HEIGHT (g_rows * (CHAR_H + LINE_SPACING) + 2 * PADDING)

// Colors
#define TERM_BACK     0xFF040720  // Fully opaque dark purple
#define TEXT_COLOR    0xFFFFFFFF
#define CURSOR_COLOR  0xFF00FFCB
#define SEL_COLOR     0xFF004488  // Darker blue for selection

// Input Event Codes
#define EV_KEY 0x01
#define KEY_LEFTSHIFT 42
#define KEY_RIGHTSHIFT 54
#define KEY_CAPSLOCK 58
#define KEY_LEFTCTRL 29
#define KEY_RIGHTCTRL 97
#define KEY_UP 103
#define KEY_DOWN 108
#define KEY_LEFT 105
#define KEY_RIGHT 106
#define KEY_C 46
#define KEY_P 25
#define BTN_LEFT 0x110

// Keyboard maps
char keymap[128] = {
  0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
  0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
  'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

char keymap_shift[128] = {
  0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
  0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
  'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

// Terminal state
typedef struct {
  char display[100][160]; // 100 rows scrollback
  int cursor_row;         // Current insertion row (absolute)
  int cursor_col;
  int esc_state;
  int input_start_row;    // Absolute row where input started
  int input_start_col;
  int scroll_offset;      // How many rows we are scrolled UP (0 = bottom)
  int active_rows;        // Total rows containing data
  int is_selecting;
  int sel_start_row, sel_start_col;
  int sel_end_row, sel_end_col;
  char ansi_buf[16];
  int ansi_idx;
  char clipboard[2048];
  int clipboard_len;
} Terminal;

// Globals
struct sulu_window win;
Terminal term;
int shell_fd_in;   // Write to shell
int shell_fd_out;  // Read from shell
int shift_state = 0;
int ctrl_pressed = 0;
int capslock_state = 0;

// Helper to access SHM/Pixels from global win
#define shm (win.shm)
#define pixels (win.pixels)
#define WIN_W (win.width)
#define WIN_H (win.height)

// Draw a single character at pixel position (with background)
void draw_char_at(int px, int py, char ch, uint color, int highlighted) {
  uint bg = highlighted ? SEL_COLOR : TERM_BACK;
  sulu_fill_rect(shm, px, py, CHAR_W, CHAR_H, bg);
  if (ch != 0) sulu_draw_char(shm, px, py, ch, color);
}

// Helper: Check if absolute (r, c) is in current selection
int is_in_selection(int r, int c) {
  if (term.sel_start_row == -1) return 0;
  
  int r1 = term.sel_start_row, c1 = term.sel_start_col;
  int r2 = term.sel_end_row, c2 = term.sel_end_col;
  
  // Normalize
  if (r1 > r2 || (r1 == r2 && c1 > c2)) {
    int tr = r1; r1 = r2; r2 = tr;
    int tc = c1; c1 = c2; c2 = tc;
  }
  
  if (r < r1 || r > r2) return 0;
  if (r == r1 && c < c1) return 0;
  if (r == r2 && c > c2) return 0;
  return 1;
}

// Redraw a single absolute row to its relative screen position
void redraw_line(int r) {
  // Check if row is within current visible window
  int screen_row = r - (term.active_rows > g_rows ? term.active_rows - g_rows : 0) + term.scroll_offset;
  if(screen_row < 0 || screen_row >= g_rows) return;

  int base_py = PADDING + screen_row * (CHAR_H + LINE_SPACING);
  
  // Clear line first
  sulu_fill_rect(shm, 0, base_py, WIN_W, CHAR_H + LINE_SPACING, TERM_BACK);
  
  int px = PADDING;
  for (int c = 0; c < g_cols; c++) {
    int py = base_py;
    char ch = term.display[r][c];
    int highlighted = is_in_selection(r, c);
    
    if (r == term.cursor_row && c == term.cursor_col) {
      draw_char_at(px, py, '{', CURSOR_COLOR, highlighted);
      px += CHAR_W;
      if (ch != 0 && ch != ' ') {
        draw_char_at(px, py, ch, TEXT_COLOR, highlighted);
        px += CHAR_W;
      }
      draw_char_at(px, py, '}', CURSOR_COLOR, highlighted);
      px += CHAR_W;
    } else {
      draw_char_at(px, py, ch, TEXT_COLOR, highlighted);
      px += CHAR_W;
    }
  }
}

// Redraw everything
void redraw_all() {
  sulu_clear(shm, TERM_BACK);
  int start_abs = (term.active_rows > g_rows ? term.active_rows - g_rows : 0) - term.scroll_offset;
  if (start_abs < 0) start_abs = 0;
  
  for (int i = 0; i < g_rows; i++) {
    int abs_row = start_abs + i;
    if (abs_row < 100) {
      redraw_line(abs_row);
    }
  }
}

// Legacy draw_char for scrolling
void draw_char(int r, int c, char ch, uint color) {
  int px = PADDING + c * CHAR_W;
  int py = PADDING + r * (CHAR_H + LINE_SPACING);
  draw_char_at(px, py, ch, color, is_in_selection(r, c));
}

// Convert screen (mx, my) to absolute buffer (row, col)
void mouse_to_buffer(int mx, int my, int *row, int *col) {
  int lx = mx - shm->x;
  int ly = my - shm->y - TITLE_HEIGHT;
  
  *col = (lx - PADDING) / CHAR_W;
  int rel_row = (ly - PADDING) / (CHAR_H + LINE_SPACING);
  int base_abs = (term.active_rows > g_rows ? term.active_rows - g_rows : 0) - term.scroll_offset;
  *row = base_abs + rel_row;
  
  if (*col < 0) *col = 0;
  if (*col >= g_cols) *col = g_cols - 1;
  if (*row < 0) *row = 0;
  if (*row >= 100) *row = 99;
}

// Put a character to terminal (handles ANSI escape sequences)
void term_putc(char c) {
  if (term.esc_state == 0) {
    if (c == '\033') {
      term.esc_state = 1;
      term.ansi_idx = 0;
      term.ansi_buf[0] = 0;
    } else if (c == '\n') {
      int old_row = term.cursor_row;
      term.cursor_row++;
      term.cursor_col = 0;
      
      // Auto-scroll to bottom if view was at the bottom
      if (term.scroll_offset == 0) {
          redraw_all();
      } else {
          redraw_line(old_row);
      }
    } else if (c == '\r') {
      term.cursor_col = 0;
    } else if (c == '\b') {
      if (term.cursor_col > 0) {
        term.cursor_col--;
        redraw_line(term.cursor_row);  // Redraw to show cursor
      }
    } else if (c >= ' ' && c <= '~') {
      term.display[term.cursor_row][term.cursor_col] = c;
      term.cursor_col++;
      redraw_line(term.cursor_row);  // Redraw to show cursor at new position
    }
  } else if (term.esc_state == 1) {
    if (c == '[') {
      term.esc_state = 2;
    } else {
      term.esc_state = 0;
    }
  } else if (term.esc_state == 2) {
    if ((c >= '0' && c <= '9') || c == ';') {
      if (term.ansi_idx < 15) {
        term.ansi_buf[term.ansi_idx++] = c;
        term.ansi_buf[term.ansi_idx] = 0;
      }
    } else {
      // End of sequence
      if (c == 'J') {
        // Clear entire buffer
        for (int r = 0; r < 100; r++) {
          for (int co = 0; co < g_cols; co++) {
            term.display[r][co] = ' ';
          }
        }
        term.scroll_offset = 0;
        term.active_rows = g_rows;
        term.cursor_row = 0;
        term.cursor_col = 0;
        redraw_all();
      } else if (c == 'K') {
        // Clear to end of line
        for (int co = term.cursor_col; co < g_cols; co++) {
          term.display[term.cursor_row][co] = ' ';
          draw_char(term.cursor_row, co, ' ', TEXT_COLOR);
        }
      } else if (c == 'H') {
        // Cursor position
        int row = 0, col = 0;
        char *p = term.ansi_buf;
        while (*p && *p != ';') {
          row = row * 10 + (*p - '0');
          p++;
        }
        if (*p == ';') p++;
        while (*p) {
          col = col * 10 + (*p - '0');
          p++;
        }
        if (row > 0) row--;
        if (col > 0) col--;
        int old_row = term.cursor_row;
        if (row < g_rows) term.cursor_row = row;
        if (col < g_cols) term.cursor_col = col;
        redraw_line(old_row);
        if (term.cursor_row != old_row) redraw_line(term.cursor_row);
      } else if (c == 'A') { // Up
        int n = atoi(term.ansi_buf); if (n == 0) n = 1;
        int old_row = term.cursor_row;
        if (term.cursor_row >= n) term.cursor_row -= n;
        else term.cursor_row = 0;
        redraw_line(old_row); redraw_line(term.cursor_row);
      } else if (c == 'B') { // Down
        int n = atoi(term.ansi_buf); if (n == 0) n = 1;
        int old_row = term.cursor_row;
        if (term.cursor_row + n < 100) term.cursor_row += n;
        else term.cursor_row = 99;
        redraw_line(old_row); redraw_line(term.cursor_row);
      } else if (c == 'C') { // Forward
        int n = atoi(term.ansi_buf); if (n == 0) n = 1;
        while(n-- > 0 && term.cursor_col < g_cols - 1) term.cursor_col++;
        redraw_line(term.cursor_row);
      } else if (c == 'D') { // Backward
        int n = atoi(term.ansi_buf); if (n == 0) n = 1;
        while(n-- > 0 && term.cursor_col > 0) term.cursor_col--;
        redraw_line(term.cursor_row);
      }
      term.esc_state = 0;
    }
  }
  
  // Handle scroll / Wrap
  if (term.cursor_col >= g_cols) {
    term.cursor_col = 0;
    term.cursor_row++;
  }
  
  if (term.cursor_row >= 100) {
    // End of buffer, shift everything up
    for (int r = 1; r < 100; r++) {
      for (int co = 0; co < g_cols; co++) {
        term.display[r - 1][co] = term.display[r][co];
      }
    }
    for (int co = 0; co < g_cols; co++) {
      term.display[99][co] = ' ';
    }
    term.cursor_row = 99;
    term.input_start_row--;
    if(term.input_start_row < 0) term.input_start_row = 0;
  }
  
  if (term.cursor_row >= term.active_rows) {
    term.active_rows = term.cursor_row + 1;
  }
}

// Convert keycode to character
char key_to_char(int code) {
  if (code < 0 || code >= 128) return 0;
  
  char ch;
  if (shift_state) {
    ch = keymap_shift[code];
  } else {
    ch = keymap[code];
  }
  
  // Handle caps lock for letters
  if (ch >= 'a' && ch <= 'z' && capslock_state) {
    ch = ch - 'a' + 'A';
  } else if (ch >= 'A' && ch <= 'Z' && capslock_state) {
    ch = ch - 'A' + 'a';
  }
  
  return ch;
}

int
main(int argc, char *argv[])
{
  int shmid;
  
  // 1. Create and attach SHM using helper
  if (sulu_init(&win, INITIAL_WIDTH, INITIAL_HEIGHT, TERM_BACK, 0) < 0) {
    printf("terminal: sulu_init failed\n");
    exit(1);
  }

  // 3. Initialize terminal state
  term.cursor_row = 0;
  term.cursor_col = 0;
  term.esc_state = 0;
  term.input_start_row = 0;
  term.input_start_col = 0;
  for (int r = 0; r < g_rows; r++) {
    for (int c = 0; c < g_cols; c++) {
      term.display[r][c] = ' ';
    }
  }
  
  // 54. Spawn shell subprocess
  int p_in[2], p_out[2];
  pipe(p_in);
  pipe(p_out);
  
  int pid = fork();
  if (pid < 0) {
    printf("terminal: fork failed\n");
    exit(1);
  }
  
  if (pid == 0) {
    // Child - become shell
    close(p_in[1]);
    close(p_out[0]);
    
    close(0); dup(p_in[0]);
    close(1); dup(p_out[1]);
    close(2); dup(p_out[1]);
    
    close(p_in[0]);
    close(p_out[1]);
    
    char *argv[] = {"sh", 0};
    exec("/bin/sh", argv);
    exit(1);
  }
  
  // Parent - terminal
  close(p_in[0]);
  close(p_out[1]);
  shell_fd_in = p_in[1];
  shell_fd_out = p_out[0];
  
  // 6. Connect to Sulu - ALREADY DONE by sulu_init
  
  // Set window title and cursor
  sulu_set_title(shm, "Terminal");
  shm->cursor_type = SULU_CURSOR_IBEAM;
  
  sleep(50);  // Give Sulu time to create window
  
  // 7. Initial render - draw cursor at position 0,0
  term.sel_start_row = -1; // No selection
  redraw_line(0);
          sulu_blit(shm, 0, 0, WIN_W, WIN_H);
  
  // 8. Main loop
  while (1) {
    // Check for shell output
    int avail = readavail(shell_fd_out);
    if (avail > 0) {
      char buf[64];
      if (avail > 64) avail = 64;
      int r = read(shell_fd_out, buf, avail);
      if (r > 0) {
        for (int i = 0; i < r; i++) {
          term_putc(buf[i]);
          // Only update input_start on newline (new prompt line)
          if (buf[i] == '\n') {
            term.input_start_row = term.cursor_row;
            term.input_start_col = term.cursor_col;
          }
        }
        
        // Request Sulu to redraw
                sulu_blit(shm, 0, 0, WIN_W, WIN_H);
      }
    }
    
    // Check for input events from Sulu
    while (sulu_event_available(shm)) {
      struct sulu_event ev;
      sulu_event_pop(shm, &ev);
      
      if (ev.type == SULU_EV_CLOSE) {
        printf("terminal: closing\n");
        exit(0);
      }

      if (ev.type == SULU_EV_MAXIMIZE) {
        if(ev.value) { // Maximize
            // Request full screen (use WM provided dims if avail)
            int mw = (ev.x > 0) ? ev.x : SULU_SCREEN_W;
            int mh = (ev.y > 0) ? ev.y : SULU_SCREEN_H - SULU_TITLE_BAR_HEIGHT;
            sulu_resize(&win, mw, mh);
        } else { // Restore
            // Use dimensions provided in event x/y if available, else use default
            int rw = (ev.x > 0) ? ev.x : 480;
            int rh = (ev.y > 0) ? ev.y : 320;
            sulu_resize(&win, rw, rh);
        }
        // Update terminal dimensions
        g_cols = (win.width - 2 * PADDING) / CHAR_W;
        g_rows = (win.height - 2 * PADDING) / (CHAR_H + LINE_SPACING);

        // Redraw all content to the new buffer
        for(int i = 0; i < g_rows; i++) {
            redraw_line(i);
        }
        sulu_blit(shm, 0, 0, WIN_W, WIN_H);
        continue;
      }

      if (ev.type == SULU_EV_KEY && ev.value == 1) {
        int code = ev.code;

        // Scroll to bottom on any keyboard keypress if scrolled up
        // Exclude mouse buttons (codes >= 0x110)
        if (term.scroll_offset > 0 && code < 0x110) {
          term.scroll_offset = 0;
          redraw_all();
          sulu_blit(shm, 0, 0, WIN_W, WIN_H);
        }
        
        if (code < 0x110 && term.sel_start_row != -1) {
          // Don't clear on modifiers
          if (code != KEY_LEFTSHIFT && code != KEY_RIGHTSHIFT &&
              code != KEY_LEFTCTRL && code != KEY_RIGHTCTRL &&
              code != KEY_CAPSLOCK &&
              !(ctrl_pressed && (code == KEY_C || code == KEY_P))) {
            term.sel_start_row = -1;
            redraw_all();
            sulu_blit(shm, 0, 0, WIN_W, WIN_H);
          }
        }
        
        // Handle modifier keys
        if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
          shift_state = 1;
          continue;
        }
        if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) {
          ctrl_pressed = 1;
          continue;
        }

        // Copy (Ctrl+C)
        if (ctrl_pressed && code == KEY_C) {
          if (term.sel_start_row != -1) {
            int r1 = term.sel_start_row, c1 = term.sel_start_col;
            int r2 = term.sel_end_row, c2 = term.sel_end_col;
            if (r1 > r2 || (r1 == r2 && c1 > c2)) {
                int tr=r1; r1=r2; r2=tr; int tc=c1; c1=c2; c2=tc;
            }
            
            int len = 0;
            for(int r = r1; r <= r2; r++) {
               int cs = (r == r1) ? c1 : 0;
               int ce = (r == r2) ? c2 : g_cols - 1;
               for(int c = cs; c <= ce; c++) {
                   if(len < 2047) shm->clipboard[len++] = term.display[r][c];
               }
               if(r < r2 && len < 2047) shm->clipboard[len++] = '\n';
            }
            shm->clipboard[len] = 0;
            shm->clipboard_len = len;
            
            struct sulu_cmd cmd = { .type = SULU_CMD_CLIP_SET };
            sulu_cmd_push(shm, &cmd);
          }
          continue;
        }

        // Paste (Ctrl+P)
        if (ctrl_pressed && code == KEY_P) {
          struct sulu_cmd cmd = { .type = SULU_CMD_CLIP_GET };
          sulu_cmd_push(shm, &cmd);
          continue;
        }
        if (code == KEY_CAPSLOCK) {
          capslock_state = !capslock_state;
          continue;
        }
        
        // Handle arrow keys - Send ANSI escape sequences to shell
        if (code == KEY_UP) {
          char buf[3] = {'\033', '[', 'A'};
          write(shell_fd_in, buf, 3);
          continue;
        }
        if (code == KEY_DOWN) {
          char buf[3] = {'\033', '[', 'B'};
          write(shell_fd_in, buf, 3);
          continue;
        }
        if (code == KEY_LEFT) {
          char buf[3] = {'\033', '[', 'D'};
          write(shell_fd_in, buf, 3);
          continue;
        }
        if (code == KEY_RIGHT) {
          char buf[3] = {'\033', '[', 'C'};
          write(shell_fd_in, buf, 3);
          continue;
        }
        
        // Convert to character and send to shell
        char ch = key_to_char(code);
        if (ch != 0) {
          // Backspace: don't echo locally, let readline() handle visual feedback
          if (ch == '\b') {
            write(shell_fd_in, &ch, 1);
            sulu_blit(shm, 0, 0, WIN_W, WIN_H);
            continue;
          }
          write(shell_fd_in, &ch, 1);
          term_putc(ch);  // Echo regular characters
          sulu_blit(shm, 0, 0, WIN_W, WIN_H);
        }
      } else if (ev.type == SULU_EV_KEY && ev.value == 0) {
        int code = ev.code;
        if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
          shift_state = 0;
        }
        if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) {
          ctrl_pressed = 0;
        }
      }
      // Mouse Selection
      else if (ev.type == SULU_EV_MOUSE_BTN && ev.code == BTN_LEFT) {
        if (ev.value == 1) {
          term.is_selecting = 1;
          mouse_to_buffer(ev.x, ev.y, &term.sel_start_row, &term.sel_start_col);
          term.sel_end_row = term.sel_start_row;
          term.sel_end_col = term.sel_start_col;
          redraw_all();
          sulu_blit(shm, 0, 0, WIN_W, WIN_H);
        } else {
          term.is_selecting = 0;
        }
      }
      else if (ev.type == SULU_EV_MOUSE_MOVE && term.is_selecting) {
          mouse_to_buffer(ev.x, ev.y, &term.sel_end_row, &term.sel_end_col);
          redraw_all();
          sulu_blit(shm, 0, 0, WIN_W, WIN_H);
      }
      else if (ev.type == SULU_EV_PASTE) {
          // Auto-scroll to bottom on paste
          term.scroll_offset = 0;
          for(int i = 0; i < shm->clipboard_len; i++) {
              char c = shm->clipboard[i];
              if (c == 0) break;
              write(shell_fd_in, &c, 1);
              term_putc(c);
          }
          redraw_all();
          sulu_blit(shm, 0, 0, WIN_W, WIN_H);
      }
      // Mouse Wheel - Scrolling
      else if (ev.type == SULU_EV_MOUSE_WHEEL) {
        int delta = ev.value;
        if (delta > 0) { // Scroll UP
          if (term.scroll_offset < term.active_rows - g_rows) {
            term.scroll_offset++;
            redraw_all();
            sulu_blit(shm, 0, 0, WIN_W, WIN_H);
          }
        } else if (delta < 0) { // Scroll DOWN
          if (term.scroll_offset > 0) {
            term.scroll_offset--;
            redraw_all();
            sulu_blit(shm, 0, 0, WIN_W, WIN_H);
          }
        }
      }
    }
    
    yield();  // Yield to allow Sulu/Shell to run
  }
  
  sulu_close(shm);
  sulu_detach(shmid, shm);
  return 0;
}
