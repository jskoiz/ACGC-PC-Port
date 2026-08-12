#ifdef __cplusplus
#define _LANGUAGE_C_PLUS_PLUS 1
#else
#define _LANGUAGE_C 1
#endif

#include "types.h"
#include "pc_types.h"
#define _GBI_RUNTIME_PTR_HELPERS 1
#include <PR/gbi.h>

#ifdef __cplusplus
#define ACGC_PC_ABI_STATIC_ASSERT(condition, message) static_assert(condition, message)
#else
#define ACGC_PC_ABI_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#endif

ACGC_PC_ABI_STATIC_ASSERT(sizeof(s32) == 4, "TARGET_PC s32 must be 32 bits");
ACGC_PC_ABI_STATIC_ASSERT(sizeof(u32) == 4, "TARGET_PC u32 must be 32 bits");
ACGC_PC_ABI_STATIC_ASSERT(sizeof(s64) == 8, "TARGET_PC s64 must be 64 bits");
ACGC_PC_ABI_STATIC_ASSERT(sizeof(u64) == 8, "TARGET_PC u64 must be 64 bits");
ACGC_PC_ABI_STATIC_ASSERT(sizeof(Gwords) == 8, "Gwords must contain two 32-bit words");
ACGC_PC_ABI_STATIC_ASSERT(sizeof(Gfx) == 8, "Gfx must contain one 64-bit guest command");
ACGC_PC_ABI_STATIC_ASSERT(offsetof(Gwords, w0) == 0, "Gwords.w0 must start at byte 0");
ACGC_PC_ABI_STATIC_ASSERT(offsetof(Gwords, w1) == 4, "Gwords.w1 must start at byte 4");
ACGC_PC_ABI_STATIC_ASSERT(sizeof(TexRect) == 16, "TexRect must contain four 32-bit words");
ACGC_PC_ABI_STATIC_ASSERT(offsetof(TexRect, w0) == 0, "TexRect.w0 must start at byte 0");
ACGC_PC_ABI_STATIC_ASSERT(offsetof(TexRect, w1) == 4, "TexRect.w1 must start at byte 4");
ACGC_PC_ABI_STATIC_ASSERT(offsetof(TexRect, w2) == 8, "TexRect.w2 must start at byte 8");
ACGC_PC_ABI_STATIC_ASSERT(offsetof(TexRect, w3) == 12, "TexRect.w3 must start at byte 12");

#undef ACGC_PC_ABI_STATIC_ASSERT

int main(void) {
    return 0;
}
