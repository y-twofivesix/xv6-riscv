#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int shmid;
  char *mem;
  int key = 1234;
  int pid;

  printf("SHM TEST: Starting...\n");

  shmid = shmget(key, 4096);
  if(shmid < 0){
      printf("SHM TEST: shmget failed\n");
      exit(1);
  }
  printf("SHM TEST: Got shmid %d\n", shmid);

  mem = (char*)shmat(shmid, (void*)0);
  if(mem == (char*)-1){
      printf("SHM TEST: shmat failed\n");
      exit(1);
  }
  printf("SHM TEST: Attached at %p\n", mem);

  mem[0] = 'A';
  printf("SHM TEST: Written 'A' to shared memory.\n");

  pid = fork();
  if(pid < 0){
      printf("SHM TEST: fork failed\n");
      exit(1);
  }

  if(pid == 0){
      // Child
      int child_shmid = shmget(key, 4096);
      char *child_mem = (char*)shmat(child_shmid, (void*)0);
      
      printf("SHM TEST Child: Attached at %p. Read: '%c'\n", child_mem, child_mem[0]);
      if(child_mem[0] == 'A'){
          printf("SHM TEST Child: SUCCESS! Memory is shared.\n");
          child_mem[0] = 'B'; // Write back
      } else {
          printf("SHM TEST Child: FAIL! Expected 'A'.\n");
      }
      exit(0);
  } else {
      // Parent
      wait(0);
      printf("SHM TEST Parent: Child finished. Read: '%c'\n", mem[0]);
      if(mem[0] == 'B'){
          printf("SHM TEST Parent: SUCCESS! Child write visible.\n");
      } else {
          printf("SHM TEST Parent: FAIL! Child write NOT visible.\n");
      }
  }

  exit(0);
}
