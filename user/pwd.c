#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
#include "kernel/param.h"

void
getpwd(char *)
{

  pwd();

}


int
main(int argc, char *argv[])
{

  char path[MAXPATH];
  getpwd(path);
  printf("%s\n", path);  
  exit(0);
}
