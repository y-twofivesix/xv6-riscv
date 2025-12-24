// Terminal Emulator - Sulu Client
// A standalone terminal that connects to Sulu via the client API

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "user/font.h"
#include "user/sulu_client.h"

// Terminal dimensions
#define COLS 60
#define ROWS 20
#define CHAR_W 8
#define CHAR_H 8
#define PADDING 8
#define LINE_SPACING 8
#define TITLE_HEIGHT 20

// Window size = terminal content area
#define WIN_WIDTH  (COLS * CHAR_W + 2 * PADDING)
#define WIN_HEIGHT (ROWS * (CHAR_H + LINE_SPACING) + 2 * PADDING)

// Colors
#define TERM_BACK     0xFF040720  // Fully opaque dark purple
#define TEXT_COLOR    0xFFFFFFFF
#define CURSOR_COLOR  0xFF00FFCB

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
  char display[ROWS][COLS];
  int cursor_row;
  int cursor_col;
  int esc_state;
  int input_start_row;
  int input_start_col;
  char ansi_buf[16];
  int ansi_idx;
} Terminal;

// Globals
struct sulu_window_shm *shm;
uint *pixels;
Terminal term;
int shell_fd_in;   // Write to shell
int shell_fd_out;  // Read from shell
int shift_state = 0;
int ctrl_pressed = 0;
int capslock_state = 0;

// Draw a single character at pixel position (with background)
// Uses the Sulu API for the actual glyph rendering
void draw_char_at(int px, int py, char ch, uint color) {
  // First clear the character cell background
  sulu_fill_rect(shm, px, py, CHAR_W, CHAR_H, TERM_BACK);
  // Then draw the character using Sulu's font renderer
  sulu_draw_char(shm, px, py, ch, color);
}

// Redraw a single line
void redraw_line(int r) {
  int base_py = PADDING + r * (CHAR_H + LINE_SPACING);
  
  // Clear line first (full width, include line spacing)
  sulu_fill_rect(shm, 0, base_py, WIN_WIDTH, CHAR_H + LINE_SPACING, TERM_BACK);
  
  int px = PADDING;
  for (int c = 0; c < COLS; c++) {
    int py = base_py;
    char ch = term.display[r][c];
    
    if (r == term.cursor_row && c == term.cursor_col) {
      // Draw cursor {ch}
      draw_char_at(px, py, '{', CURSOR_COLOR);
      px += CHAR_W;
      if (ch != 0 && ch != ' ') {
        draw_char_at(px, py, ch, TEXT_COLOR);
        px += CHAR_W;
      }
      draw_char_at(px, py, '}', CURSOR_COLOR);
      px += CHAR_W;
    } else {
      if (ch != 0) draw_char_at(px, py, ch, TEXT_COLOR);
      px += CHAR_W;
    }
  }
}

