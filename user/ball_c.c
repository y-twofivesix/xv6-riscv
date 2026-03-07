// bouncing_ball.c - Simple Sulu API test client
// Draws an animated rectangle in a window

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "user/sulu_client.h"

int main(int argc, char*argv[])
{
  printf("bouncing_ball: starting...\n");
  
  // 1. Initialize window using all-in-one helper
  int width = 480;
  int height = 480;
  struct sulu_window win;
  
  if(sulu_init(&win, width, height, 0xFF0000FF, SULU_FLAG_DOUBLE_BUFFER) < 0) {
    printf("bouncing_ball: sulu_init failed\n");
    exit(1);
  }
  printf("bouncing_ball: window created at %p (Double Buffered + Dirty Rects)\n", win.shm);
  
  // 2. Set window title and cursor
  sulu_set_title(win.shm, "Smooth Bouncing Ball");
  win.shm->cursor_type = SULU_CURSOR_ARROW;
  
  sleep(10);
  
  // 3. Initial clear of BOTH buffers
  sulu_clear(win.shm, win.shm->bgcolor);
  sulu_swap(&win);
  sulu_clear(win.shm, win.shm->bgcolor);
  sulu_blit_rect(&win, 0, 0, width, height);
  
  // 5. Animation loop
  int x = 50, y = 120;
  int dx = 2, dy = 2; 
  int radius = 40;
  
  // Track the last drawn position for EACH buffer (to enable smart erase)
  int last_x[2] = {x, x};
  int last_y[2] = {y, y};
  
  for(int frame = 0; frame < 20000; frame++) {
    // Process events
    while(sulu_event_available(win.shm)) {
      struct sulu_event ev;
      sulu_event_pop(win.shm, &ev);
      if(ev.type == SULU_EV_CLOSE) exit(0);
      if(ev.type == SULU_EV_MAXIMIZE) {
        if(ev.value) {
            width = (ev.x > 0) ? ev.x : SULU_SCREEN_W;
            height = (ev.y > 0) ? ev.y : SULU_SCREEN_H - SULU_TITLE_BAR_HEIGHT;
        } else {
            width = (ev.x > 0) ? ev.x : 400;
            height = (ev.y > 0) ? ev.y : 300;
        }
        sulu_resize(&win, width, height);
        sulu_clear(win.shm, win.shm->bgcolor);
        sulu_swap(&win);
        sulu_clear(win.shm, win.shm->bgcolor);
        sulu_blit_rect(&win, 0, 0, width, height);
        // Reset last positions after resize
        last_x[0] = last_x[1] = x;
        last_y[0] = last_y[1] = y;
        continue;
      }
    }

    // --- High Performance Smart Erase Strategy ---
    
    // 1. Identify current back buffer (the one we are about to draw into)
    // sulu_pixels() always returns the pointer to the non-front buffer.
    int back_idx = 1 - win.shm->front_buf;

    // 2. Erase ONLY the ball's old position in THIS specific buffer
    sulu_fill_circle(win.shm, last_x[back_idx], last_y[back_idx], radius, win.shm->bgcolor);
    
    // Save the very-old coordinates for the blit union later
    int very_old_x = last_x[back_idx];
    int very_old_y = last_y[back_idx];

    // 3. Update physics
    int prev_x = x;
    int prev_y = y;
    x += dx;
    y += dy;
    if(x - radius <= 0 || x + radius >= width) dx = -dx;
    if(y - radius <= 0 || y + radius >= height) dy = -dy;

    // 4. Draw the ball at its new position
    sulu_fill_circle(win.shm, x, y, radius, 0xFFFF0000); // Red
    
    // 5. Store current position as the new "last known" for this buffer
    last_x[back_idx] = x;
    last_y[back_idx] = y;

    // 6. Swap - Atomically switch which buffer Sulu is looking at
    sulu_swap(&win);

    // 7. Tell Sulu to update the dirty region
    // The dirty region must cover:
    // - Where the ball was in the PREVIOUS front buffer (so we clear the ghost)
    // - Where the ball is in the NEW front buffer (the one we just swapped in)
    int min_x = very_old_x; if(x < min_x) min_x = x; if(prev_x < min_x) min_x = prev_x;
    int min_y = very_old_y; if(y < min_y) min_y = y; if(prev_y < min_y) min_y = prev_y;
    int max_x = very_old_x; if(x > max_x) max_x = x; if(prev_x > max_x) max_x = prev_x;
    int max_y = very_old_y; if(y > max_y) max_y = y; if(prev_y > max_y) max_y = prev_y;
    
    min_x -= radius; min_y -= radius;
    max_x += radius; max_y += radius;
    
    sulu_blit_rect(&win, min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);

    usleep(SULU_DEFAULT_FRAME_USEC);
  }
  
  // 6. Close window
  sleep(50);
  printf("bouncing_ball: closing window...\n");
  sulu_close(win.shm);
  
  // Note: We don't call sulu_detach here - Sulu will clean up the SHM
  // when it processes our SULU_CMD_CLOSE. Calling shmdt() here would
  // race with Sulu still reading from the SHM buffer.
  sleep(10);  // Give Sulu time to process close
  
  printf("bouncing_ball: done\n");
  exit(0);
}
