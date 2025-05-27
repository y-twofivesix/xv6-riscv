//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-c -- kill line
//   control-d -- end of file
//   control-p -- print process list
//   control-u -- clear screen

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

#define BACKSPACE 0x100
#define ESCAPE    0x1B
#define LEFT      0x44
#define RIGHT     0x43
#define UP        0x41
#define DOWN      0x42
#define PARENTH_O 0x5B


#define GOTO_XY(x,y)  printf("\033[%d;%dH", (y), (x))
#define MOVE_UP(x)    printf("\033[%dA", (x)) // Move up X lines;
#define MOVE_DOWN(x)  printf("\033[%dB", (x)) // Move down X lines;
#define MOVE_RIGHT(x) printf("\033[%dC", (x)) // Move right X column;
#define MOVE_LEFT(x)  printf("\033[%dD", (x)) // Move left X column;

#define C(x)  ((x)-'@')  // Control-x

#define MAX_ESC_STAGES  2
static int escaped = 0;

//
// send one character to the uart.
// called by printf(), and to echo input characters,
// but not from write().
//
void
consputc(int c)
{
  if(c == BACKSPACE){
    // if the user typed backspace, overwrite with a space.
    uartputc_sync('\b'); uartputc_sync(' '); uartputc_sync('\b');
  } else {
    uartputc_sync(c);
  }
}

struct {
  struct spinlock lock;
  
  // input
#define INPUT_BUF_SIZE 128
  char buf[INPUT_BUF_SIZE];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
} cons;

//
// user write()s to the console go here.
//
int
consolewrite(int user_src, uint64 src, int n)
{
  int i;

  for(i = 0; i < n; i++){
    char c;
    if(either_copyin(&c, user_src, src+i, 1) == -1)
      break;
    uartputc(c);
  }
  return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dist indicates whether dst is a user
// or kernel address.
//
int
consoleread(int user_dst, uint64 dst, int n)
{
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons.lock);
  while(n > 0){
    // wait until interrupt handler has put some
    // input into cons.buffer.
    while(cons.r == cons.w){
      if(killed(myproc())){
        release(&cons.lock);
        return -1;
      }
      sleep(&cons.r, &cons.lock);
    }

    c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

    if(c == C('D')){  // end-of-file
      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        cons.r--;
      }
      break;
    }

    // copy the input byte to the user-space buffer.
    cbuf = c;
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    if(c == '\n'){
      // a whole line has arrived, return to
      // the user-level read().
      break;
    }
  }
  release(&cons.lock);

  return target - n;
}

//
// the console input interrupt handler.
// uartintr() calls this for input character.
// do erase/kill processing, append to cons.buf,
// wake up consoleread() if a whole line has arrived.
//

void handle_esc_key_down(int c) {
  switch (c)
  {
  case UP:
    MOVE_UP(1);
    break;
  case DOWN:
    MOVE_DOWN(1);
    break;
  case LEFT:
    MOVE_LEFT(1);
      break;
  case RIGHT:
    MOVE_RIGHT(1);
      break;
  default:
    break;
  }
  
      
}


void escape_stage_1(int c, int next) {
      switch (c)
      {
      case PARENTH_O:
        if ( next != -1 ) {
          handle_esc_key_down(next);
        }
        break;
      
      default:
        break;
      }
}


void
consoleintr(int c, int next)
{
  acquire(&cons.lock);

  if (escaped) {
    switch (escaped)
    {
    case (1):
      // handle escape sequence
      escape_stage_1(c, next);
      break;

    default:
      break;
    }

    escaped++;
    escaped %= MAX_ESC_STAGES + 1;

  
  } else {

  switch(c){
  case ESCAPE:
    escaped = 1;
    break;
  case C('P'):  // Print process list.
    procdump();
    break;
  case C('C'):  // Kill line.
    while(cons.e != cons.w &&
      cons.buf[(cons.e-1) % INPUT_BUF_SIZE] != '\n'){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  case C('U'):  // clear screen.
    printf("\033[2J");
    // for(int i=0;i<INPUT_BUF_SIZE;i++){
    //   uartputc_sync('\n');
    // }
    // consputc('\n');
    break;
  case C('H'): // Backspace
  case '\x7f': // Delete key
    if(cons.e != cons.w){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  case LEFT:
    consputc(c);
    break;
  case RIGHT:
    consputc(c);
    break;
  case UP:
    consputc(c);
    break;
  case DOWN:
    consputc(c);
    break;
  default:
    if(c != 0 && cons.e-cons.r < INPUT_BUF_SIZE){
      c = (c == '\r') ? '\n' : c;
      
      // echo back to the user.
      consputc(c);

      // store for consumption by consoleread().
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

      if(c == '\n' || c == C('D') || cons.e-cons.r == INPUT_BUF_SIZE ){
        // wake up consoleread() if a whole line (or end-of-file)
        // has arrived.
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
    break;
  }
  
  }

  release(&cons.lock);
}

void
consoleinit(void)
{
  initlock(&cons.lock, "cons");

  uartinit();

  // connect read and write system calls
  // to consoleread and consolewrite.
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;
}
