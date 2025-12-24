// rect_demo.c - Simple Sulu API test client
// Draws an animated rectangle in a window

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "user/sulu_client.h"

int main(int argc, char*argv[])
{
  printf("rect_demo: starting...\n");
  
  // 1. Initialize window using all-in-one helper
  int width = 480;
  int height = 480;
  struct sulu_window win;
  
  if(sulu_init(&win, width, height, SULU_FLAG_DOUBLE_BUFFER) < 0) {
    printf("rect_demo: sulu_init failed\n");
    exit(1);
  }
  printf("rect_demo: window created at %p\n", win.shm);
  
  // 2. Set window title
  sulu_set_title(win.shm, "Bouncing Box Demo");
  
  sleep(1);  // Give Sulu time to create window
  
  // 3. Use win.pixels directly from the struct
  
  // 5. Animation loop
  int x = 120, y = 50;
  int dx = 2, dy = 2;
  int radius = 40;
  
  for(int frame = 0; frame < 20000; frame++) {
    // Resize every 15000 frames to test
    if(frame > 0 && frame % 15000 == 0) {
      if(width == 480) {
        width = 320; height = 240;
      } else {
        width = 480; height = 480;
      }
      printf("rect_demo: resizing to %dx%d...\n", width, height);
      if(sulu_resize(&win, width, height) < 0) {
        printf("rect_demo: resize failed!\n");
        break;
      }
    }

    // Clear to black
    sulu_clear(win.shm, 0xFF000000);
    
    // Draw red circle using new API
    sulu_fill_circle(win.shm, x, y, radius, 0xFFFF0000);  // Red
    
    // Swap buffers (Double Buffering)
    sulu_swap(&win);
    
    // Update position
    x += dx;
    y += dy;
    if(x - radius <= 0 || x + radius >= width) dx = -dx;
    if(y - radius <= 0 || y + radius >= height) dy = -dy;

    sleep(1);

    // Yield to allow Sulu to composite immediately
    yield();
  }
  
  // 6. Close window
  sleep(5);
  printf("rect_demo: closing window...\n");
  sulu_close(win.shm);
  
  // Note: We don't call sulu_detach here - Sulu will clean up the SHM
  // when it processes our SULU_CMD_CLOSE. Calling shmdt() here would
  // race with Sulu still reading from the SHM buffer.
  sleep(1);  // Give Sulu time to process close
  
  printf("rect_demo: done\n");
  exit(0);
}
