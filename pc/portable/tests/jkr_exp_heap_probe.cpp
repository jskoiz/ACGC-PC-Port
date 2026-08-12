#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "JSystem/JKernel/JKRExpHeap.h"
#include "JSystem/JUtility/JUTConsole.h"
#include "dolphin/os/OSMutex.h"

namespace {

constexpr size_t kGuardBytes = 64;
constexpr size_t kArenaBytes = 0x8000;

struct ArenaStorage {
    unsigned char before[kGuardBytes];
    alignas(32) unsigned char arena[kArenaBytes];
    unsigned char after[kGuardBytes];
};

ArenaStorage storage;

bool check(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "jkr_exp_heap_probe: %s\n", message);
    }
    return condition;
}

bool canariesIntact() {
    for (size_t i = 0; i < kGuardBytes; ++i) {
        if (storage.before[i] != 0xA5 || storage.after[i] != 0xA5) {
            return false;
        }
    }
    return true;
}

bool inManagedRange(const void* memory, size_t size, const unsigned char* begin, size_t length) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(memory);
    const uintptr_t rangeBegin = reinterpret_cast<uintptr_t>(begin);
    const uintptr_t rangeEnd = rangeBegin + length;
    return address >= rangeBegin && address <= rangeEnd && size <= rangeEnd - address;
}

#if UINTPTR_MAX > UINT32_MAX
u32 expectedBlockCheckCode(const void* block) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(block);
    const u32 low = static_cast<u32>(address);
    const u32 high = static_cast<u32>(address >> 32);
    return (low ^ high) * 3;
}
#endif

} // namespace

extern "C" void OSInitMutex(OSMutex*) {
}

extern "C" void OSLockMutex(OSMutex*) {
}

extern "C" void OSUnlockMutex(OSMutex*) {
}

extern "C" void OSPanic(const char*, int, const char*, ...) {
}

extern "C" void* OSGetArenaHi() {
    return nullptr;
}

extern "C" void* OSGetArenaLo() {
    return nullptr;
}

extern "C" void OSSetArenaHi(void*) {
}

extern "C" void OSSetArenaLo(void*) {
}

extern "C" void* OSInitAlloc(void*, void*, int) {
    return nullptr;
}

extern "C" void* OSPhysicalToCached(u32) {
    return nullptr;
}

void JKRHeap::destroy() {
}

void JUTReportConsole(const char*) {
}

void JUTReportConsole_f(const char*, ...) {
}

void JUTReportConsole_f_va(const char*, va_list) {
}

void JUTWarningConsole(const char*) {
}

void JUTWarningConsole_f(const char*, ...) {
}

void JUTWarningConsole_f_va(const char*, va_list) {
}

