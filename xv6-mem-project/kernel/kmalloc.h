// Kernel-space dynamic memory allocator: shared definitions
// used by both the kernel implementation (kmalloc.c) and
// user-space test programs (via syscalls).

#define KM_FIRST_FIT 0
#define KM_BEST_FIT  1
#define KM_WORST_FIT 2

// Snapshot of the kernel heap allocator state, used for
// fragmentation analysis from user space.
struct kmstat {
  int policy;          // currently active placement policy
  uint64 heap_size;    // total size of the managed heap, in bytes
  uint64 used_bytes;   // bytes currently allocated to blocks in use
  uint64 free_bytes;   // bytes currently available (sum of free blocks)
  uint64 free_blocks;  // number of free blocks (holes)
  uint64 used_blocks;  // number of allocated blocks
  uint64 largest_free; // size of the largest free block
};
