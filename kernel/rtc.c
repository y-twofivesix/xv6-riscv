#include "types.h"
#include "riscv.h"
#include "defs.h"

#include "param.h"
#include "memlayout.h"
#include "spinlock.h"

// Goldfish RTC Registers
#define RTC_TIME_LOW  0x00
#define RTC_TIME_HIGH 0x04

// Helper to access RTC registers
#define R(r) ((volatile uint32 *)(RTC + (r)))

void
rtcinit(void)
{
  // Nothing to initialize for Goldfish RTC
  // It starts running automatically
  printf("[RTC] Goldfish RTC initialized at 0x%x\n", RTC);
}

uint64
rtctime(void)
{
  uint64 t;
  
  // Read high and low 32-bit values
  // Since time advances, reading low then high might result in a rollover race.
  // Goldfish RTC might latch? The simple approach:
  uint32 high = *R(RTC_TIME_HIGH);
  uint32 low = *R(RTC_TIME_LOW);
  
  // Combine to 64-bit nanoseconds
  t = ((uint64)high << 32) | low;
  
  // Convert nanoseconds to seconds for Unix Timestamp
  return t / 1000000000;
}
