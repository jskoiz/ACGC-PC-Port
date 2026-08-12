#define TARGET_PC 1

extern "C" {
#include "types.h"
#include "dolphin/os/OSThread.h"
#include "dolphin/card.h"
#include "dolphin/card/__card.h"
}

#include <cstddef>

static_assert(sizeof(s32) == 4, "CARD scalar s32 must be 32-bit");
static_assert(sizeof(u32) == 4, "CARD scalar u32 must be 32-bit");
static_assert(sizeof(CARDFileInfo) == 0x14, "CARDFileInfo wire layout changed");
static_assert(offsetof(CARDFileInfo, iBlock) == 0x10,
              "CARDFileInfo iBlock offset changed");
static_assert(sizeof(CARDDir) == 0x40, "CARDDir wire layout changed");
static_assert(sizeof(CARDStat) == 0x6C, "CARDStat wire layout changed");
static_assert(sizeof(CARDCallback) == sizeof(void (*)(s32, s32)),
              "CARDCallback must remain a native callback pointer");

using CardCheckFn = s32 (*)(s32 chan);
using CardCreateFn = s32 (*)(s32 chan, const char *fileName, u32 size,
                             CARDFileInfo *fileInfo);
using CardWriteAsyncFn = s32 (*)(CARDFileInfo *fileInfo, const void *buf, s32 length,
                                 s32 offset, CARDCallback callback);
using CardInternalReadFn = s32 (*)(s32 chan, u32 addr, s32 length, void *dst,
                                   CARDCallback callback);
using CardInternalStatusFn = s32 (*)(s32 chan, s32 fileNo, CARDDir *dirent,
                                     CARDCallback callback);

int acgc_card_public_abi_cpp_probe() {
    CardCheckFn check = CARDCheck;
    CardCreateFn create = CARDCreate;
    CardWriteAsyncFn writeAsync = CARDWriteAsync;
    CardInternalReadFn internalRead = __CARDRead;
    CardInternalReadFn internalWrite = __CARDWrite;
    CardInternalStatusFn statusEx = __CARDSetStatusExAsync;

    return check != nullptr && create != nullptr && writeAsync != nullptr &&
           internalRead != nullptr && internalWrite != nullptr && statusEx != nullptr;
}
