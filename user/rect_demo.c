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
  
  if(sulu_init(&win, width, height) < 0) {
    printf("rect_demo: sulu_init failed\n");
    exit(1);
  }
  printf("rect_demo: window created at %p\n", win.shm);
  
  // 2. Set window title
  sulu_set_title(win.shm, "Bouncing Box Demo");
  
  sleep(1);  // Give Sulu time to create window
  
  // 3. Use win.pixels directly from the struct
  uint *pixels = win.pixels;
  
  // 5. Animation loop
  int x = 50, y = 50;
  int dx = 2, dy = 2;
  int rect_w = 80, rect_h = 60;
  
  for(int frame = 0; frame < 20000; frame++) {
    // Clear to black
    for(int i = 0; i < width * height; i++)
      pixels[i] = 0xFF000000;
    
    // Draw red rectangle
    for(int py = y; py < y + rect_h && py < height; py++) {
      for(int px = x; px < x + rect_w && px < width; px++) {
        pixels[py * width + px] = 0xFFFF0000;  // Red
      }
    }
    
    // Send BLIT command
    sulu_blit(win.shm, 0, 0, width, height);
    
    // Update position
    x += dx;
    y += dy;
    if(x <= 0 || x + rect_w >= width) dx = -dx;
    if(y <= 0 || y + rect_h >= height) dy = -dy;

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
