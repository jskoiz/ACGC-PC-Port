#ifndef ACGC_PORTABLE_GBI_REFERENCE_REGISTRY_H
#define ACGC_PORTABLE_GBI_REFERENCE_REGISTRY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Runtime GBI command words are 32-bit guest values, while the values they
 * refer to are native host pointer-width values.  A registry handle reserves
 * the high nibble 0xF, which is outside the ordinary N64 segmented-address
 * range used by the PC decoder.  The decoder checks this registry before its
 * normal address-resolution rules.
 *
 * Handles are process-local capabilities, not guest addresses.  The registry
 * does not own the referenced memory.  A value remains registered until its
 * handle is released or the registry is reset; callers must keep the registry
 * alive while command words containing its handles may still be consumed.
 * Registration deduplicates an already-live native value.
 *
 * This type is intentionally not thread-safe.  One game/renderer owner must
 * perform registration, resolution, release, and reset.  A future cross-thread
 * adapter must provide its own external synchronization.
 */
#define ACGC_GBI_REFERENCE_REGISTRY_CAPACITY 8192u
#define ACGC_GBI_REFERENCE_HANDLE_PREFIX_MASK UINT32_C(0xF0000000)
#define ACGC_GBI_REFERENCE_HANDLE_PREFIX UINT32_C(0xF0000000)
#define ACGC_GBI_REFERENCE_HANDLE_SLOT_MASK UINT32_C(0x00001FFF)
#define ACGC_GBI_REFERENCE_HANDLE_GENERATION_MASK UINT32_C(0x00007FFF)
#define ACGC_GBI_REFERENCE_HANDLE_GENERATION_SHIFT 13u

typedef enum AcgcGbiReferenceStatus {
    ACGC_GBI_REFERENCE_OK = 0,
    ACGC_GBI_REFERENCE_INVALID_ARGUMENT,
    ACGC_GBI_REFERENCE_INVALID_HANDLE,
    ACGC_GBI_REFERENCE_STALE_HANDLE,
    ACGC_GBI_REFERENCE_EXHAUSTED
} AcgcGbiReferenceStatus;

typedef struct AcgcGbiReferenceEntry {
    uintptr_t value;
    uint16_t generation;
    uint8_t occupied;
} AcgcGbiReferenceEntry;

typedef struct AcgcGbiReferenceRegistry {
    AcgcGbiReferenceEntry entries[ACGC_GBI_REFERENCE_REGISTRY_CAPACITY];
    uint32_t next_slot;
} AcgcGbiReferenceRegistry;

void acgc_gbi_reference_registry_init(AcgcGbiReferenceRegistry* registry);

/*
 * Invalidate every handle and return allocation to slot zero.  Each slot's
 * generation advances, so handles from before reset remain stale.  Generation
 * values are 15-bit and skip zero; after the bounded wrap, an ancient stale
 * handle can numerically recur and must no longer be retained by callers.
 */
void acgc_gbi_reference_registry_reset(AcgcGbiReferenceRegistry* registry);

AcgcGbiReferenceStatus acgc_gbi_reference_registry_register(
    AcgcGbiReferenceRegistry* registry,
    uintptr_t value,
    uint32_t* out_handle
);

AcgcGbiReferenceStatus acgc_gbi_reference_registry_resolve(
    const AcgcGbiReferenceRegistry* registry,
    uint32_t handle,
    uintptr_t* out_value
);

AcgcGbiReferenceStatus acgc_gbi_reference_registry_release(
    AcgcGbiReferenceRegistry* registry,
    uint32_t handle
);

const char* acgc_gbi_reference_status_string(AcgcGbiReferenceStatus status);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_PORTABLE_GBI_REFERENCE_REGISTRY_H */
