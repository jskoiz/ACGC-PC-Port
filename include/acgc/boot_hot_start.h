#ifndef ACGC_BOOT_HOT_START_H
#define ACGC_BOOT_HOT_START_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* (*AcgcBootHotStartInvoke)(void* entry, void* context);

/* Advance one opaque hot-start entry, returning whether another remains. */
int acgc_boot_hot_start_step(
    void** entry,
    AcgcBootHotStartInvoke invoke,
    void* context
);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_BOOT_HOT_START_H */
