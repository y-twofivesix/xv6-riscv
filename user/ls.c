#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

void
ls(char *path, int verbose)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, O_RDONLY)) < 0){
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_DEVICE:
  case T_FILE:
    printf("%s %d %d %d\n", fmtname(path), st.type, st.ino, (int) st.size);
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf("ls: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de))
    {
      if(de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0){
        printf("ls: cannot stat %s\n", buf);
        continue;
      }
      
      if (st.type == T_DIR)
        printf("\033[1;35m");

      if (verbose)
        printf(" %s\033[m\t%d\t%d\t%d\n", fmtname(buf), st.type, st.ino, (int) st.size);
      else
        printf("%s\033[m ", fmtname(buf));
      printf("\033[m");
    }
    printf("\n");
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  int i;
  int v = 0;
  int flags[argc];

  if(argc < 2)
  {
    ls(".", v);
    exit(0);
  }

  for(int j=1; j < argc; j++)
  {
      if(!strcmp(argv[j], "-v"))
      {
        v = 1;
        flags[j] = 1;
      }
  }

  for(i=1; i<argc; i++)
  {
    if (!flags[i])
        ls(argv[i], v);
  }
    
  exit(0);

}
