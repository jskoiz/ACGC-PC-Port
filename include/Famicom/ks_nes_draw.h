#ifndef KS_NES_DRAW_H
#define KS_NES_DRAW_H

#include "types.h"
#include "Famicom/ks_nes_common.h"

#ifdef __cplusplus
static inline bool ksNesNametableIsPacked(const u8* nametable) {
#if defined(TARGET_PC) && UINTPTR_MAX > UINT32_MAX
    const uintptr_t address = reinterpret_cast<uintptr_t>(nametable);
    return address <= static_cast<uintptr_t>(0xFFFFFFFFu) && (address & static_cast<uintptr_t>(0x80000000u)) == 0;
#else
    return ((s32)nametable) >= 0;
#endif
}

static inline u32 ksNesNametablePackedValue(const u8* nametable) {
#if defined(TARGET_PC) && UINTPTR_MAX > UINT32_MAX
    return static_cast<u32>(reinterpret_cast<uintptr_t>(nametable));
#else
    return (u32)nametable;
#endif
}
#endif

extern void ksNesDrawInit(ksNesCommonWorkObj* wp);
extern void ksNesDraw(ksNesCommonWorkObj* wp, ksNesStateObj* sp);
extern void ksNesDrawEnd();

extern u8 ksNesPaletteNormal[];

#endif
