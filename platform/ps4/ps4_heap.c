/* Retail malloc. Linked with --wrap so the first static constructor does not
 * enter OpenOrbis libc's heap. One dlmalloc mspace on flexible memory, with
 * direct memory as the last resort. */

#include <stddef.h>
#include <stdint.h>

typedef void *mspace;
mspace create_mspace_with_base(void *base, size_t capacity, int locked);
void *mspace_malloc(mspace msp, size_t bytes);
void mspace_free(mspace msp, void *mem);
void *mspace_calloc(mspace msp, size_t n_elements, size_t elem_size);
void *mspace_realloc(mspace msp, void *mem, size_t newsize);
void *mspace_memalign(mspace msp, size_t alignment, size_t bytes);

int32_t sceKernelDebugOutText(int32_t channel, const char *text);
int32_t sceSystemServiceLoadExec(const char *path, char *const argv[]);
int32_t sceKernelAvailableFlexibleMemorySize(size_t *size);
int32_t sceKernelReserveVirtualRange(void **addr, size_t len, int32_t flags, size_t alignment);
int32_t sceKernelMapNamedFlexibleMemory(void **addr, size_t len, int32_t prot, int32_t flags, const char *name);
int32_t sceKernelMapNamedSystemFlexibleMemory(void **addr, size_t len, int32_t prot, int32_t flags, const char *name);
size_t sceKernelGetDirectMemorySize(void);
int32_t sceKernelAllocateDirectMemory(int64_t start, int64_t end, size_t len, size_t align, int32_t type, int64_t *physOut);
int32_t sceKernelMapDirectMemory(void **addr, size_t len, int32_t prot, int32_t flags, int64_t phys, size_t align);

#define SS_MB ((size_t)1024 * 1024)
#define SS_PAGE ((size_t)16 * 1024)
#define SS_PROT_CPU_RW 0x3
#define SS_MAP_FIXED 0x10
#define SS_ENOMEM 12

static mspace g_heap;
static uintptr_t g_base;
static size_t g_size;
static int g_creating;

void ps4_heap_error(void *space, void *chunk, int corruption) {
    (void)space;
    (void)chunk;
    sceKernelDebugOutText(0, corruption ? "[ss] heap corruption\n" : "[ss] heap bad free\n");
    __builtin_trap();
}

static int owns(const void *ptr) {
    uintptr_t address = (uintptr_t)ptr;
    return address >= g_base && address < g_base + g_size;
}

static mspace finish(const char *ready, void *base, size_t size) {
    mspace space = create_mspace_with_base(base, size, 1);
    void *probe;
    if (!space)
        return 0;
    probe = mspace_malloc(space, 32);
    if (!probe)
        return 0;
    mspace_free(space, probe);
    g_base = (uintptr_t)base;
    g_size = size;
    sceKernelDebugOutText(0, ready);
    return space;
}

static void *reserve_high(size_t size) {
    static const uintptr_t hints[] = {0x1000000000ull, 0x800000000ull, 0x600000000ull};
    size_t i;
    for (i = 0; i < sizeof(hints) / sizeof(hints[0]); i++) {
        void *address = (void *)hints[i];
        if (sceKernelReserveVirtualRange(&address, size, 0, SS_PAGE) != 0)
            continue;
        if ((uintptr_t)address >= 0x400000000ull)
            return address;
    }
    return 0;
}

static mspace map_flexible(int system_pool, size_t size, const char *ready) {
    void *guard = reserve_high(size + SS_PAGE);
    void *base;
    int32_t ret;
    if (!guard)
        return 0;
    base = (char *)guard + SS_PAGE;
    ret = system_pool
              ? sceKernelMapNamedSystemFlexibleMemory(&base, size, SS_PROT_CPU_RW, SS_MAP_FIXED, "ss heap")
              : sceKernelMapNamedFlexibleMemory(&base, size, SS_PROT_CPU_RW, SS_MAP_FIXED, "ss heap");
    if (ret != 0)
        return 0;
    return finish(ready, base, size);
}

