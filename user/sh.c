// Shell.

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "kernel/param.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS 10

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

int fork1(void);  // Fork but panics on failure.
void panic(char*);
struct cmd *parsecmd(char*);
void runcmd(struct cmd*) __attribute__((noreturn));

int search_path(char * path, const char* argv0) 
{
  int i = 0;
  const char * paths = "./\n/bin/";
  while (strtok(paths, path, '\n', i++) != 0)
  {
    strcat(path, argv0);
    struct stat s;
    if (stat(path, &s) > 0 || s.type == T_FILE)
    {
      return 0;
    }
  }

  return -1;
}

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    exit(1);

  switch(cmd->type){
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      exit(1);

    
    char path[50];
    if ( search_path(path, ecmd->argv[0]) < 0 )
    {
      printf("executable '%s' is not in PATH\n",  ecmd->argv[0]);
      break;
    }

    // Shebang (#!) support
    int fd = open(path, O_RDONLY);
    if(fd >= 0) {
        char head[128];
        int n = read(fd, head, sizeof(head)-1);
        close(fd);
        if(n >= 2 && head[0] == '#' && head[1] == '!') {
            head[n] = 0;
            char *p = head + 2;
            while(*p == ' ') p++; // Skip spaces after #!
            char *interp = p;
            while(*p && *p != ' ' && *p != '\n' && *p != '\r') p++;
            *p = 0; // Terminate interpreter path

            // Shift argv to make room for interpreter
            char *new_argv[MAXARGS];
            new_argv[0] = interp;
            new_argv[1] = path;
            for(int i = 1; ecmd->argv[i]; i++) {
                if(i + 1 < MAXARGS) new_argv[i+1] = ecmd->argv[i];
                else break;
            }
            new_argv[ecmd->argv[0] ? (1 + (n > 2 ? 1 : 0)) : 1] = 0; // Simplified for demo
            
            // Re-build argv properly
            int j = 0;
            new_argv[j++] = interp;
            new_argv[j++] = path; // The script itself is the first arg to the interpreter
            for(int k = 1; ecmd->argv[k] && j < MAXARGS-1; k++) {
                new_argv[j++] = ecmd->argv[k];
            }
            new_argv[j] = 0;

            exec(interp, new_argv);
            fprintf(2, "exec interpreter %s failed\n", interp);
            exit(1);
        }
    }

    exec(path, ecmd->argv);
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    close(rcmd->fd);
    if(open(rcmd->file, rcmd->mode) < 0){
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    if(fork1() == 0)
      runcmd(lcmd->left);
    wait(0);
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    if(pipe(p) < 0)
      panic("pipe");
    if(fork1() == 0){
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if(fork1() == 0){
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait(0);
    wait(0);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    if(fork1() == 0)
      runcmd(bcmd->cmd);
    break;
  }
  exit(0);
}

void
getpwd(char *)
{
  pwd();
}

// Command history
#define HIST_SIZE 16
#define HIST_LEN  100
static char history[HIST_SIZE][HIST_LEN];
static int hist_count = 0;  // Total commands stored
static int hist_start = 0;  // Oldest entry index (circular)

void
add_history(char *cmd)
{
  // Strip trailing newline for storage
  int len = strlen(cmd);
  if(len > 0 && cmd[len-1] == '\n') len--;
  if(len == 0) return;  // Don't store empty commands
  
  // Don't store duplicates of the last command
  if(hist_count > 0){
    int last = (hist_start + hist_count - 1) % HIST_SIZE;
    if(strlen(history[last]) == len && memcmp(history[last], cmd, len) == 0)
      return;
  }
  
  // Add to circular buffer
  int idx = (hist_start + hist_count) % HIST_SIZE;
  if(hist_count < HIST_SIZE){
    hist_count++;
  } else {
    hist_start = (hist_start + 1) % HIST_SIZE;
  }
  
  if(len >= HIST_LEN) len = HIST_LEN - 1;
  memmove(history[idx], cmd, len);
  history[idx][len] = '\0';
}

// Helper for tab completion

void attempt_completion(char *buf, int *n, int *pos, int max_buf)
{
    // 1. Find start of current word
    int start = *pos;
    while(start > 0 && buf[start-1] != ' ') start--;
    int len = *pos - start;
    if(len == 0 || len >= DIRSIZ) return;
    
    // Check if first token (command)
    int is_first = 1;
    for(int i=0; i<start; i++) {
        if(buf[i] != ' ') { is_first = 0; break; }
    }

    char prefix[DIRSIZ+1];
    memmove(prefix, &buf[start], len);
    prefix[len] = 0;
    
    // 2. Open Directory (Try . then /bin)
    int fd = open(".", O_RDONLY);
    if(fd < 0) return;
    
    struct dirent de;
    int matches = 0;
    char match_name[DIRSIZ+1];
    
    // Search Loop Helper
    for(int pass = 0; pass < 2; pass++) {
        if(pass == 1) {
            if(!is_first) break; // Only search /bin for commands
            if(matches > 0) break; // Found local match, ignore /bin
            close(fd);
            fd = open("/bin", O_RDONLY);
            if(fd < 0) break;
        }
        
        while(read(fd, &de, sizeof(de)) == sizeof(de)){
            if(de.inum == 0) continue;
            if(strncmp(de.name, prefix, len) == 0){
                matches++;
                memmove(match_name, de.name, DIRSIZ);
                match_name[DIRSIZ] = 0;
            }
        }
        
        // Reset read head or close
        if(pass == 0 && matches > 0) break; 
    }
    close(fd);
    
    // 3. Complete if single match
    if(matches == 1){
        // Find end of name (handle non-null termination in dirent)
        int dlen = 0;
        while(dlen < DIRSIZ && match_name[dlen]) dlen++;
        match_name[dlen] = 0;
        
        char *suffix = match_name + len;
        int slen = strlen(suffix);
        
        if(*n + slen >= max_buf - 1) return;
        
        // Shift tail
        if(*pos < *n){
             for(int i = *n; i >= *pos; i--) buf[i+slen] = buf[i];
        } else {
             buf[*n + slen] = 0;
        }
        
        memmove(&buf[*pos], suffix, slen);
        // int old_pos = *pos;
        *pos += slen;
        *n += slen;
        
        // Print Suffix
        printf("%s", suffix);
        
        // Redraw tail if needed
        if(*n > *pos){
             printf("%s", &buf[*pos]);
             // Move cursor back
             for(int k=0; k < (*n - *pos); k++) printf("\b");
        }
    }
}

int
getcmd(char *buf, int nbuf)
{
  char path[MAXPATH];
  getpwd(path);

  char promptstr[MAXPATH];
  sprintf(promptstr,"\033[37m< %s >\033[m ", path);
  
  // History navigation state
  int hist_pos = hist_count;  // Start past the end (new command)
  char saved_line[100];       // Save current line when navigating
  saved_line[0] = '\0';
  int saved = 0;
  
  memset(buf, 0, nbuf);
  
  while(1){
    write(2, promptstr, strlen(promptstr));
    
    // Use simple character-by-character input for history support
    int n = 0, pos = 0;
    int esc_state = 0;
    
    while(1){
      char c;
      if(read(0, &c, 1) < 1){
        if(n == 0) return -1;  // EOF
        break;
      }
      
      if(esc_state == 0){
        if(c == '\033'){
          esc_state = 1;
          continue;
        }
        if(c == '\t'){
            attempt_completion(buf, &n, &pos, nbuf);
            continue;
        }
        if(c == '\b' || c == 0x7f){
          if(pos > 0){
            for(int j = pos - 1; j < n - 1; j++) buf[j] = buf[j+1];
            pos--; n--; buf[n] = '\0';
            printf("\b \b");
            if(n > pos){
              printf("%s ", &buf[pos]);
              for(int j = 0; j <= n - pos; j++) printf("\b");
            }
          }
          continue;
        }
        if(c == '\n' || c == '\r'){
          buf[n] = '\n'; n++;
          break;
        }
        if(n + 1 < nbuf){
          for(int j = n; j > pos; j--) buf[j] = buf[j-1];
          buf[pos] = c; pos++; n++; buf[n] = '\0';
          if(pos < n){
            printf("%s", &buf[pos]);
            for(int j = 0; j < n - pos; j++) printf("\033[D");
          }
        }
      } else if(esc_state == 1){
        if(c == '[') esc_state = 2;
        else esc_state = 0;
      } else if(esc_state == 2){
        if(c == 'A'){  // Up arrow - older history
          if(hist_pos > 0){
            if(hist_pos == hist_count && !saved){
              // Save current line before navigating
              memmove(saved_line, buf, n);
              saved_line[n] = '\0';
              saved = 1;
            }
            hist_pos--;
            // Clear current line: move to start, overwrite with spaces, move back
            for(int i = 0; i < pos; i++) printf("\b");
            for(int i = 0; i < n; i++) printf(" ");
            for(int i = 0; i < n; i++) printf("\b");
            // Show history entry
            int idx = (hist_start + hist_pos) % HIST_SIZE;
            strcpy(buf, history[idx]);
            n = pos = strlen(buf);
            printf("%s", buf);
          }
        } else if(c == 'B'){  // Down arrow - newer history
          if(hist_pos < hist_count){
            hist_pos++;
            // Clear current line
            for(int i = 0; i < pos; i++) printf("\b");
            for(int i = 0; i < n; i++) printf(" ");
            for(int i = 0; i < n; i++) printf("\b");
            // Show new content
            if(hist_pos == hist_count){
              strcpy(buf, saved_line);
            } else {
              int idx = (hist_start + hist_pos) % HIST_SIZE;
              strcpy(buf, history[idx]);
            }
            n = pos = strlen(buf);
            printf("%s", buf);
          }
        } else if(c == 'C'){  // Right arrow
          if(pos < n){ pos++; printf("\033[C"); }
        } else if(c == 'D'){  // Left arrow
          if(pos > 0){ pos--; printf("\033[D"); }
        }
        esc_state = 0;
      }
    }
    
    buf[n] = '\0';
    if(buf[0] == 0) return -1;
    return 0;
  }
}

int
main(void)
{
  static char buf[100];
  int fd;

  // Ensure that three file descriptors are open.
  while((fd = open("/dev/console", O_RDWR)) >= 0){
    if(fd >= 3){
      close(fd);
      break;
    }
  }

  // Read and run input commands.
  while(getcmd(buf, sizeof(buf)) >= 0)
  {
    add_history(buf);  // Store command in history
    
    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' ')
    {
      // Chdir must be called by the parent, not the child.
      buf[strlen(buf)-1] = 0;  // chop \n
      if(chdir(buf+3) < 0)
        fprintf(2, "cannot cd %s\n", buf+3);
      continue;
    }
    if(fork1() == 0)
      runcmd(parsecmd(buf));
    wait(0);
    
  }
  exit(0);
}

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

//PAGEBREAK!
// Constructors

struct cmd*
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd*)cmd;
}

struct cmd*
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd*)cmd;
}

struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd*)cmd;
}
//PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q)
    *q = s;
  ret = *s;
  switch(*s){
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if(*s == '>'){
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq)
    *eq = s;

  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parseexec(char**, char*);
struct cmd *nulterminate(struct cmd*);

struct cmd*
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if(s != es){
    fprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd*
parseline(char **ps, char *es)
{
  struct cmd *cmd;
  cmd = parsepipe(ps, es);
  while(peek(ps, es, "&")){
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if(peek(ps, es, ";")){
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd*
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if(peek(ps, es, "|")){
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while(peek(ps, es, "<>")){
    tok = gettoken(ps, es, 0, 0);
    if(gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch(tok){
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE|O_TRUNC, 1);
      break;
    case '+':  // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd*
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if(!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if(!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if(peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while(!peek(ps, es, "|)&;")){
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if(argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd*
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    return 0;

  switch(cmd->type){
  case EXEC:
    ecmd = (struct execcmd*)cmd;
    for(i=0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
