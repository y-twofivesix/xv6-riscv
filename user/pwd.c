#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

void
getpwd(char *buf)
{

    if (pwd() < 0)
    {
      printf("failed to get pwd.\n");
      return;
    }
    printf("%s\n", buf);
}

int
main(int argc, char *argv[])
{
  char buf[DIRSIZ];
  getpwd(buf);
  exit(0);
}
