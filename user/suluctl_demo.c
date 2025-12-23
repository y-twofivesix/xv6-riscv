#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define SCREEN_W 1280
#define SCREEN_H 800

struct gpixel {
  int x;
  int y;
  int color;
};

int main(int argc, char *argv[])
{
  printf("suluctl_demo: starting...\n");
  
  // Suspend Sulu
  printf("1. Suspending Sulu...\n");
  sleep(10); // Give time to see the message
  
  int fd = open("/dev/suluctl", O_WRONLY);
  if(fd < 0) {
    printf("Failed to open /dev/suluctl\n");
    exit(1);
  }
  write(fd, "suspend", 7);
  close(fd);
  
  printf("2. Sulu suspended. Drawing graphics...\n");
  sleep(10);
  
  // Draw test pattern with gwin
  int gwin = open("/dev/gwin0", O_WRONLY);
  if(gwin < 0) {
    printf("Failed to open /dev/gwin0\n");
    exit(1);
  }
  
  struct gpixel pixels[128];
  int n = 0;
  
  // Draw colorful test pattern
  // Red square in top-left
  for(int y = 100; y < 200; y += 4) {
    for(int x = 100; x < 200; x += 4) {
      if(n < 128) {
        pixels[n].x = x;
        pixels[n].y = y;
        pixels[n].color = 0xFFFF0000; // Red
        n++;
      }
    }
  }
  write(gwin, pixels, n * sizeof(struct gpixel));
  n = 0;
  
  // Green square in top-right
  for(int y = 100; y < 200; y += 4) {
    for(int x = SCREEN_W - 200; x < SCREEN_W - 100; x += 4) {
      if(n < 128) {
        pixels[n].x = x;
        pixels[n].y = y;
        pixels[n].color = 0xFF00FF00; // Green
        n++;
      }
    }
  }
  write(gwin, pixels, n * sizeof(struct gpixel));
  n = 0;
  
  // Blue square in bottom-left
  for(int y = SCREEN_H - 200; y < SCREEN_H - 100; y += 4) {
    for(int x = 100; x < 200; x += 4) {
      if(n < 128) {
        pixels[n].x = x;
        pixels[n].y = y;
        pixels[n].color = 0xFF0000FF; // Blue
        n++;
      }
    }
  }
  write(gwin, pixels, n * sizeof(struct gpixel));
  n = 0;
  
  // Yellow square in bottom-right
  for(int y = SCREEN_H - 200; y < SCREEN_H - 100; y += 4) {
    for(int x = SCREEN_W - 200; x < SCREEN_W - 100; x += 4) {
      if(n < 128) {
        pixels[n].x = x;
        pixels[n].y = y;
        pixels[n].color = 0xFFFFFF00; // Yellow
        n++;
      }
    }
  }
  write(gwin, pixels, n * sizeof(struct gpixel));
  
  // White text in center
  char *msg = "SULUCTL TEST - Sulu Suspended";
  int msg_x = SCREEN_W / 2 - 150;
  int msg_y = SCREEN_H / 2;
  n = 0;
  for(int i = 0; msg[i] && n < 128; i++) {
    for(int dy = 0; dy < 16 && n < 128; dy++) {
      for(int dx = 0; dx < 8 && n < 128; dx++) {
        pixels[n].x = msg_x + i * 10 + dx;
        pixels[n].y = msg_y + dy;
        pixels[n].color = 0xFFFFFFFF; // White
        n++;
        if(n >= 128) {
          write(gwin, pixels, n * sizeof(struct gpixel));
          n = 0;
        }
      }
    }
  }
  if(n > 0) write(gwin, pixels, n * sizeof(struct gpixel));
  
  close(gwin);
  
  printf("3. Graphics drawn. Waiting 5 seconds...\n");
  sleep(50); // Show for 5 seconds
  
  // Resume Sulu
  printf("4. Resuming Sulu...\n");
  fd = open("/dev/suluctl", O_WRONLY);
  if(fd >= 0) {
    write(fd, "resume", 6);
    close(fd);
  }
  
  printf("5. Done! Sulu should be back.\n");
  exit(0);
}
