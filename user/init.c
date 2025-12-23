// init: The initial user-level program

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"
#include "kernel/fcntl.h"

char *argv[] = { "/bin/sulu", 0 };

int
main(void)
{
  int pid, wpid;
  if(open("/dev/console", O_RDWR) < 0){
    mknod("/dev/console", CONSOLE, 0);
    open("/dev/console", O_RDWR);
  }
  mknod("/dev/gwin0", GWIN, 0);
  mknod("/dev/suluctl", SULUCTL, 0);
  // Note: /dev/input is created conditionally based on GUI availability

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

    // Detect GUI availability - check if VirtIO GPU device exists
    // A simple way is to see if the framebuffer shared memory was actually initialized
    int gui_available = 0;
    int shmid = shmget(SHM_FB, 0);
    if(shmid >= 0){
        void *test_fb = (void*)shmat(shmid, 0);
        // Check if it mapped to a real address (not 0 which would be invalid)
        if(test_fb != (void*)-1 && (uint64)test_fb >= 0x1000){
            gui_available = 1;
        }
    }
    
    if(gui_available){
        printf("[INIT] GUI mode detected\n");
        // Create GUI-specific devices
        mknod("/dev/input", INPUT, 0);
        printf("\n[INIT] starting Sulu...\n");
        
        pid = fork();
        if(pid < 0){
          printf("init: fork failed\n");
          exit(1);
        }
        if(pid == 0){
          char *argv_sulu[] = { "sulu", 0 };
          exec("/bin/sulu", argv_sulu);
          printf("init: exec sulu failed\n");
          exit(1);
        }
        
        // ALSO start a shell on the UART console for serial access
        printf("[INIT] Starting serial console shell...\n");
        int shell_pid = fork();
        if(shell_pid < 0){
          printf("init: fork failed\n");
          exit(1);
        }
        if(shell_pid == 0){
          // Enable cursor for serial console
          printf("\033[?25h");
          char *argv_sh[] = { "sh", 0 };
          exec("/bin/sh", argv_sh);
          printf("init: exec sh failed\n");
          exit(1);
        }
    } else {
        printf("\n[INIT] Headless mode - starting shell...\n");
        
        pid = fork();
        if(pid < 0){
          printf("init: fork failed\n");
          exit(1);
        }
        if(pid == 0){
          char *argv_sh[] = { "sh", 0 };
          exec("/bin/sh", argv_sh);
          printf("init: exec sh failed\n");
          exit(1);
        }
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
