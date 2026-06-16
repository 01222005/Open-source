#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "kmalloc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    if (addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// Allocate 'size' bytes from the kernel heap allocator and return
// the kernel address as an opaque handle. The handle must only be
// passed back to kmfree(); user code never dereferences it.
uint64
sys_kmalloc(void)
{
  int size;

  argint(0, &size);
  if (size < 0)
    return 0;
  return (uint64)kmalloc((uint64)size);
}

// Free a handle previously returned by kmalloc().
uint64
sys_kmfree(void)
{
  uint64 handle;

  argaddr(0, &handle);
  kmfree((void *)handle);
  return 0;
}

// Switch the kernel heap allocator's placement policy
// (KM_FIRST_FIT / KM_BEST_FIT / KM_WORST_FIT).
uint64
sys_kmpolicy(void)
{
  int policy;

  argint(0, &policy);
  return (uint64)kmsetpolicy(policy);
}

// Copy a snapshot of the kernel heap allocator's state into the
// user-supplied struct kmstat, for fragmentation analysis.
uint64
sys_kmstat(void)
{
  uint64 addr;
  struct kmstat st;

  argaddr(0, &addr);
  kmgetstat(&st);
  if (copyout(myproc()->pagetable, addr, (char *)&st, sizeof(st)) < 0)
    return -1;
  return 0;
}
