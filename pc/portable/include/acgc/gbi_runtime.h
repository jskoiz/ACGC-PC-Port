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

/*
 * LP64 static display-list references use the command's w1 as a tagged
 * logical word, the following Gfx as a full-width uintptr_t payload, and a
 * fixed trailer Gfx. The tag deliberately carries the originating opcode and
 * pointer/raw kind. The payload is never reconstructed from the tag, so a
 * native pointer is never truncated. The trailer is source-membership proof:
 * an exact raw guest word, including a pointer-kind-looking word, followed by
 * an ordinary command cannot be mistaken for a static reference. Raw guest
 * words also use a separate high-word marker in the payload, so both halves
 * of the representation are fail-closed.
 *
 * Layout (most significant bit first):
 *   0xE | pointer-kind | opcode (8 bits) | reserved bits (zero)
 *
 * The E prefix is reserved for this representation. Runtime references use
 * the separate F-prefixed registry namespace. On LP64 the physical width is
 * three Gfx entries; on ILP32 and the original target static macros retain
 * their one-entry representation.
 */
#define ACGC_GBI_STATIC_REFERENCE_PREFIX UINT32_C(0xE0000000)
#define ACGC_GBI_STATIC_REFERENCE_PREFIX_MASK UINT32_C(0xF0000000)
#define ACGC_GBI_STATIC_REFERENCE_POINTER_MASK UINT32_C(0x08000000)
#define ACGC_GBI_STATIC_REFERENCE_COMMAND_MASK UINT32_C(0x07F80000)
#define ACGC_GBI_STATIC_REFERENCE_COMMAND_SHIFT 19
#define ACGC_GBI_STATIC_REFERENCE_RESERVED_MASK UINT32_C(0x0007FFFF)

#if UINTPTR_MAX > UINT32_MAX
#define ACGC_GBI_STATIC_REFERENCE_PHYSICAL_WIDTH 3u
#define ACGC_GBI_STATIC_REFERENCE_TRAILER_W0 UINT32_C(0xA6C0F17E)
#define ACGC_GBI_STATIC_REFERENCE_TRAILER_W1 UINT32_C(0x53A9D421)
#define ACGC_GBI_STATIC_REFERENCE_TRAILER_IS_VALID(w0, w1) \
    ((uint32_t)(w0) == ACGC_GBI_STATIC_REFERENCE_TRAILER_W0 && \
     (uint32_t)(w1) == ACGC_GBI_STATIC_REFERENCE_TRAILER_W1)
#else
#define ACGC_GBI_STATIC_REFERENCE_PHYSICAL_WIDTH 1u
#define ACGC_GBI_STATIC_REFERENCE_TRAILER_W0 UINT32_C(0)
#define ACGC_GBI_STATIC_REFERENCE_TRAILER_W1 UINT32_C(0)
#define ACGC_GBI_STATIC_REFERENCE_TRAILER_IS_VALID(w0, w1) (0)
#endif

#if UINTPTR_MAX > UINT32_MAX
#define ACGC_GBI_STATIC_REFERENCE_RAW_PAYLOAD_MASK ((uintptr_t)UINT64_C(0xFFFFFFFF00000000))
#define ACGC_GBI_STATIC_REFERENCE_RAW_PAYLOAD_MAGIC ((uintptr_t)UINT64_C(0xA6C0F17E00000000))
#define ACGC_GBI_STATIC_REFERENCE_PACK_RAW(value) \
    (ACGC_GBI_STATIC_REFERENCE_RAW_PAYLOAD_MAGIC | \
     ((uintptr_t)(uint32_t)(value)))
#define ACGC_GBI_STATIC_REFERENCE_IS_RAW_PAYLOAD(payload) \
    (((uintptr_t)(payload) & ACGC_GBI_STATIC_REFERENCE_RAW_PAYLOAD_MASK) == \
     ACGC_GBI_STATIC_REFERENCE_RAW_PAYLOAD_MAGIC)
#define ACGC_GBI_STATIC_REFERENCE_RAW_VALUE(payload) \
    ((uint32_t)(uintptr_t)(payload))
#else
#define ACGC_GBI_STATIC_REFERENCE_PACK_RAW(value) ((uintptr_t)(uint32_t)(value))
#define ACGC_GBI_STATIC_REFERENCE_IS_RAW_PAYLOAD(payload) (1)
#define ACGC_GBI_STATIC_REFERENCE_RAW_VALUE(payload) ((uint32_t)(payload))
#endif

/* value is intentionally absent from the tag: pointer relocation arithmetic
   is not a portable static initializer on the host compilers. */
#define ACGC_GBI_STATIC_REFERENCE_TAG(value, is_pointer, command) \
    (ACGC_GBI_STATIC_REFERENCE_PREFIX | \
     ((is_pointer) ? ACGC_GBI_STATIC_REFERENCE_POINTER_MASK : UINT32_C(0)) | \
     ((((uint32_t)(command)) & UINT32_C(0xFF)) << ACGC_GBI_STATIC_REFERENCE_COMMAND_SHIFT))

#define ACGC_GBI_STATIC_REFERENCE_PAYLOAD(value, is_pointer) \
    ((is_pointer) ? (uintptr_t)(value) : ACGC_GBI_STATIC_REFERENCE_PACK_RAW(value))

#define ACGC_GBI_STATIC_REFERENCE_IS_TAGGED(tag) \
    ((((uint32_t)(tag)) & ACGC_GBI_STATIC_REFERENCE_PREFIX_MASK) == \
     ACGC_GBI_STATIC_REFERENCE_PREFIX)

#define ACGC_GBI_STATIC_REFERENCE_IS_WELL_FORMED(tag) \
    (ACGC_GBI_STATIC_REFERENCE_IS_TAGGED(tag) && \
     (((uint32_t)(tag) & ACGC_GBI_STATIC_REFERENCE_RESERVED_MASK) == 0))

#define ACGC_GBI_STATIC_REFERENCE_COMMAND(tag) \
    ((((uint32_t)(tag)) & ACGC_GBI_STATIC_REFERENCE_COMMAND_MASK) >> \
     ACGC_GBI_STATIC_REFERENCE_COMMAND_SHIFT)

#define ACGC_GBI_STATIC_REFERENCE_IS_POINTER(tag) \
    ((((uint32_t)(tag)) & ACGC_GBI_STATIC_REFERENCE_POINTER_MASK) != 0)

typedef enum AcgcGbiStaticReferenceStatus {
    ACGC_GBI_STATIC_REFERENCE_NOT_REFERENCE = 0,
    ACGC_GBI_STATIC_REFERENCE_RESOLVED,
    ACGC_GBI_STATIC_REFERENCE_INVALID_REFERENCE
} AcgcGbiStaticReferenceStatus;

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

AcgcGbiStaticReferenceStatus pc_gbi_unpack_static_reference(
    uint32_t tag,
    uintptr_t payload,
    uint32_t trailer_w0,
    uint32_t trailer_w1,
    uintptr_t* out_value,
    int* out_is_pointer
);

void pc_gbi_reset_runtime_ptr_registry(void);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_PORTABLE_GBI_RUNTIME_H */
