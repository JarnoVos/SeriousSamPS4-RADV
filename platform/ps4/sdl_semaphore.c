// SDL semaphore adapter using PS4's implemented kernel semaphore API.
#include <SDL.h>
#include <orbis/libkernel.h>
#include <orbis/_types/errors.h>
#include <limits.h>

struct SDL_semaphore {
    OrbisKernelSema handle;
    SDL_atomic_t count;
};

SDL_sem *SDL_CreateSemaphore(Uint32 initial)
{
    if (initial > INT_MAX) { SDL_SetError("Semaphore count exceeds INT_MAX"); return NULL; }
    SDL_sem *sem = SDL_calloc(1, sizeof(*sem));
    if (!sem) { SDL_OutOfMemory(); return NULL; }
    int rc = sceKernelCreateSema(&sem->handle, "SeriousSam SDL", ORBIS_KERNEL_SEMA_ATTR_TH_FIFO,
                                initial, INT_MAX, NULL);
    if (rc < 0) { SDL_SetError("sceKernelCreateSema: %x", rc); SDL_free(sem); return NULL; }
    SDL_AtomicSet(&sem->count, initial);
    return sem;
}

void SDL_DestroySemaphore(SDL_sem *sem)
{
    if (sem) { sceKernelDeleteSema(sem->handle); SDL_free(sem); }
}

int SDL_SemWaitTimeout(SDL_sem *sem, Uint32 milliseconds)
{
    if (!sem) return SDL_SetError("NULL semaphore");
    int rc;
    if (!milliseconds) rc = sceKernelPollSema(sem->handle, 1);
    else if (milliseconds == SDL_MUTEX_MAXWAIT) rc = sceKernelWaitSema(sem->handle, 1, NULL);
    else {
        // The PS4 timeout parameter uses 32-bit microseconds. Split long waits.
        Uint32 remaining = milliseconds;
        do {
            Uint32 slice = remaining > 4000000 ? 4000000 : remaining;
            OrbisKernelUseconds timeout = slice * 1000;
            rc = sceKernelWaitSema(sem->handle, 1, &timeout);
            remaining -= slice;
        } while ((uint32_t)rc == ORBIS_KERNEL_ERROR_ETIMEDOUT && remaining);
    }
    if (!rc) { SDL_AtomicAdd(&sem->count, -1); return 0; }
    if ((uint32_t)rc == ORBIS_KERNEL_ERROR_EBUSY || (uint32_t)rc == ORBIS_KERNEL_ERROR_ETIMEDOUT)
        return SDL_MUTEX_TIMEDOUT;
    return SDL_SetError("sceKernelWaitSema: %x", rc);
}
int SDL_SemWait(SDL_sem *sem) { return SDL_SemWaitTimeout(sem, SDL_MUTEX_MAXWAIT); }
int SDL_SemTryWait(SDL_sem *sem) { return SDL_SemWaitTimeout(sem, 0); }
Uint32 SDL_SemValue(SDL_sem *sem) { return sem ? SDL_AtomicGet(&sem->count) : 0; }
int SDL_SemPost(SDL_sem *sem)
{
    if (!sem) return SDL_SetError("NULL semaphore");
    SDL_AtomicAdd(&sem->count, 1);
    int rc = sceKernelSignalSema(sem->handle, 1);
    if (!rc) return 0;
    SDL_AtomicAdd(&sem->count, -1);
    return SDL_SetError("sceKernelSignalSema: %x", rc);
}
