#ifndef ACGC_APPLE_CANONICAL_ENVELOPE_PARSER_H
#define ACGC_APPLE_CANONICAL_ENVELOPE_PARSER_H

#include <stddef.h>
#include <stdint.h>

#include "acgc/gx_canonical_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The canonical V1 wire constants and section IDs are owned by
 * gx_canonical_state.h. The parser reads that ABI explicitly as little-endian
 * bytes and never casts its input to the native metadata structs.
 */

typedef enum AcgcAppleCanonicalEnvelopeParserStatus {
    ACGC_APPLE_CANONICAL_ENVELOPE_OK = 0,
    ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_ARGUMENT,
    ACGC_APPLE_CANONICAL_ENVELOPE_OUTPUT_OVERLAP,
    ACGC_APPLE_CANONICAL_ENVELOPE_TRUNCATED,
    ACGC_APPLE_CANONICAL_ENVELOPE_UNSUPPORTED_VERSION,
    ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_HEADER,
    ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_SIZE,
    ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY,
    ACGC_APPLE_CANONICAL_ENVELOPE_OVERFLOW
} AcgcAppleCanonicalEnvelopeParserStatus;

/* A decoded directory entry; it contains no pointer into the input bytes. */
typedef struct AcgcAppleCanonicalEnvelopeSection {
    uint32_t section_id;
    uint32_t section_version;
    uint32_t byte_offset;
    uint32_t byte_size;
    uint32_t count;
    uint32_t capacity;
    uint32_t valid_mask;
    uint32_t reserved;
} AcgcAppleCanonicalEnvelopeSection;

/*
 * A caller-owned structural view of one complete canonical envelope. The
 * view contains metadata and payload ranges only; it retains no input
 * pointer and performs no semantic section-value validation.
 */
typedef struct AcgcAppleCanonicalEnvelopeView {
    uint32_t magic;
    uint32_t version;
    uint32_t header_byte_size;
    uint32_t directory_entry_byte_size;
    uint32_t directory_count;
    uint32_t known_state_mask;
    uint32_t present_state_mask;
    uint32_t required_state_mask;
    uint32_t payload_offset;
    uint32_t payload_byte_size;
    uint32_t total_byte_size;
    uint32_t reserved;
    AcgcAppleCanonicalEnvelopeSection sections[
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT];
} AcgcAppleCanonicalEnvelopeView;

/*
 * Parse one exact byte extent. On every non-OK return, output is left
 * byte-for-byte unchanged. The output's full object range must be disjoint
 * from the input extent; overlapping ranges return OUTPUT_OVERLAP. The input
 * and output remain owned by the caller; this function allocates nothing and
 * retains no pointer.
 */
AcgcAppleCanonicalEnvelopeParserStatus
acgc_apple_canonical_envelope_parse(
    const uint8_t* bytes,
    size_t byte_size,
    AcgcAppleCanonicalEnvelopeView* output
);

const char* acgc_apple_canonical_envelope_status_string(
    AcgcAppleCanonicalEnvelopeParserStatus status
);

#ifdef __cplusplus
}
#endif

#endif
