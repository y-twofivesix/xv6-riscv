// rect_demo.c - Simple Sulu API test client
// Draws an animated rectangle in a window

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "user/sulu_client.h"

int main(int argc, char *argv[])
{
  printf("rect_demo: starting...\n");
  
  // 1. Allocate shared memory for our window
  // Now supports multi-page SHM (up to 64KB = 16 pages)
  // 120x120 pixels = 57,600 bytes + 1000 header = ~58KB = 15 pages
  int width = 480;
  int height = 480;
  int shm_size = sizeof(struct sulu_window_shm) + (width * height * 4);
  int shm_key = getpid();  // Use our PID as unique key
  
  int shmid = shmget(shm_key, shm_size);
  if(shmid < 0) {
    printf("rect_demo: shmget failed\n");
    exit(1);
  }
  
  // 2. Map the shared memory
  struct sulu_window_shm *shm = (struct sulu_window_shm*)shmat(shmid, 0);
  if(shm == (void*)-1) {
    printf("rect_demo: shmat failed\n");
    exit(1);
  }
  printf("rect_demo: attached shm at %p, shmid %d\n", shm, shmid);
  printf("rect_demo: heap top: %p\n", sbrk(0));
  
  // 3. Connect to Sulu
  printf("rect_demo: sending connect request...\n");
  int fd = sulu_connect(shm_key, width, height);
  if(fd < 0) {
    printf("rect_demo: connection failed\n");
    exit(1);
  }
  printf("rect_demo: sent connect request\n");
  sleep(1);  // Give Sulu time to create window
  
  // 4. Get pixel buffer
  uint *pixels = sulu_pixels(shm);
  
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
    sulu_blit(shm, 0, 0, width, height);
    
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
  sulu_close(shm);
  
  // Note: We don't call sulu_detach here - Sulu will clean up the SHM
  // when it processes our SULU_CMD_CLOSE. Calling shmdt() here would
  // race with Sulu still reading from the SHM buffer.
  sleep(1);  // Give Sulu time to process close
  
  printf("rect_demo: done\n");
  exit(0);
}
