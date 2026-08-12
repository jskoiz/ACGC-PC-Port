#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "JSystem/JKernel/JKRAram.h"
#include "JSystem/JKernel/JKRArchive.h"
#include "JSystem/JKernel/JKRDvdAramRipper.h"
#include "JSystem/JKernel/JKRFileLoader.h"

static_assert(sizeof(s32) == 4, "JSystem s32 must remain four bytes");
static_assert(sizeof(u32) == 4, "JSystem u32 must remain four bytes");
static_assert(std::is_same<s32, int32_t>::value, "TARGET_PC s32 must be int32_t");
static_assert(std::is_same<u32, uint32_t>::value, "TARGET_PC u32 must be uint32_t");
static_assert(sizeof(uintptr_t) >= sizeof(void*), "uintptr_t must hold host pointers");
static_assert(std::is_same<ARNativeAddress, uintptr_t>::value,
              "TARGET_PC native ARAM transport addresses must use uintptr_t");

using FileLoaderResourceSize = s32 (JKRFileLoader::*)(const void*) const;
using ArchiveResourceSize = s32 (JKRArchive::*)(const void*) const;

static_assert(
    std::is_same<decltype(static_cast<FileLoaderResourceSize>(&JKRFileLoader::getResSize)),
                 FileLoaderResourceSize>::value,
    "JKRFileLoader getResSize must use the fixed-width result contract"
);
static_assert(
    std::is_same<decltype(static_cast<ArchiveResourceSize>(&JKRArchive::getResSize)),
                 ArchiveResourceSize>::value,
    "JKRArchive getResSize must retain the fixed-width override contract"
);
static_assert(
    std::is_same<decltype(&JKRAramStream::readFromAram), u32 (*)()>::value,
    "JKRAramStream readFromAram must return the fixed-width result"
);
static_assert(
    std::is_same<decltype(&JKRAramStream::writeToAram), s32 (*)(JKRAramStreamCommand*)>::value,
    "JKRAramStream writeToAram must return the fixed-width result"
);
static_assert(
    std::is_same<decltype(&ARStartDMA), void (*)(u32, ARNativeAddress, u32, u32)>::value,
    "PC ARStartDMA must carry a native MRAM address and fixed ARAM offset"
);
static_assert(
    std::is_same<decltype(&ARQPostRequestNative), void (*)(u32, ARNativeAddress, u32, u32)>::value,
    "PC native ARQ adapter must carry a native MRAM address and fixed ARAM offset"
);
static_assert(std::is_same<JKRADCommand::LoadCallback, void (*)(ARNativeAddress)>::value,
              "DVD ARAM callbacks must retain native command addresses on TARGET_PC");
static_assert(sizeof(((JKRAramStreamCommand*)nullptr)->mAddress) == sizeof(u32),
              "ARAM command addresses remain guest u32 values");
static_assert(sizeof(((JKRAramStreamCommand*)nullptr)->mSize) == sizeof(u32),
              "ARAM command sizes remain guest u32 values");
static_assert(sizeof(((JKRAramStreamCommand*)nullptr)->mOffset) == sizeof(u32),
              "ARAM command offsets remain guest u32 values");
static_assert(sizeof(((JKRAMCommand*)nullptr)->mSource) == sizeof(u32),
              "ARAM DMA command source layout remains a guest word");
static_assert(sizeof(((JKRAMCommand*)nullptr)->mDestination) == sizeof(u32),
              "ARAM DMA command destination layout remains a guest word");
static_assert(sizeof(((ARQRequest*)nullptr)->source) == sizeof(u32),
              "legacy ARQ source layout must remain a fixed-width word");
static_assert(sizeof(((ARQRequest*)nullptr)->dest) == sizeof(u32),
              "legacy ARQ destination layout must remain a fixed-width word");
static_assert(offsetof(JKRAramStreamCommand, mAddress) == 0x04,
              "ARAM command address offset changed");
static_assert(offsetof(JKRAramStreamCommand, mSize) == 0x08,
              "ARAM command size offset changed");
#if UINTPTR_MAX == UINT32_MAX
static_assert(offsetof(JKRAramStreamCommand, mOffset) == 0x14,
              "32-bit ARAM command offset changed");
#else
static_assert(offsetof(JKRAramStreamCommand, mOffset) == 0x10 + sizeof(void*),
              "64-bit ARAM command offset does not account for its native stream pointer");
#endif

int main() {
    return 0;
}
