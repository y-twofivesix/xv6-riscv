#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// Syscall wrapper for suluctl_poll
uint64
sys_suluctl_poll(void)
{
  return suluctl_poll();
}
