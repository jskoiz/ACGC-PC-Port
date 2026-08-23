#ifndef PC_GX_CUMULATIVE_SNAPSHOT_H
#define PC_GX_CUMULATIVE_SNAPSHOT_H

#include <stddef.h>
#include <stdint.h>

#include "acgc/gx_canonical_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is the renderer-neutral assembly boundary for one complete V1
 * canonical envelope. The caller supplies fourteen already-built canonical
 * little-endian byte sections in directory order. The assembler validates the
 * envelope and directory metadata and copies those bytes; it does not
 * serialize native value structs, validate payload semantic fields or
 * cross-section value dependencies, acquire leases, gather live state,
 * publish callbacks, or render. No resource, cache, producer, callback, or
 * renderer object crosses this boundary.
 */
#define PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT UINT32_C(14)
#define PC_GX_CUMULATIVE_SNAPSHOT_FULL_MASK UINT32_C(0x00003FFF)
#define PC_GX_CUMULATIVE_SNAPSHOT_HEADER_SIZE \
    ACGC_GX_CANONICAL_ENVELOPE_HEADER_SIZE
#define PC_GX_CUMULATIVE_SNAPSHOT_DIRECTORY_ENTRY_SIZE \
    ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE
#define PC_GX_CUMULATIVE_SNAPSHOT_PAYLOAD_OFFSET \
    ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET
#define PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES UINT32_C(76092)

typedef struct PCGXCumulativeSnapshotByteSpan {
    const uint8_t* data;
    size_t size;
} PCGXCumulativeSnapshotByteSpan;

/*
 * Metadata is value-only and remains separate from the serialized byte span.
 * byte_size must equal bytes.size and is emitted as the directory byte_size
 * word after it has passed the fixed-width bounds of the V1 ABI. Every span
 * must already contain an explicitly encoded canonical little-endian section
 * byte stream; this API does not accept native value structs as payloads.
 */
typedef struct PCGXCumulativeSnapshotSection {
    uint32_t section_id;
    uint32_t section_version;
    size_t byte_size;
    uint32_t count;
    uint32_t capacity;
    uint32_t valid_mask;
    PCGXCumulativeSnapshotByteSpan bytes;
} PCGXCumulativeSnapshotSection;

/*
 * Assemble one exact little-endian V1 envelope. sections[0] through
 * sections[13] must be section IDs 1 through 14 respectively, and every
 * section must be present. On failure, destination and destination_byte_size
 * are left byte-for-byte unchanged. destination_byte_size must point to a
 * suitably aligned writable size_t object that is disjoint from destination,
 * the section metadata array, and every input byte span.
 */
int pc_gx_cumulative_snapshot_assemble(
    const PCGXCumulativeSnapshotSection sections[
        PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT],
    uint8_t* destination,
    size_t destination_capacity,
    size_t* destination_byte_size
);

#ifdef __cplusplus
}
#endif

#endif /* PC_GX_CUMULATIVE_SNAPSHOT_H */
