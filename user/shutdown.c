#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc > 1 && strcmp(argv[1], "-r") == 0){
    reboot();
  } else {
    shutdown();
  }
  exit(0);
}
