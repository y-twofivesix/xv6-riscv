#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void
start_window(int n)
{
  char path[20];
  // Simple way to build the path /dev/winX
  path[0] = '/'; path[1] = 'd'; path[2] = 'e'; path[3] = 'v'; path[4] = '/';
  path[5] = 'w'; path[6] = 'i'; path[7] = 'n'; path[8] = '0' + n; path[9] = 0;
  
  int pid = fork();
  if(pid == 0){
    close(0);
    close(1);
    close(2);
    
    if(open(path, O_RDWR) < 0){
      fprintf(2, "rio: could not open %s\n", path);
      exit(1);
    }
    dup(0); // stdout
    dup(0); // stderr
    char *argv[] = { "/bin/sh", 0 };
    exec("/bin/sh", argv);
    fprintf(2, "rio: exec sh failed\n");
    exit(1);
  }
}

int
main(int argc, char *argv[])
{
  int i;
  char buf[2];
    
  // Start the first window
  start_window(1);
  
  // Open control channel
  int fd = open("/dev/winctl", O_RDWR);
  if(fd < 0){
    fprintf(2, "rio: could not open /dev/winctl\n");
    exit(1);
  }

  // Force initial draw of Window 1 title
  char init_switch[1] = {'1'};
  write(fd, init_switch, 1);

  // Windows in use tracker (simple approach)
  int used[5] = {0, 1, 0, 0, 0}; // Win 1 is used at start

  // Listen for 'New Window' events (e.g. from right-click)
  while(1){
    if(read(fd, buf, 1) == 1){
      if(buf[0] == 'N'){
        // Find next free window
        int next = -1;
        for(i = 1; i <= 4; i++){
          if(!used[i]){
            next = i;
            break;
          }
        }
        
        if(next != -1){
          // Mark as used in kernel
          char cmd[2] = {'U', '0' + next};
          write(fd, cmd, 2);
          
          // Spawn it
          start_window(next);
          used[next] = 1;
          
          // Switch to it
          char sw[1] = {'0' + next};
          write(fd, sw, 1);
        } else {
          printf("Rio: Max windows (4) reached.\n");
        }
      }
    }
  }
  
  exit(0);
}
