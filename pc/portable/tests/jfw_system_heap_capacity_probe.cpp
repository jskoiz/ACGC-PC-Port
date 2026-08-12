#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "JSystem/JFramework/JFWSystem.h"
#include "JSystem/JKernel/JKRExpHeap.h"
#include "dolphin/os/OSArena.h"
#include "dolphin/os/OSAlloc.h"
#include "dolphin/os/OSMemory.h"
#include "dolphin/os/OSMutex.h"

namespace {

constexpr size_t kArenaOffset = 0x100;
constexpr size_t kArenaBytes = 0x20000;
constexpr size_t kGuardBytes = 64;

struct ArenaStorage {
    unsigned char before[kGuardBytes];
    alignas(32) unsigned char arena[kArenaBytes];
    unsigned char after[kGuardBytes];
};

ArenaStorage storage;
alignas(32) unsigned char physicalInfo[0x40];
void* arenaLo;
void* arenaHi;
bool invalidArena;

bool check(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "jfw_system_heap_capacity_probe: %s\n", message);
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

} // namespace

extern "C" void OSInit() {
    if (arenaLo == nullptr) {
        arenaLo = storage.arena + kArenaOffset;
        arenaHi = storage.arena + kArenaBytes;
        memset(physicalInfo, 0, sizeof(physicalInfo));
        *reinterpret_cast<u32*>(physicalInfo + 0x28) = static_cast<u32>(kArenaBytes);
    }
}

extern "C" void DVDInit() {
}

extern "C" void* OSGetArenaHi() {
    return invalidArena ? arenaLo : arenaHi;
}

extern "C" void* OSGetArenaLo() {
    return arenaLo;
}

extern "C" void OSSetArenaHi(void* value) {
    arenaHi = value;
}

extern "C" void OSSetArenaLo(void* value) {
    arenaLo = value;
}

extern "C" void* OSInitAlloc(void* start, void*, int maxHeaps) {
    const uintptr_t arraySize = static_cast<uintptr_t>(maxHeaps) * 24;
    const uintptr_t newStart = (reinterpret_cast<uintptr_t>(start) + arraySize + 0x1F) & ~uintptr_t(0x1F);
    return reinterpret_cast<void*>(newStart);
}

extern "C" void* OSPhysicalToCached(u32) {
    return physicalInfo;
}

extern "C" void OSInitMutex(OSMutex*) {
}

extern "C" void OSLockMutex(OSMutex*) {
}

extern "C" void OSUnlockMutex(OSMutex*) {
}

extern "C" void OSPanic(const char*, int, const char*, ...) {
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
    fprintf(stderr, "jfw_system_heap_capacity_probe requires an LP64 host\n");
    return 2;
#else
    static_assert(sizeof(uintptr_t) > sizeof(u32), "probe must exercise LP64 pointers");

    memset(storage.before, 0xA5, sizeof(storage.before));
    memset(storage.arena, 0xCD, sizeof(storage.arena));
    memset(storage.after, 0xA5, sizeof(storage.after));

    OSInit();
    const uintptr_t legacyArenaSpan = reinterpret_cast<uintptr_t>(arenaHi) - reinterpret_cast<uintptr_t>(arenaLo);
    const u32 legacyRequestedSize = static_cast<u32>(legacyArenaSpan - 0xD0);

    bool ok = true;
    invalidArena = true;
    JKRHeap::sRootHeap = nullptr;
    ok = check(JKRExpHeap::createRoot(1, false) == nullptr,
               "invalid arena setup did not fail closed") && ok;
    invalidArena = false;

    JKRHeap::sRootHeap = nullptr;
    JKRHeap::sSystemHeap = nullptr;
    JKRHeap::sCurrentHeap = nullptr;
    JFWSystem::rootHeap = nullptr;
    JFWSystem::systemHeap = nullptr;
    JFWSystem::CSetUpParam::sysHeapSize = legacyRequestedSize;

    JFWSystem::firstInit();

    ok = check(JFWSystem::rootHeap != nullptr, "root heap creation failed") && ok;
    ok = check(JFWSystem::systemHeap != nullptr,
               "system heap creation failed after capacity sizing") && ok;
    if (!ok) {
        return 1;
    }

    const s32 rootFreeBeforeAllocation = JFWSystem::rootHeap->getFreeSize();
    ok = check(rootFreeBeforeAllocation >= 0, "root heap reported an invalid free capacity") && ok;
    ok = check(static_cast<u32>(rootFreeBeforeAllocation) < legacyRequestedSize,
               "probe did not reproduce the oversized legacy arena request") && ok;
    ok = check(JFWSystem::systemHeap->check(), "system heap failed its initial consistency check") && ok;

    constexpr u32 kConsoleAllocation = 9464;
    void* allocation = JFWSystem::systemHeap->alloc(kConsoleAllocation, 32);
    ok = check(allocation != nullptr, "9464-byte system allocation failed") && ok;
    const uintptr_t allocationAddress = reinterpret_cast<uintptr_t>(allocation);
    ok = check(allocationAddress > UINT32_MAX, "system allocation was not above 4 GiB") && ok;
    ok = check((allocationAddress & 31u) == 0, "system allocation was not 32-byte aligned") && ok;
    ok = check(JFWSystem::systemHeap->getSize(allocation) >= static_cast<s32>(kConsoleAllocation),
               "system allocation size was truncated") && ok;
    ok = check(JFWSystem::systemHeap->check(), "system heap failed after allocation") && ok;
    ok = check(JFWSystem::rootHeap->check(), "root heap failed after child allocation") && ok;

    JFWSystem::systemHeap->free(allocation);
    ok = check(JFWSystem::systemHeap->check(), "system heap failed after freeing allocation") && ok;
    ok = check(JFWSystem::rootHeap->check(), "root heap failed after freeing child allocation") && ok;
    ok = check(canariesIntact(), "arena canary changed during root/system heap exercise") && ok;

    if (!ok) {
        return 1;
    }

    printf("jfw_system_heap_capacity_probe: root/system creation and 9464-byte LP64 allocation passed\n");
    return 0;
#endif
}