static mspace map_direct(size_t size) {
    int64_t phys = 0;
    void *base = (void *)0x1800000000ull;
    if (sceKernelAllocateDirectMemory(0, (int64_t)sceKernelGetDirectMemorySize(), size, 2 * SS_MB, 0, &phys) != 0)
        return 0;
    if (sceKernelMapDirectMemory(&base, size, SS_PROT_CPU_RW, 0, phys, 2 * SS_MB) != 0)
        return 0;
    if ((uintptr_t)base < 0x400000000ull)
        return 0;
    return finish("[ss] heap: direct memory\n", base, size);
}

static mspace create_heap(void) {
    static const size_t system_sizes[] = {512 * SS_MB, 384 * SS_MB, 256 * SS_MB, 192 * SS_MB, 128 * SS_MB};
    size_t available = 0;
    size_t i;
    size_t size;
    mspace space;
    for (i = 0; i < sizeof(system_sizes) / sizeof(system_sizes[0]); i++) {
        space = map_flexible(1, system_sizes[i], "[ss] heap: system flexible\n");
        if (space)
            return space;
    }
    if (sceKernelAvailableFlexibleMemorySize(&available) == 0 && available > 96 * SS_MB) {
        size = (available - 64 * SS_MB) & ~(SS_PAGE - 1);
        if (size > 512 * SS_MB)
            size = 512 * SS_MB;
        for (; size >= 32 * SS_MB; size = (size / 2) & ~(SS_PAGE - 1)) {
            space = map_flexible(0, size, "[ss] heap: flexible memory\n");
            if (space)
                return space;
        }
    }
    space = map_direct(256 * SS_MB);
    if (space)
        return space;
    return map_direct(128 * SS_MB);
}

static mspace heap(void) {
    if (!g_heap && !g_creating) {
        g_creating = 1;
        g_heap = create_heap();
        g_creating = 0;
        if (!g_heap)
            sceKernelDebugOutText(0, "[ss] heap: failed\n");
    }
    return g_heap;
}

__attribute__((constructor(101))) static void ss_heap_before_ctors(void) {
    (void)heap();
}

void *__wrap_malloc(size_t size) {
    mspace space = heap();
    if (!space)
        return 0;
    if (size == 0)
        size = 1;
    return mspace_malloc(space, size);
}

void __wrap_free(void *ptr) {
    if (!ptr || !owns(ptr))
        return;
    mspace_free(heap(), ptr);
}

void *__wrap_calloc(size_t nelem, size_t size) {
    mspace space = heap();
    if (!space)
        return 0;
    return mspace_calloc(space, nelem, size);
}

void *__wrap_realloc(void *ptr, size_t size) {
    mspace space;
    if (ptr && !owns(ptr))
        return 0;
    space = heap();
    if (!space)
        return 0;
    if (ptr && size == 0) {
        mspace_free(space, ptr);
        return 0;
    }
    return mspace_realloc(space, ptr, size);
}

void *__wrap_memalign(size_t alignment, size_t size) {
    mspace space = heap();
    if (!space)
        return 0;
    if (alignment < sizeof(void *))
        alignment = sizeof(void *);
    return mspace_memalign(space, alignment, size);
}

void *__wrap___memalign(size_t alignment, size_t size) {
    return __wrap_memalign(alignment, size);
}

void *__wrap_aligned_alloc(size_t alignment, size_t size) {
    return __wrap_memalign(alignment, size);
}

int __wrap_posix_memalign(void **out, size_t alignment, size_t size) {
    void *ptr = __wrap_memalign(alignment, size);
    if (!ptr)
        return SS_ENOMEM;
    *out = ptr;
    return 0;
}

void ss_ps4_load_modules(void);

/* libc _exit enters libkernel with syscall 0 and the retail kernel raises SIGSYS. */
void _exit(int status) {
    sceKernelDebugOutText(0, status ? "[ss] exit failure\n" : "[ss] exit\n");
    ss_ps4_load_modules();
    sceSystemServiceLoadExec("exit", 0);
    for (;;)
        ;
}

void _Exit(int status) {
    _exit(status);
}
