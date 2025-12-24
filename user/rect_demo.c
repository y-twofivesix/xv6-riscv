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
  
  // 3. Tell Sulu about our window
  int fd = open("/dev/suluctl", O_WRONLY);
  if(fd < 0) {
    printf("rect_demo: failed to open suluctl\n");
    exit(1);
  }
  
  char cmd[64];
  // Format: "connect <shm_key> <width> <height>"
  cmd[0] = 'c'; cmd[1] = 'o'; cmd[2] = 'n'; cmd[3] = 'n'; 
  cmd[4] = 'e'; cmd[5] = 'c'; cmd[6] = 't'; cmd[7] = ' ';
  
  // Simple integer to string (shm_key)
  int n = 8;
  int k = shm_key;
  if(k == 0) { cmd[n++] = '0'; } else {
    char tmp[16]; int i = 0;
    while(k > 0) { tmp[i++] = '0' + (k % 10); k /= 10; }
    while(i > 0) cmd[n++] = tmp[--i];
  }
  cmd[n++] = ' ';
  
  // Width
  k = width;
  if(k == 0) { cmd[n++] = '0'; } else {
    char tmp[16]; int i = 0;
    while(k > 0) { tmp[i++] = '0' + (k % 10); k /= 10; }
    while(i > 0) cmd[n++] = tmp[--i];
  }
  cmd[n++] = ' ';
  
  // Height
  k = height;
  if(k == 0) { cmd[n++] = '0'; } else {
    char tmp[16]; int i = 0;
    while(k > 0) { tmp[i++] = '0' + (k % 10); k /= 10; }
    while(i > 0) cmd[n++] = tmp[--i];
  }
  
  write(fd, cmd, n);
  close(fd);
  
  printf("rect_demo: sent connect request\n");
  sleep(5);  // Give Sulu time to create window
  
  // 4. Get pixel buffer
  uint *pixels = sulu_pixels(shm);
  
  // 5. Animation loop
  int x = 50, y = 50;
  int dx = 2, dy = 2;
  int rect_w = 80, rect_h = 60;
  
  for(int frame = 0; frame < 200; frame++) {
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
    x += 2*dx;
    y += 2*dy;
    if(x <= 0 || x + rect_w >= width) dx = -dx;
    if(y <= 0 || y + rect_h >= height) dy = -dy;

    sleep(1);
  }
  
  // 6. Close window
  sleep(5);
  sulu_close(shm);
  
  printf("rect_demo: done\n");
  exit(0);
}
