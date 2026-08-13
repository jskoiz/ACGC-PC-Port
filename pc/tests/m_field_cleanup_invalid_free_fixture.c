/*
 * Focused contract fixture for the field display-list cleanup path.
 *
 * The guest allocator owns the exact pointer it returns.  Rounding that
 * pointer through a guest-width u32 on an LP64 host discards the allocation's
 * high bits, so the cleanup path cannot recover the allocator header.  This
 * fixture keeps the reproduction independent of the ISO, renderer, and full
 * game lifecycle while checking the native-width ownership invariant.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "libc64/__osMalloc.h"

static int panic_count;

void osCreateMesgQueue(OSMessageQueue* queue, OSMessage message, int flags) {
    (void)queue;
    (void)message;
    (void)flags;
}

int osRecvMesg(OSMessageQueue* queue, OSMessage* message, int flags) {
    (void)queue;
    (void)message;
    (void)flags;
    return 0;
}

int osSendMesg(OSMessageQueue* queue, OSMessage message, int flags) {
    (void)queue;
    (void)message;
    (void)flags;
    return 0;
}

u32 __OSBusClock = 162000000;

OSId osGetThreadId(OSThread* thread) {
    (void)thread;
    return 1;
}

OSTime osGetTime(void) {
    return 1;
}

void OSReport(const char* format, ...) {
    (void)format;
}

void OSPanic(const char* file, int line, const char* message, ...) {
    (void)file;
    (void)line;
    (void)message;
    panic_count++;
}

static int fail(const char* message) {
    fprintf(stderr, "m_field_cleanup_invalid_free_fixture: %s\n", message);
    return 1;
}

static int test_allocator_owned_aligned_pointer(void) {
    static unsigned char arena_storage[8192] __attribute__((aligned(32)));
    OSArena arena = { 0 };
    void* allocation;

    panic_count = 0;
    __osMallocInit(&arena, arena_storage, sizeof(arena_storage));
    allocation = __osMallocAlign(&arena, 64, 16);
    if (allocation == NULL) {
        return fail("allocator could not produce the aligned fixture block");
    }
    if (((uintptr_t)allocation & (uintptr_t)(16 - 1)) != 0) {
        return fail("allocator returned an unaligned fixture block");
    }

    __osFree(&arena, allocation);
    if (panic_count != 0) {
        return fail("allocator rejected its exact aligned return value");
    }
    __osMallocCleanup(&arena);
    return 0;
}

int main(void) {
#if UINTPTR_MAX <= UINT32_MAX
    puts("m_field_cleanup_invalid_free_fixture: SKIP (host pointer width is not LP64)");
    return 77;
#else
    if (test_allocator_owned_aligned_pointer() != 0) {
        return 1;
    }

    void* allocation = NULL;
    if (posix_memalign(&allocation, 16, 64) != 0 || allocation == NULL) {
        return fail("posix_memalign failed");
    }

    const uintptr_t native_address = (uintptr_t)allocation;
    if (native_address <= UINT32_MAX) {
        free(allocation);
        puts("m_field_cleanup_invalid_free_fixture: SKIP (allocation landed in u32 range)");
        return 77;
    }

    const uintptr_t legacy_address =
        ((uintptr_t)(uint32_t)native_address + (uintptr_t)(16 - 1)) & ~(uintptr_t)(16 - 1);
    if (legacy_address == native_address) {
        free(allocation);
        return fail("legacy u32 conversion unexpectedly preserved the LP64 address");
    }

    const uintptr_t native_aligned_address =
        (native_address + (uintptr_t)(16 - 1)) & ~(uintptr_t)(16 - 1);
    if (native_aligned_address != native_address) {
        free(allocation);
        return fail("native-width alignment changed the allocator-owned address");
    }

    printf("m_field_cleanup_invalid_free_fixture: legacy=0x%" PRIxPTR
           " native=0x%" PRIxPTR " preserved=1\n",
           legacy_address, native_aligned_address);
    free(allocation);
    return 0;
#endif
}
