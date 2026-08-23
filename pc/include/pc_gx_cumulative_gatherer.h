#ifndef PC_GX_CUMULATIVE_GATHERER_H
#define PC_GX_CUMULATIVE_GATHERER_H

#include <stddef.h>
#include <stdint.h>

#include "pc_gx_cumulative_snapshot.h"
#include "pc_gx_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The gatherer owns no heap memory and does not capture raw Geometry itself.
 * A caller supplies the completed pointer-free batch captured at the flush
 * boundary and one reusable storage object.  The byte arrays are deliberately
 * separate from the metadata and destination so the cumulative assembler's
 * alias checks remain meaningful.
 */
#define PC_GX_CUMULATIVE_GATHERER_GEOMETRY_SCRATCH_SIZE \
    ACGC_GX_CANONICAL_GEOMETRY_MAX_SECTION_SIZE
#define PC_GX_CUMULATIVE_GATHERER_ENCODED_PAYLOAD_SIZE \
    (PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES - \
     PC_GX_CUMULATIVE_SNAPSHOT_PAYLOAD_OFFSET)

typedef struct PCGXCumulativeSnapshotStorage {
    _Alignas(size_t) uint8_t encoded_payload[
        PC_GX_CUMULATIVE_GATHERER_ENCODED_PAYLOAD_SIZE];
    _Alignas(size_t) uint8_t geometry_scratch[
        PC_GX_CUMULATIVE_GATHERER_GEOMETRY_SCRATCH_SIZE];
    _Alignas(size_t) uint8_t envelope[PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES];
    PCGXCumulativeSnapshotSection sections[
        PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT];
    size_t envelope_byte_size;
} PCGXCumulativeSnapshotStorage;

/*
 * The callback receives only the completed pointer-free envelope.  The
 * Texture/Dynamic lease remains private to the synchronous gather transaction
 * and no resource pointer can escape through this API.  The envelope pointer
 * is valid only until the callback returns; consumers that need retention
 * must copy it.  The callback is a read-only, non-reentrant consumer of the
 * GX owner: it must not call pc_gx_init(), pc_gx_shutdown(), GX state setters,
 * vertex submission, GXBegin(), GXEnd(), pc_gx_flush_vertices(), cumulative
 * callback registration/clear, or nested cumulative gathering.  It may copy
 * the envelope bytes and update independent observer context before return.
 */
typedef void (*PCGXCumulativeSnapshotCallback)(
    void* context,
    const uint8_t* envelope,
    size_t envelope_byte_size
);

/* Registration is rejected while a gather/Texture borrow is active. */
int pc_gx_set_cumulative_snapshot_callback(
    PCGXCumulativeSnapshotCallback callback,
    void* context
);
int pc_gx_clear_cumulative_snapshot_callback(void);

/*
 * Build and publish one complete fourteen-section envelope from the raw state
 * represented by g_gx and the caller-supplied completed Geometry batch.  This
 * function does not call pc_gx_raw_geometry_capture_completed().  Both input
 * pointers must remain valid for the synchronous call.  It returns one only
 * when the callback ran after successful production, encoding, assembly, and
 * lease revalidation; otherwise it returns zero and invokes no callback.  On
 * failure, envelope, envelope_byte_size, and sections remain unchanged; the
 * encoded and Geometry scratch workspaces are caller-owned staging areas and
 * may be overwritten.  The supplied storage remains caller-owned and may be
 * reused after return.  The current PC GX state and Texture/TLUT borrow seam
 * are single-threaded and permit only one active invocation at a time.
 */
int pc_gx_cumulative_snapshot_gather(
    const PCGXRawGeometryBatch* completed_geometry,
    PCGXCumulativeSnapshotStorage* storage
);

#ifdef __cplusplus
}
#endif

#endif /* PC_GX_CUMULATIVE_GATHERER_H */
