// User-space test program for the kernel heap allocator
// (kmalloc/kmfree/kmpolicy/kmstat syscalls, see kernel/kmalloc.c).
//
// For each placement policy (First-Fit, Best-Fit, Worst-Fit) this
// program:
//   1. allocates a batch of variable-sized blocks,
//   2. frees every other block to create holes (fragmentation),
//   3. reports allocator statistics (free/used bytes, block counts,
//      largest free hole) so the policies can be compared, and
//   4. runs many alloc/free cycles and reports the elapsed ticks
//      as a coarse allocation/deallocation latency measurement.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define NPTRS 64
#define NCYCLES 2000

static unsigned int rngstate;

static unsigned int
rnd(void)
{
  rngstate = rngstate * 1103515245 + 12345;
  return (rngstate >> 16) & 0x7fff;
}

static char *polname[] = {"First-Fit", "Best-Fit", "Worst-Fit"};

static void
printstat(char *label, struct kmstat *st)
{
  printf("  [%s] heap=%d used=%d free=%d used_blk=%d free_blk=%d largest_free=%d\n",
         label, (int)st->heap_size, (int)st->used_bytes, (int)st->free_bytes,
         (int)st->used_blocks, (int)st->free_blocks, (int)st->largest_free);
}

static void
run_policy(int policy)
{
  uint64 ptrs[NPTRS];
  struct kmstat st;
  int i, t0, t1;

  printf("\n=== Policy: %s ===\n", polname[policy]);
  kmpolicy(policy);
  rngstate = 12345; // same seed for every policy: identical workload

  kmstat(&st);
  printstat("initial", &st);

  // Phase 1: allocate NPTRS blocks of varying sizes.
  for (i = 0; i < NPTRS; i++) {
    int sz = 32 + (rnd() % 512);
    ptrs[i] = kmalloc(sz);
    if (ptrs[i] == 0)
      printf("  alloc %d (size %d) failed\n", i, sz);
  }
  kmstat(&st);
  printstat("after batch alloc", &st);

  // Phase 2: free every other block to fragment the heap.
  for (i = 0; i < NPTRS; i += 2) {
    kmfree(ptrs[i]);
    ptrs[i] = 0;
  }
  kmstat(&st);
  printstat("after freeing every other block", &st);

  // Phase 3: many alloc/free cycles, measure elapsed ticks.
  t0 = uptime();
  for (i = 0; i < NCYCLES; i++) {
    uint64 p = kmalloc(64 + (rnd() % 256));
    if (p)
      kmfree(p);
  }
  t1 = uptime();
  printf("  %d alloc/free cycles took %d ticks\n", NCYCLES, t1 - t0);

  // Cleanup remaining allocations.
  for (i = 1; i < NPTRS; i += 2) {
    if (ptrs[i])
      kmfree(ptrs[i]);
  }
  kmstat(&st);
  printstat("after cleanup", &st);
}

int
main(void)
{
  for (int p = KM_FIRST_FIT; p <= KM_WORST_FIT; p++)
    run_policy(p);

  printf("\nkmtest done\n");
  exit(0);
}
