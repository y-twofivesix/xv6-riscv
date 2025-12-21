#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void
start_window(int n)
{
  char path[20];
  // Simple way to build the path /bin/winX
  path[0] = '/'; path[1] = 'b'; path[2] = 'i'; path[3] = 'n'; path[4] = '/';
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
    
    printf("\nRio Window %d\nType Ctrl-Q to switch windows.\n", n);
    
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
  
  printf("Rio Window Manager starting...\n");
  
  for(i = 1; i <= 4; i++){
    start_window(i);
    sleep(2); // Small delay to avoid interleaved output during startup
  }
  
  // Wait for children
  while(1){
    wait(0);
    // If a shell exits, we could restart it here.
    // For now, just stay alive.
    sleep(100);
  }
  
  exit(0);
}
