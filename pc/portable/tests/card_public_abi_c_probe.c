#define TARGET_PC 1

#include "types.h"
#include "dolphin/os/OSThread.h"
#include "dolphin/card.h"
#include "dolphin/card/__card.h"

#include <stddef.h>

_Static_assert(sizeof(s32) == 4, "CARD scalar s32 must be 32-bit");
_Static_assert(sizeof(u32) == 4, "CARD scalar u32 must be 32-bit");
_Static_assert(sizeof(CARDFileInfo) == 0x14, "CARDFileInfo wire layout changed");
_Static_assert(offsetof(CARDFileInfo, iBlock) == 0x10,
               "CARDFileInfo iBlock offset changed");
_Static_assert(sizeof(CARDDir) == 0x40, "CARDDir wire layout changed");
_Static_assert(sizeof(CARDStat) == 0x6C, "CARDStat wire layout changed");
_Static_assert(sizeof(CARDCallback) == sizeof(void (*)(s32, s32)),
               "CARDCallback must remain a native callback pointer");

typedef s32 (*CardCheckFn)(s32 chan);
typedef s32 (*CardCreateFn)(s32 chan, const char *fileName, u32 size,
                            CARDFileInfo *fileInfo);
typedef s32 (*CardFastDeleteFn)(s32 chan, s32 fileNo);
typedef s32 (*CardFormatFn)(s32 chan);
typedef s32 (*CardEncodingFn)(s32 chan, u16 *encode);
typedef s32 (*CardXferredFn)(s32 chan);
typedef BOOL (*CardProbeFn)(s32 chan);
typedef s32 (*CardStatusFn)(s32 chan, s32 fileNo, CARDStat *stat);
typedef s32 (*CardReadFn)(CARDFileInfo *fileInfo, void *buf, s32 length, s32 offset);
typedef s32 (*CardWriteFn)(CARDFileInfo *fileInfo, const void *buf, s32 length,
                           s32 offset);
typedef s32 (*CardWriteAsyncFn)(CARDFileInfo *fileInfo, const void *buf, s32 length,
                                s32 offset, CARDCallback callback);

typedef s32 (*CardInternalReadFn)(s32 chan, u32 addr, s32 length, void *dst,
                                  CARDCallback callback);
typedef s32 (*CardInternalRawReadFn)(s32 chan, void *buf, s32 length, s32 offset,
                                     CARDCallback callback);
typedef s32 (*CardInternalStatusFn)(s32 chan, s32 fileNo, CARDDir *dirent,
                                    CARDCallback callback);

int acgc_card_public_abi_c_probe(void) {
    CardCheckFn check = CARDCheck;
    CardCreateFn create = CARDCreate;
    CardFastDeleteFn fastDelete = CARDFastDelete;
    CardFormatFn format = CARDFormat;
    CardEncodingFn encoding = CARDGetEncoding;
    CardEncodingFn memSize = CARDGetMemSize;
    CardXferredFn xferred = CARDGetXferredBytes;
    CardProbeFn probe = CARDProbe;
    CardStatusFn status = CARDSetStatus;
    CardReadFn read = CARDRead;
    CardWriteFn write = CARDWrite;
    CardWriteAsyncFn writeAsync = CARDWriteAsync;
    CardInternalReadFn internalRead = __CARDRead;
    CardInternalReadFn internalWrite = __CARDWrite;
    CardInternalRawReadFn rawRead = __CARDRawReadAsync;
    CardInternalStatusFn statusEx = __CARDSetStatusExAsync;

    return check != NULL && create != NULL && fastDelete != NULL && format != NULL &&
           encoding != NULL && memSize != NULL && xferred != NULL && probe != NULL &&
           status != NULL && read != NULL && write != NULL && writeAsync != NULL &&
           internalRead != NULL && internalWrite != NULL && rawRead != NULL &&
           statusEx != NULL;
}
