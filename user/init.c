// init: The initial user-level program

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"
#include "kernel/fcntl.h"

char *argv[] = { "/bin/rio", 0 };

int
main(void)
{
  int pid, wpid;
  if(open("/dev/console", O_RDWR) < 0){
    mknod("/dev/console", CONSOLE, 0);
    open("/dev/console", O_RDWR);
  }
  mknod("/dev/winctl", WM, 0);
  mknod("/dev/win1", WM, 1);
  mknod("/dev/win2", WM, 2);
  mknod("/dev/win3", WM, 3);
  mknod("/dev/win4", WM, 4);
  mknod("/dev/gwin0", GWIN, 0);

  dup(0);  // stdout
  dup(0);  // stderr

  for(;;){
    

    // Clear screen first
    printf("\033[2J");            
    printf("\033[%d;%dH", 0, 0);

    // Run 'cat INFO' to display welcome message
    pid = fork();
    if(pid == 0){
        char *argv_cat[] = { "cat", "/bin/INFO", 0 };
        exec("/bin/cat", argv_cat);
        printf("init: exec cat failed\n");
        exit(1);
    }
    wait(0);

    // // Run 'shmtest' to verify shared memory
    // pid = fork();
    // if(pid == 0){
    //     char *argv_shm[] = { "shmtest", 0 };
    //     exec("/bin/shmtest", argv_shm);
    //     printf("init: exec shmtest failed\n");
    //     exit(1);
    // }
    // wait(0);

    printf("\n[INIT] Starting Window Manager...\n");

    pid = fork();
    if(pid < 0){
      printf("init: fork failed\n");
      exit(1);
    }
    if(pid == 0){
      exec("/bin/rio", argv);
      printf("init: exec rio failed\n");
      exit(1);
    }

    for(;;){
      // this call to wait() returns if the shell exits,
      // or if a parentless process exits.
      wpid = wait((int *) 0);
      if(wpid == pid){
        // the shell exited; restart it.
        break;
      } else if(wpid < 0){
        printf("init: wait returned an error\n");
        exit(1);
      } else {
        // it was a parentless process; do nothing.
      }
    }
  }
}
