#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

void
clear()
{
    printf("\033[2J");            // clear screen
    printf("\033[%d;%dH", 0, 0);  // goto 0,0
}

int
main(int argc, char *argv[])
{
  clear();
  exit(0);
}