// Legacy draw_char for scrolling
void draw_char(int r, int c, char ch, uint color) {
  int px = PADDING + c * CHAR_W;
  int py = PADDING + r * (CHAR_H + LINE_SPACING);
  draw_char_at(px, py, ch, color);
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
      redraw_line(old_row);
    } else if (c == '\r') {
      term.cursor_col = 0;
    } else if (c == '\b') {
      if (term.cursor_col > 0) {
        term.cursor_col--;
        term.display[term.cursor_row][term.cursor_col] = ' ';  // Clear the character
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
        // Clear screen
        for (int r = 0; r < ROWS; r++) {
          for (int co = 0; co < COLS; co++) {
            term.display[r][co] = ' ';
            draw_char(r, co, ' ', TEXT_COLOR);
          }
        }
        term.cursor_row = 0;
        term.cursor_col = 0;
      } else if (c == 'K') {
        // Clear to end of line
        for (int co = term.cursor_col; co < COLS; co++) {
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
        if (row < ROWS) term.cursor_row = row;
        if (col < COLS) term.cursor_col = col;
        redraw_line(old_row);
        if (term.cursor_row != old_row) redraw_line(term.cursor_row);
      }
      term.esc_state = 0;
    }
  }
  
  // Handle line wrap
  if (term.cursor_col >= COLS) {
    term.cursor_col = 0;
    term.cursor_row++;
  }
  
  // Handle scroll
  if (term.cursor_row >= ROWS) {
    for (int r = 1; r < ROWS; r++) {
      for (int co = 0; co < COLS; co++) {
        term.display[r - 1][co] = term.display[r][co];
        draw_char(r - 1, co, term.display[r - 1][co], TEXT_COLOR);
      }
    }
    for (int co = 0; co < COLS; co++) {
      term.display[ROWS - 1][co] = ' ';
      draw_char(ROWS - 1, co, ' ', TEXT_COLOR);
    }
    term.cursor_row = ROWS - 1;
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
  int my_pid = getpid();
  int shmid;
  
  // 1. Create and attach SHM using helper
  shm = sulu_attach(my_pid, WIN_WIDTH, WIN_HEIGHT, 0, &shmid);
  if (!shm) {
    printf("terminal: sulu_attach failed\n");
    exit(1);
  }
  
  // Get pixel buffer
  pixels = sulu_pixels(shm);

  // 3. Initialize terminal state
  term.cursor_row = 0;
  term.cursor_col = 0;
  term.esc_state = 0;
  term.input_start_row = 0;
  term.input_start_col = 0;
  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
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
  
  // 6. Set bgcolor in SHM BEFORE connecting (Sulu will read this)
  shm->bgcolor = TERM_BACK;
  
  // 7. Connect to Sulu using the new /dev/sulu binary API
  int sulu_fd = sulu_connect(my_pid, WIN_WIDTH, WIN_HEIGHT);
  if (sulu_fd < 0) {
    printf("terminal: sulu_connect failed\n");
    exit(1);
  }
  
  // Set window title
  sulu_set_title(shm, "Terminal");
  
  sleep(50);  // Give Sulu time to create window
  
  // 7. Initial render - draw cursor at position 0,0
  redraw_line(0);
  sulu_blit(shm, 0, 0, WIN_WIDTH, WIN_HEIGHT);
  
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
        }
        term.input_start_row = term.cursor_row;
        term.input_start_col = term.cursor_col;
        
        // Request Sulu to redraw
        sulu_blit(shm, 0, 0, WIN_WIDTH, WIN_HEIGHT);
      }
    }
    
    // Check for input events from Sulu
    while (sulu_event_available(shm)) {
      struct sulu_event ev;
      sulu_event_pop(shm, &ev);
      
      if (ev.type == SULU_EV_KEY && ev.value == 1) {
        int code = ev.code;
        
        // Handle modifier keys
        if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
          shift_state = 1;
          continue;
        }
        if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) {
          ctrl_pressed = 1;
          continue;
        }
        if (code == KEY_CAPSLOCK) {
          capslock_state = !capslock_state;
          continue;
        }
        
        // Handle arrow keys
        if (code == KEY_LEFT) {
          // Don't move left past input start
          if (term.cursor_row == term.input_start_row && term.cursor_col > term.input_start_col) {
            term.cursor_col--;
            redraw_line(term.cursor_row);
            sulu_blit(shm, 0, 0, WIN_WIDTH, WIN_HEIGHT);
          }
          continue;
        }
        if (code == KEY_RIGHT) {
          // Move right within bounds (no content limit)
          if (term.cursor_col < COLS - 1) {
            term.cursor_col++;
            redraw_line(term.cursor_row);
            sulu_blit(shm, 0, 0, WIN_WIDTH, WIN_HEIGHT);
          }
          continue;
        }
        
        // Convert to character and send to shell
        char ch = key_to_char(code);
        if (ch != 0) {
          // Check for backspace - don't go past input start
          if (ch == '\b') {
            if (term.cursor_row == term.input_start_row && term.cursor_col <= term.input_start_col) {
              continue;  // Don't backspace past prompt
            }
          }
          write(shell_fd_in, &ch, 1);
          term_putc(ch);  // Echo
          sulu_blit(shm, 0, 0, WIN_WIDTH, WIN_HEIGHT);
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
      // Right-click - fill screen green (test)
      else if (ev.type == SULU_EV_MOUSE_BTN && ev.code == 0x111) {  // BTN_RIGHT

      }
    }
    
    yield();  // Yield to allow Sulu/Shell to run
  }
  
  sulu_close(shm);
  sulu_detach(shmid, shm);
  return 0;
}
