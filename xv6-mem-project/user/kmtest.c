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
#define NCYCLES 5000000

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

  // Phase 3: many alloc/free cycles completely in kernel space.
  int elapsed = kmtest_perf(NCYCLES);
  printf("  %d alloc/free cycles (pure kernel) took %d ticks\n", NCYCLES, elapsed);

  // Cleanup remaining allocations.
  for (i = 1; i < NPTRS; i += 2) {
    if (ptrs[i])
      kmfree(ptrs[i]);
  }
  kmstat(&st);
  printstat("after cleanup", &st);
}

static void
run_reclaim_test(void)
{
  struct kmstat st_before, st_during, st_after;
  int pid;

  printf("\n=== Test Process Memory Auto-Reclamation ===\n");

  // Get initial stats
  kmstat(&st_before);
  printf("  [Before Fork] used_bytes=%d, used_blocks=%d\n", 
         (int)st_before.used_bytes, (int)st_before.used_blocks);

  pid = fork();
  if (pid < 0) {
    printf("  fork failed\n");
    exit(1);
  }

  if (pid == 0) {
    // Child process: allocate but do not free!
    printf("  [Child %d] Allocating memory blocks...\n", getpid());
    uint64 p1 = kmalloc(256);
    uint64 p2 = kmalloc(512);
    uint64 p3 = kmalloc(1024);
    
    if (p1 == 0 || p2 == 0 || p3 == 0) {
      printf("  [Child] Allocation failed!\n");
      exit(1);
    }
    
    kmstat(&st_during);
    printf("  [Child] used_bytes=%d, used_blocks=%d\n", 
           (int)st_during.used_bytes, (int)st_during.used_blocks);
    
    printf("  [Child] Exiting without freeing...\n");
    exit(0);
  } else {
    // Parent process
    wait(0);
    
    kmstat(&st_after);
    printf("  [After Child Exit] used_bytes=%d, used_blocks=%d\n", 
           (int)st_after.used_bytes, (int)st_after.used_blocks);
    
    if (st_after.used_bytes == st_before.used_bytes && 
        st_after.used_blocks == st_before.used_blocks) {
      printf("  [Success] Memory reclamation verified! All child memory reclaimed.\n");
    } else {
      printf("  [Fail] Memory leak detected! Child memory was not reclaimed.\n");
    }
  }
}

static void
run_safety_test(void)
{
  printf("\n=== Test Safety Checks (Expected Warning Messages below) ===\n");
  
  // 1. Invalid pointer (out of bounds)
  printf("  Test invalid pointer (out of bounds): kmfree((void*)0x10000)\n");
  kmfree(0x10000);
  
  // 2. Unaligned pointer
  uint64 p = kmalloc(32);
  if (p) {
    printf("  Test unaligned pointer: kmfree((void*)(p + 5))\n");
    kmfree(p + 5);
    
    // 3. Double free
    printf("  Test double free: kmfree((void*)p) followed by kmfree((void*)p)\n");
    kmfree(p);
    kmfree(p);
  }
  printf("  Safety checks test done.\n");
}

int
main(void)
{
  for (int p = KM_FIRST_FIT; p <= KM_WORST_FIT; p++)
    run_policy(p);

  run_reclaim_test();
  run_safety_test();

  printf("\nkmtest done\n");
  exit(0);
}
