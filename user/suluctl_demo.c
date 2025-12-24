#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int main(int argc, char *argv[])
{
  printf("suluctl_demo: starting... (graphics removed)\n");
  
  // Suspend
  printf("1. Suspending Sulu...\n");
  sleep(100);
  int fd = open("/dev/suluctl", O_WRONLY);
  if(fd >= 0) {
      write(fd, "suspend", 7);
      close(fd);
  }
  
  printf("2. Sulu suspended. Waiting 5 seconds...\n");
  sleep(500);
  
  // Resume
  printf("3. Resuming Sulu...\n");
  fd = open("/dev/suluctl", O_WRONLY);
  if(fd >= 0) {
      write(fd, "resume", 6);
      close(fd);
  }
  
  printf("4. Done.\n");
  exit(0);
}
