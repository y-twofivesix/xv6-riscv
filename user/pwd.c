#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"

void
getpwd(char (*)[MAXDEPTH][DIRSIZ], int *)
{

  pwd();

}


int
main(int argc, char *argv[])
{

  char paths[MAXDEPTH][DIRSIZ];
  int level;
  getpwd(&paths, &level);

  for (;level>=0;)
  {
    printf("%s%s", paths[level], level!=0?"/": "");
    level--;
  }
  printf("\n");
  exit(0);
}
