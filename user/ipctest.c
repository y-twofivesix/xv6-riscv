#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int pid;
  char buf[64];
  int sender;

  printf("IPC Test Starting...\n");

  pid = fork();

  if(pid < 0){
    printf("fork failed\n");
    exit(1);
  }

  if(pid == 0){
    // Child
    // Child
    // Message from parent

    // Parent pid is not easily available in xv6 via syscall, but we can't send TO ourselves for this test to be interesting
    // actually getpid() is fine if we knew parent's pid, but we don't.
    // Wait, in xv6 fork(), we don't get parent pid.
    // However, the parent can send to us first.
    
    // Let's have parent send first.
    printf("Child: waiting for message...\n");
    if(recv(&sender, buf, sizeof(buf)) < 0){
       printf("Child: recv failed\n");
       exit(1);
    }
    
    // Sleep to avoid racing parent's printf
    sleep(10);

    printf("Child: received '%s' from PID %d\n", buf, sender);

    // Reply
    printf("Child: sending reply...\n");
    if(send(sender, "pong", 5) < 0){
      printf("Child: send failed\n");
      exit(1);
    }
    exit(0);
  } else {
    // Parent
    sleep(10); // Give child time to start and block on recv
    printf("Parent: sending 'ping' to child %d...\n", pid);
    if(send(pid, "ping", 5) < 0){
      printf("Parent: send failed\n");
      // Don't exit, try to wait
    }

    // Wait for reply
    printf("Parent: waiting for reply...\n");
    if(recv(&sender, buf, sizeof(buf)) < 0){
      printf("Parent: recv failed\n");
      exit(1);
    }
    printf("Parent: received '%s' from PID %d\n", buf, sender);
    
    wait(0);
    printf("IPC Test Finished\n");
  }

  exit(0);
}
