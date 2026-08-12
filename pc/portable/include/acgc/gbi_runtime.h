#ifndef ACGC_PORTABLE_GBI_RUNTIME_H
#define ACGC_PORTABLE_GBI_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Classify a 32-bit GBI word without making the caller infer status from a
 * zero pointer. NOT_REFERENCE preserves the normal segmented/raw path;
 * RESOLVED returns a live registry value; INVALID_REFERENCE means the
 * reserved-prefix word is malformed or stale and must fail closed.
 */
typedef enum AcgcGbiRuntimePtrStatus {
    ACGC_GBI_RUNTIME_PTR_NOT_REFERENCE = 0,
    ACGC_GBI_RUNTIME_PTR_RESOLVED,
    ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE
} AcgcGbiRuntimePtrStatus;

uint32_t pc_gbi_pack_runtime_ptr(
    uintptr_t addr,
    int is_ptr,
    const char* expr,
    const char* file,
    int line
);

AcgcGbiRuntimePtrStatus pc_gbi_unpack_runtime_ptr(
    uint32_t packed,
    uintptr_t* out_value
);

void pc_gbi_reset_runtime_ptr_registry(void);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_PORTABLE_GBI_RUNTIME_H */
