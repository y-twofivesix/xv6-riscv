#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

//
// the riscv Platform Level Interrupt Controller (PLIC).
//

void
plicinit(void)
{
  // set desired IRQ priorities non-zero (otherwise disabled).
  *(uint32*)(PLIC + UART0_IRQ*4) = 1;
  for(int i = 0; i < 8; i++)
    *(uint32*)(PLIC + (VIRTIO0_IRQ + i)*4) = 1;
}

void
plicinithart(void)
{
  int hart = cpuid();
  
  // set enable bits for this hart's S-mode
  uint32 mask = (1 << UART0_IRQ);
  for(int i = 0; i < 8; i++)
    mask |= (1 << (VIRTIO0_IRQ + i));
  
  *(uint32*)PLIC_SENABLE(hart) = mask;

  // set this hart's S-mode priority threshold to 0.
  *(uint32*)PLIC_SPRIORITY(hart) = 0;
}

// ask the PLIC what interrupt we should serve.
int
plic_claim(void)
{
  int hart = cpuid();
  int irq = *(uint32*)PLIC_SCLAIM(hart);
  return irq;
}

// tell the PLIC we've served this IRQ.
void
plic_complete(int irq)
{
  int hart = cpuid();
  *(uint32*)PLIC_SCLAIM(hart) = irq;
}
