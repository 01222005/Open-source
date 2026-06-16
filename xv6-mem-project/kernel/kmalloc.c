// Kernel-space dynamic memory allocator.
//
// Manages a fixed-size heap (carved out of the kernel's BSS segment)
// using an explicit doubly linked list of variable-sized blocks,
// ordered by address. Supports three placement policies
// (First-Fit / Best-Fit / Worst-Fit) selectable at run time, and
// coalesces adjacent free blocks on kmfree() to fight external
// fragmentation.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "kmalloc.h"

#define KM_HEAP_SIZE (512 * 1024) // 512KB managed heap
#define KM_ALIGN 16               // payload alignment

struct kmblock {
  uint64 size;          // usable payload size, not including this header
  int free;
  struct kmblock *next; // next block by address, 0 if last
  struct kmblock *prev; // previous block by address, 0 if first
};

#define KMHDR (sizeof(struct kmblock))

static struct {
  struct spinlock lock;
  struct kmblock *head;
  int policy;
} km;

// The managed heap lives statically inside the kernel image so that
// it is guaranteed to be one contiguous range of physical memory.
static char km_heap[KM_HEAP_SIZE] __attribute__((aligned(KM_ALIGN)));

void
kmallocinit(void)
{
  initlock(&km.lock, "kmalloc");

  struct kmblock *b = (struct kmblock *)km_heap;
  b->size = KM_HEAP_SIZE - KMHDR;
  b->free = 1;
  b->next = 0;
  b->prev = 0;

  km.head = b;
  km.policy = KM_FIRST_FIT;
}

static uint64
align_up(uint64 n, uint64 a)
{
  return (n + a - 1) & ~(a - 1);
}

// Search the free list according to the active placement policy.
// Caller must hold km.lock.
static struct kmblock *
find_block(uint64 nbytes)
{
  struct kmblock *b, *best = 0;

  for (b = km.head; b; b = b->next) {
    if (!b->free || b->size < nbytes)
      continue;

    switch (km.policy) {
    case KM_FIRST_FIT:
      return b;
    case KM_BEST_FIT:
      if (!best || b->size < best->size)
        best = b;
      break;
    case KM_WORST_FIT:
      if (!best || b->size > best->size)
        best = b;
      break;
    }
  }
  return best;
}

void *
kmalloc(uint64 nbytes)
{
  if (nbytes == 0)
    return 0;
  nbytes = align_up(nbytes, KM_ALIGN);

  acquire(&km.lock);

  struct kmblock *b = find_block(nbytes);
  if (!b) {
    release(&km.lock);
    return 0;
  }

  // Split off the unused tail of the block if it is big enough to
  // host another block header plus a minimal payload.
  if (b->size >= nbytes + KMHDR + KM_ALIGN) {
    struct kmblock *nb = (struct kmblock *)((char *)b + KMHDR + nbytes);
    nb->size = b->size - nbytes - KMHDR;
    nb->free = 1;
    nb->next = b->next;
    nb->prev = b;
    if (nb->next)
      nb->next->prev = nb;
    b->next = nb;
    b->size = nbytes;
  }

  b->free = 0;
  release(&km.lock);

  return (void *)((char *)b + KMHDR);
}

void
kmfree(void *ptr)
{
  if (!ptr)
    return;

  struct kmblock *b = (struct kmblock *)((char *)ptr - KMHDR);

  acquire(&km.lock);
  b->free = 1;

  // Coalesce with the following block first so that, if both
  // neighbours are free, b absorbs next and then prev absorbs b.
  if (b->next && b->next->free) {
    struct kmblock *n = b->next;
    b->size += KMHDR + n->size;
    b->next = n->next;
    if (b->next)
      b->next->prev = b;
  }
  if (b->prev && b->prev->free) {
    struct kmblock *p = b->prev;
    p->size += KMHDR + b->size;
    p->next = b->next;
    if (p->next)
      p->next->prev = p;
  }

  release(&km.lock);
}

int
kmsetpolicy(int policy)
{
  if (policy != KM_FIRST_FIT && policy != KM_BEST_FIT && policy != KM_WORST_FIT)
    return -1;

  acquire(&km.lock);
  km.policy = policy;
  release(&km.lock);
  return 0;
}

void
kmgetstat(struct kmstat *st)
{
  struct kmstat s;
  struct kmblock *b;

  memset(&s, 0, sizeof(s));
  s.heap_size = KM_HEAP_SIZE;

  acquire(&km.lock);
  s.policy = km.policy;
  for (b = km.head; b; b = b->next) {
    if (b->free) {
      s.free_bytes += b->size;
      s.free_blocks++;
      if (b->size > s.largest_free)
        s.largest_free = b->size;
    } else {
      s.used_bytes += b->size;
      s.used_blocks++;
    }
  }
  release(&km.lock);

  *st = s;
}
