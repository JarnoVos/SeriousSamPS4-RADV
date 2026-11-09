/* dlmalloc 2.8.6 (Doug Lea, MIT-0) as an mspace over memory this process maps itself.
 * OpenOrbis libc malloc maps 2.5 GiB of system flexible memory on the first call.
 * That mapping fails on retail and the allocator then reads address 0x38. */

#define ONLY_MSPACES 1
#define MSPACES 1
#define USE_LOCKS 1
#define HAVE_MMAP 0
#define HAVE_MREMAP 0
#define HAVE_MORECORE 0
#define FOOTERS 1
#define INSECURE 0
#define USE_DEV_RANDOM 0
#define NO_MALLINFO 1
#define NO_MALLOC_STATS 1
#define LACKS_SCHED_H 0
#define malloc_getpagesize ((size_t)16384U)
#define DEFAULT_GRANULARITY ((size_t)64U * (size_t)1024U)

void ps4_heap_error(void *mspace, void *chunk, int corruption);
#define CORRUPTION_ERROR_ACTION(m) ps4_heap_error((m), 0, 1)
#define USAGE_ERROR_ACTION(m, p) ps4_heap_error((m), (p), 0)

#include "third_party/dlmalloc/malloc.c"
