#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"


#define BACKSPACE 0x100
#define ESCAPE    0x1B
#define SHIFT     0x00
#define LEFT      0x44
#define RIGHT     0x43
#define UP        0x41
#define DOWN      0x42
#define PARENTH_O 0x5B

#define CLEAR_SCREEN()  printf("\033[2J")
#define GOTO_XY(x,y)    printf("\033[%d;%dH", (y), (x))
#define MOVE_UP(x)      printf("\033[%dA", (x)) // Move up X lines;
#define MOVE_DOWN(x)    printf("\033[%dB", (x)) // Move down X lines;
#define MOVE_RIGHT(x)   printf("\033[%dC", (x)) // Move right X column;
#define MOVE_LEFT(x)    printf("\033[%dD", (x)) // Move left X column;

//
// wrapper so that it's OK if main() does not call exit().
//
void
start()
{
  extern int main();
  main();
  exit(0);
}

char*
strcpy(char *s, const char *t)
{
  char *os;

  os = s;
  while((*s++ = *t++) != 0)
    ;
  return os;
}

int
strcmp(const char *p, const char *q)
{
  while(*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}

void strcat(char *s1, const char *s2) {
    int i = 0;

    // Move to the end of str1
    while (s1[i] != '\0')
        i++;

    // Copy characters from str2 to str1
    int j = 0;
    while (s2[j] != '\0') {
        s1[i] = s2[j];  
        i++;
        j++;
    }
    // Null-terminate the concatenated string
    s1[i] = '\0';
}


int strtok(const char * s, char * substr, char delim, int off) {
  int i = 0, j = 0, n = 0;

  // Move to the nth instance of
  // delim
  while (s[i] != '\0') 
  {

    if (n >= off)
      break;

    if (s[i] == delim )
      n++;

    i++;
    
  }


  // move to ith instance of delim
  j = i+1;
  while ( s[j] != delim && s[j] != '\0') 
  {
      j++;
  }

  
  memcpy( substr, &s[i], j );
  substr[j] = '\0';
  return strlen(substr); 
}


uint
strlen(const char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}

void*
memset(void *dst, int c, uint n)
{
  char *cdst = (char *) dst;
  int i;
  for(i = 0; i < n; i++){
    cdst[i] = c;
  }
  return dst;
}

char*
strchr(const char *s, char c)
{
  for(; *s; s++)
    if(*s == c)
      return (char*)s;
  return 0;
}

// Simple gets: just read until newline, no editing
char*
gets(char *buf, int max)
{
  int i, cc;
  char c;

  for(i=0; i+1 < max; ){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    buf[i++] = c;
    if(c == '\n' || c == '\r')
      break;
  }
  buf[i] = '\0';
  return buf;
}

// readline: interactive line editing with cursor movement
char*
readline(char *buf, int max)
{
  int n = 0;     // total length
  int pos = 0;   // cursor position
  int cc;
  char c;
  int esc_state = 0;

  while(1){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;

    if(esc_state == 0){
      if(c == '\033'){
        esc_state = 1;
        continue;
      }
      if(c == '\b' || c == 0x7f){
        if(pos > 0){
          // Shift buffer left
          for(int j = pos - 1; j < n - 1; j++)
            buf[j] = buf[j+1];
          pos--;
          n--;
          buf[n] = '\0';
          
          // Visual backspace: move left, print space, move left
          printf("\b \b");
          
          // If deleted from middle, redraw the tail
          if(n > pos){
            printf("%s ", &buf[pos]);
            for(int j = 0; j <= n - pos; j++) printf("\b");
          }
        }
        continue;
      }
      if(c == '\n' || c == '\r'){
        buf[n] = '\n';
        n++;
        break;
      }
      // Insert character
      if(n + 1 < max){
        // Shift buffer right
        for(int j = n; j > pos; j--)
          buf[j] = buf[j-1];
        buf[pos] = c;
        pos++;
        n++;
        buf[n] = '\0';
        
        // If inserted in middle, redraw the tail
        if(pos < n){
          printf("%s", &buf[pos]);
          for(int j = 0; j < n - pos; j++) printf("\033[D");
        }
      }
    } else if(esc_state == 1){
      if(c == '[') esc_state = 2;
      else esc_state = 0;
    } else if(esc_state == 2){
      if(c == 'C'){ // Forward (Right Arrow)
        if(pos < n){
          pos++;
          printf("\033[C");
        }
      } else if(c == 'D'){ // Backward (Left Arrow)
        if(pos > 0){
          pos--;
          printf("\033[D");
        }
      }
      esc_state = 0;
    }
  }
  buf[n] = '\0';
  return buf;
}


int
atoi(const char *s)
{
  int n;

  n = 0;
  while('0' <= *s && *s <= '9')
    n = n*10 + *s++ - '0';
  return n;
}

void*
memmove(void *vdst, const void *vsrc, int n)
{
  char *dst;
  const char *src;

  dst = vdst;
  src = vsrc;
  if (src > dst) {
    while(n-- > 0)
      *dst++ = *src++;
  } else {
    dst += n;
    src += n;
    while(n-- > 0)
      *--dst = *--src;
  }
  return vdst;
}

int
memcmp(const void *s1, const void *s2, uint n)
{
  const char *p1 = s1, *p2 = s2;
  while (n-- > 0) {
    if (*p1 != *p2) {
      return *p1 - *p2;
    }
    p1++;
    p2++;
  }
  return 0;
}

void *
memcpy(void *dst, const void *src, uint n)
{
  return memmove(dst, src, n);
}

void
usleep(uint64 usec)
{
  uint64 start = rdtime();
  uint64 cycles = usec * 10; // 10MHz clock = 10 cycles per microsecond
  
  // If sleep is >= 1 tick (10ms), use kernel sleep to yield CPU
  if(usec >= 10000) {
    sleep(usec / 10000);
  }
  
  // Busy wait for the remaining time to ensure microsecond precision
  while(rdtime() - start < cycles)
    ;
}
