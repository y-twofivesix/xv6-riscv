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
  // mknod("/dev/gwin0", GWIN, 0);
  mknod("/dev/sulu", SULU_DEV, 0);
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
        
        // Start shell FIRST on UART console for faster serial access
        printf("[INIT] Starting serial console shell...\n");
        printf("\033[?25h");  // Enable cursor for serial console
        flush_console();  // Clear any stale input from previous crashes
        int shell_pid = fork();
        if(shell_pid < 0){
          printf("init: fork failed\n");
          exit(1);
        }
        if(shell_pid == 0){
          char *argv_sh[] = { "sh", 0 };
          exec("/bin/sh", argv_sh);
          printf("init: exec sh failed\n");
          exit(1);
        }
        
        // Then start Sulu window manager
        printf("[INIT] starting Sulu...\n");
        int sulu_pid = fork();
        if(sulu_pid < 0){
          printf("init: fork failed\n");
          exit(1);
        }
        if(sulu_pid == 0){
          char *argv_sulu[] = { "sulu", 0 };
          exec("/bin/sulu", argv_sulu);
          printf("init: exec sulu failed\n");
          exit(1);
        }
        
        // Wait for either Sulu or shell to exit
        for(;;){
          wpid = wait((int *) 0);
          if(wpid == sulu_pid){
            // Sulu exited - kill shell and restart both
            kill(shell_pid);
            wait(0);  // Reap the shell
            break;
          } else if(wpid == shell_pid){
            // Shell exited - kill Sulu and restart both
            kill(sulu_pid);
            wait(0);  // Reap Sulu
            break;
          } else if(wpid < 0){
            printf("init: wait returned an error\n");
            exit(1);
          }
          // else: parentless process exited, continue waiting
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
        
        // Wait for shell to exit
        for(;;){
          wpid = wait((int *) 0);
          if(wpid == pid){
            // Shell exited; restart it
            break;
          } else if(wpid < 0){
            printf("init: wait returned an error\n");
            exit(1);
          }
        }
    }
  }
}
