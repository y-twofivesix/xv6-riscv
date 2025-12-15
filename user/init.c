// init: The initial user-level program

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"
#include "kernel/fcntl.h"

char *argv[] = { "/bin/sh", 0 };

int
main(void)
{
  int pid, wpid;
  if(open("/bin/console", O_RDWR) < 0){
    mknod("/bin/console", CONSOLE, 0);
    open("/bin/console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

  for(;;){

    printf("\033[2J");            // clear screen
    printf("\033[%d;%dH", 0, 0);  // goto 0,0
    printf("\n\033[37m");
    printf("\t    █████╗ ██████╗ ████████╗          ██████╗ \n");
    printf("\t██╗██╔══██╗██╔══██╗╚══██╔══╝██╗   ██╗██╔═████╗\n");
    printf("\t██║███████║██████╔╝   ██║   ██║   ██║██║██╔██║\n");
    printf("\t██║██╔══██║██╔══██╗   ██║   ╚██╗ ██╔╝████╔╝██║\n");
    printf("\t██║██║  ██║██║  ██║   ██║    ╚████╔╝ ╚██████╔╝\n");
    printf("\t╚═╝╚═╝  ╚═╝╚═╝  ╚═╝   ╚═╝     ╚═══╝   ╚═════╝ \n");

    printf("\n\tiART research-kernel v0.0.0. AART. 2025.");
    printf("\n\tThis is a fork of the xv6 operating system,");
    printf("\n\ta re-implementation of Dennis Ritchie's and");
    printf("\n\tKen Thompson's Unix Version 6 (v6).");
    printf("\033[m"); 
    printf("\n"); 


    pid = fork();
    if(pid < 0){
      printf("init: fork failed\n");
      exit(1);
    }
    if(pid == 0){
      exec("/bin/sh", argv);
      printf("init: exec sh failed\n");
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
