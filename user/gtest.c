#include "kernel/types.h"
#include "user/user.h"

struct gpixel {
  int x;
  int y;
  int color;
};

static struct gpixel square[50 * 50];

int
main(int argc, char *argv[])
{
  int fd;

  fd = open("/dev/gwin0", 2); // O_RDWR
  if(fd < 0){
    printf("gtest: could not open /dev/gwin0\n");
    exit(1);
  }

  // Draw a 50x50 red square at (100, 100) using a single bulk write
  int i = 0;
  for(int dy = 0; dy < 50; dy++){
    for(int dx = 0; dx < 50; dx++){
      square[i].x = 100 + dx;
      square[i].y = 100 + dy;
      square[i].color = 0xFF0000FF;
      i++;
    }
  }

  if(write(fd, square, sizeof(square)) != sizeof(square)){
    printf("gtest: write failed\n");
    exit(1);
  }

  printf("gtest: square drawn in bulk. check display.\n");
  
  close(fd);
  exit(0);
}