int main() {
#if UINTPTR_MAX <= UINT32_MAX
    fprintf(stderr, "jkr_exp_heap_probe requires an LP64 host\n");
    return 2;
#else
    static_assert(sizeof(uintptr_t) > sizeof(u32), "probe must exercise LP64 pointer contracts");
    static_assert(sizeof(JKRExpHeap::CMemBlock) == 24,
                  "LP64 CMemBlock must contain two native pointers");
    static_assert(sizeof(JKRHeap::TState::CheckCode) == sizeof(u32),
                  "TState checksum must preserve its legacy u32 ABI");

    memset(storage.before, 0xA5, sizeof(storage.before));
    memset(storage.arena, 0xCD, sizeof(storage.arena));
    memset(storage.after, 0xA5, sizeof(storage.after));

    JKRHeap::sRootHeap = nullptr;
    JKRHeap::sSystemHeap = nullptr;
    JKRHeap::sCurrentHeap = nullptr;

    const size_t heapHeaderBytes = (sizeof(JKRExpHeap) + 0x0F) & ~size_t(0x0F);
    const size_t managedBytes = (kArenaBytes - heapHeaderBytes) & ~size_t(0x0F);
    unsigned char* managed = storage.arena + heapHeaderBytes;
    JKRExpHeap* heap = ::new (static_cast<void*>(storage.arena))
        JKRExpHeap(managed, static_cast<u32>(managedBytes), nullptr, false);
    alignas(32) unsigned char rejectedArena[256];

    bool ok = true;
    ok = check(heap->check(), "fresh heap check failed") && ok;
    ok = check(heap->getTotalFreeSize() == managedBytes - sizeof(JKRExpHeap::CMemBlock),
               "fresh free-space size does not use the native block header") && ok;
    ok = check(JKRExpHeap::create(nullptr, static_cast<u32>(managedBytes), heap, false) == nullptr,
               "create(void*) accepted a null arena") && ok;
    ok = check(JKRExpHeap::create(0x100000, heap, false) == nullptr,
               "create(u32) did not reject an allocation failure") && ok;
    const size_t minimumCreateSize = heapHeaderBytes + sizeof(JKRExpHeap::CMemBlock);
    ok = check(JKRExpHeap::create(rejectedArena, static_cast<u32>(minimumCreateSize - 1), heap, false) == nullptr,
               "create(void*) accepted a heap smaller than its native header") && ok;
    ok = check(JKRExpHeap::create(static_cast<u32>(minimumCreateSize - 1), heap, false) == nullptr,
               "create(u32) accepted a heap smaller than its native header") && ok;
    ok = check(heap->check(), "heap check failed after create failure probes") && ok;

    const u32 headRequest = 0x43;
    const u32 headExpectedSize = 0x48;
    void* head = heap->alloc(headRequest, 32);
    ok = check(head != nullptr, "head allocation failed") && ok;
    const s32 headSize = heap->getSize(head);
    ok = check(headSize == headExpectedSize, "LP64 head allocation did not round to CMemBlock alignment") && ok;
    ok = check(inManagedRange(head, static_cast<size_t>(headSize), managed, managedBytes),
               "head allocation escaped managed range") && ok;
    ok = check((reinterpret_cast<uintptr_t>(head) & 31u) == 0, "head allocation is not 32-byte aligned") && ok;
    JKRExpHeap::CMemBlock* headBlock = JKRExpHeap::CMemBlock::getHeapBlock(head);
    ok = check(headBlock != nullptr && headBlock->getContent() == head,
               "getHeapBlock did not recover the LP64 block header") && ok;
    ok = check((reinterpret_cast<uintptr_t>(headBlock) % alignof(JKRExpHeap::CMemBlock)) == 0,
               "head block header is not naturally aligned") && ok;
    memset(head, 0x11, static_cast<size_t>(headSize));
    ok = check(heap->check(), "heap check failed after head allocation") && ok;
    {
        JKRHeap::TState state(heap, 0xFFFFFFFF, false);
        ok = check(state.getCheckCode() == expectedBlockCheckCode(headBlock),
                   "TState checksum did not fold both LP64 block-address halves") && ok;
    }

    const u32 tailRequest = 0x55;
    const u32 tailExpectedSize = 0x58;
    void* tail = heap->alloc(tailRequest, -32);
    ok = check(tail != nullptr, "tail allocation failed") && ok;
    const s32 tailSize = heap->getSize(tail);
    ok = check(tailSize >= static_cast<s32>(tailExpectedSize) &&
                   (static_cast<unsigned>(tailSize) % alignof(JKRExpHeap::CMemBlock)) == 0,
               "LP64 tail allocation did not preserve CMemBlock alignment") && ok;
    ok = check(inManagedRange(tail, static_cast<size_t>(tailSize), managed, managedBytes),
               "tail allocation escaped managed range") && ok;
    ok = check((reinterpret_cast<uintptr_t>(tail) & 31u) == 0, "tail allocation is not 32-byte aligned") && ok;
    JKRExpHeap::CMemBlock* tailBlock = JKRExpHeap::CMemBlock::getHeapBlock(tail);
    ok = check(tailBlock != nullptr && tailBlock->getContent() == tail,
               "getHeapBlock did not recover the tail block header") && ok;
    ok = check((reinterpret_cast<uintptr_t>(tailBlock) % alignof(JKRExpHeap::CMemBlock)) == 0,
               "tail block header is not naturally aligned") && ok;
    memset(tail, 0x22, static_cast<size_t>(tailSize));
    ok = check(heap->check(), "heap check failed after tail allocation") && ok;
    ok = check(canariesIntact(), "outer arena canary changed after head/tail allocations") && ok;

    const u32 tailBasicRequest = 0x37;
    const u32 tailBasicExpectedSize = 0x38;
    void* tailBasic = heap->alloc(tailBasicRequest, -4);
    ok = check(tailBasic != nullptr, "basic tail allocation failed") && ok;
    const s32 tailBasicSize = heap->getSize(tailBasic);
    ok = check(tailBasicSize == static_cast<s32>(tailBasicExpectedSize),
               "LP64 basic tail allocation did not round to CMemBlock alignment") && ok;
    ok = check((reinterpret_cast<uintptr_t>(tailBasic) % alignof(JKRExpHeap::CMemBlock)) == 0,
               "basic tail allocation is not naturally aligned") && ok;
    memset(tailBasic, 0x23, static_cast<size_t>(tailBasicSize));
    ok = check(heap->check(), "heap check failed after basic tail allocation") && ok;

    heap->free(head);
    ok = check(heap->check(), "heap check failed after freeing head allocation") && ok;

    const u32 reuseRequest = 0x2B;
    const u32 reuseExpectedSize = 0x30;
    void* reused = heap->alloc(reuseRequest, 4);
    ok = check(reused != nullptr, "head free block was not reusable") && ok;
    ok = check(heap->getSize(reused) == reuseExpectedSize,
               "LP64 reuse allocation did not round to CMemBlock alignment") && ok;
    ok = check((reinterpret_cast<uintptr_t>(reused) % alignof(JKRExpHeap::CMemBlock)) == 0,
               "reused head block is not naturally aligned") && ok;
    JKRExpHeap::CMemBlock* reusedBlock = JKRExpHeap::CMemBlock::getHeapBlock(reused);
    ok = check(reusedBlock != nullptr && (reinterpret_cast<uintptr_t>(reusedBlock) % alignof(JKRExpHeap::CMemBlock)) == 0,
               "reused block header is not naturally aligned") && ok;
    memset(reused, 0x33, static_cast<size_t>(heap->getSize(reused)));
    ok = check(heap->check(), "heap check failed after free/reuse") && ok;

    heap->free(tail);
    heap->free(tailBasic);
    const u32 resizeRequest = 0x73;
    const u32 resizeExpectedSize = 0x78;
    const s32 resized = heap->resize(reused, resizeRequest);
    ok = check(resized >= 0, "resize could not grow into adjacent free space") && ok;
    ok = check(heap->getSize(reused) == resizeExpectedSize,
               "LP64 resize did not round to CMemBlock alignment") && ok;
    ok = check(inManagedRange(reused, heap->getSize(reused), managed, managedBytes),
               "resized allocation escaped managed range") && ok;
    ok = check(heap->check(), "heap check failed after resize") && ok;
    ok = check(canariesIntact(), "outer arena canary changed after resize") && ok;

    heap->free(reused);
    ok = check(heap->check(), "heap check failed after final free") && ok;
    ok = check(heap->getTotalFreeSize() == managedBytes - sizeof(JKRExpHeap::CMemBlock),
               "final free-space size does not restore the native header accounting") && ok;
    ok = check(canariesIntact(), "outer arena canary changed after final free") && ok;

    if (!ok) {
        return 1;
    }

    printf("jkr_exp_heap_probe: LP64 allocator invariants passed (CMemBlock=%zu)\n",
           sizeof(JKRExpHeap::CMemBlock));
    return 0;
#endif
}
