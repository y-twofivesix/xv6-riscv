//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
//

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
#define C(x)  ((x)-'@')  // Control-x

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
consolewrite(int user_src, uint64 src, int n, int minor)
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
consoleread(int user_dst, uint64 dst, int n, int minor)
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
void
consoleintr(int c)
{
  static int esc_state = 0;
  acquire(&cons.lock);

  switch(c){
  case C('P'):  // Print process list.
    procdump();
    break;
  default:
    if(c != 0 && cons.e-cons.r < INPUT_BUF_SIZE){
      c = (c == '\r') ? '\n' : c;

      // don't echo escape sequences
      int in_esc = 0;
      if(c == '\033') {
        esc_state = 1;
        in_esc = 1;
      } else if(esc_state == 1) {
        in_esc = 1;
        if(c == '[') esc_state = 2;
        else esc_state = 0;
      } else if(esc_state == 2) {
        in_esc = 1;
        // Stay in state 2 for digits/semicolons, otherwise end sequence
        if(!(c >= '0' && c <= '9') && c != ';') esc_state = 0;
      }

      // echo back to the user if not in escape
      // Don't echo backspace - let userspace handle it
      // Don't echo Tab - let userspace handle completion
      if(!in_esc && c != '\b' && c != 0x7f && c != '\t') {
        consputc(c);
      }

      // store for consumption by consoleread().
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

      // Wake up reader for every character (Raw-ish mode)
      cons.w = cons.e;
      wakeup(&cons.r);
    }
    break;
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

// Flush console input buffer - useful after crashes
void
console_flush(void)
{
  acquire(&cons.lock);
  cons.r = cons.w = cons.e = 0;
  release(&cons.lock);
}
