#ifndef PC_GX_CUMULATIVE_GATHERER_H
#define PC_GX_CUMULATIVE_GATHERER_H

#include <stddef.h>
#include <stdint.h>

#include "pc_gx_cumulative_snapshot.h"
#include "pc_gx_internal.h"
#include "pc_gx_texture_raw_state.h"

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

/*
 * The canonical resource callback is the only cumulative seam that exposes
 * the exact pointer-bearing lease.  It is invoked synchronously while the
 * gatherer's borrow is active and must copy/consume every byte it needs before
 * returning; it may not retain the Texture/Dynamic values, lease, envelope,
 * or any pointer reachable from them.  It runs before the pointer-free
 * envelope callback; a zero return or failed post-callback lease revalidation
 * aborts publication.  The borrow ends successfully before the pointer-free
 * envelope callback runs.  The attempt id is the same id delivered by the
 * post-borrow attempt callback;
 * direct gather calls that do not install an id receive zero.  The callback
 * remains same-owner/non-reentrant and may not mutate GX state or register or
 * clear cumulative callbacks.
 */
typedef int (*PCGXCumulativeSnapshotResourceCallback)(
    void* context,
    uint64_t attempt_id,
    const uint8_t* envelope,
    size_t envelope_byte_size,
    const AcgcGxCanonicalTextureState* texture,
    const AcgcGxCanonicalDynamicState* dynamic,
    const PCGXTextureDynamicLease* lease
);

typedef enum PCGXCumulativeSnapshotAttemptResult {
    PC_GX_CUMULATIVE_SNAPSHOT_ATTEMPT_NO_PUBLICATION = 0,
    PC_GX_CUMULATIVE_SNAPSHOT_ATTEMPT_PUBLISHED = 1
} PCGXCumulativeSnapshotAttemptResult;

/*
 * This notification is issued once, synchronously, after the gatherer has
 * ended its Texture/Dynamic borrow.  `attempt_id` is process-lifetime state;
 * it is never reset by pc_gx_init() or pc_gx_shutdown().  The notification
 * carries no envelope or resource pointer and remains same-owner/non-reentrant.
 */
typedef void (*PCGXCumulativeSnapshotAttemptCallback)(
    void* context,
    uint64_t attempt_id,
    int result
);

/* Registration is rejected while a gather/Texture borrow is active. */
int pc_gx_set_cumulative_snapshot_callback(
    PCGXCumulativeSnapshotCallback callback,
    void* context
);
int pc_gx_clear_cumulative_snapshot_callback(void);

/* Atomically install the envelope and completion callbacks as one pair. */
int pc_gx_set_cumulative_snapshot_callbacks(
    PCGXCumulativeSnapshotCallback callback,
    PCGXCumulativeSnapshotAttemptCallback attempt_callback,
    void* context
);
int pc_gx_clear_cumulative_snapshot_callbacks(void);

/* Register the active-borrow resource transport independently of the
 * pointer-free envelope/attempt callback pair. */
int pc_gx_set_cumulative_snapshot_resource_callback(
    PCGXCumulativeSnapshotResourceCallback callback,
    void* context
);
int pc_gx_clear_cumulative_snapshot_resource_callback(void);

/* Set the attempt id visible to the active-borrow resource callback. */
int pc_gx_set_cumulative_snapshot_attempt_id(uint64_t attempt_id);

/* Called by the completed-Geometry flush boundary after the borrow ends. */
int pc_gx_notify_cumulative_snapshot_attempt(
    uint64_t attempt_id,
    PCGXCumulativeSnapshotAttemptResult result
);

/* Internal lifecycle guard shared by the envelope and completion callbacks. */
int pc_gx_cumulative_snapshot_callback_dispatch_is_active(void);

/*
 * Build and publish one complete fourteen-section envelope from the raw state
 * represented by g_gx and the caller-supplied completed Geometry batch.  This
 * function does not call pc_gx_raw_geometry_capture_completed().  Both input
 * pointers must remain valid for the synchronous call.  It returns one only
 * when the resource callback, lease revalidation, and borrow end all succeeded
 * before the pointer-free callback ran; otherwise it returns zero and invokes
 * no callback.  On failure, envelope, envelope_byte_size, and sections remain
 * unchanged; the encoded and Geometry scratch workspaces are caller-owned
 * staging areas and may be overwritten.  The supplied storage remains
 * caller-owned and may be
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
