#ifndef ACGC_GX_SEMANTIC_PACKET_ADAPTER_H
#define ACGC_GX_SEMANTIC_PACKET_ADAPTER_H

#include "acgc/graph_submission.h"
#include "acgc/gx_semantic_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The adapter result is a fixed-width diagnostic handoff, not a packet ABI. */
#define ACGC_GX_SEMANTIC_PACKET_ADAPTER_RESULT_VERSION UINT32_C(1)

typedef enum AcgcGxSemanticPacketAdapterStatus {
    ACGC_GX_SEMANTIC_PACKET_ADAPTER_ACCEPTED = 0,
    ACGC_GX_SEMANTIC_PACKET_ADAPTER_INVALID_ARGUMENT = 1,
    ACGC_GX_SEMANTIC_PACKET_ADAPTER_INVALID_CAPTURE = 2,
    ACGC_GX_SEMANTIC_PACKET_ADAPTER_EMPTY_CAPTURE = 3,
    ACGC_GX_SEMANTIC_PACKET_ADAPTER_INCOMPLETE_CAPTURE = 4,
    ACGC_GX_SEMANTIC_PACKET_ADAPTER_DECODER_UNAVAILABLE = 5,
    ACGC_GX_SEMANTIC_PACKET_ADAPTER_DECODER_REJECTED = 6,
    ACGC_GX_SEMANTIC_PACKET_ADAPTER_INVALID_PACKET = 7
} AcgcGxSemanticPacketAdapterStatus;

typedef struct AcgcGxSemanticPacketAdapterResult {
    uint32_t version;
    uint32_t status;
    uint32_t graph_frame;
    uint32_t source_word_capacity;
    uint32_t captured_word_count;
} AcgcGxSemanticPacketAdapterResult;

#if defined(__cplusplus)
static_assert(
    sizeof(AcgcGxSemanticPacketAdapterResult) == 20,
    "GX semantic packet adapter result must stay fixed-width"
);
#else
_Static_assert(
    sizeof(AcgcGxSemanticPacketAdapterResult) == 20,
    "GX semantic packet adapter result must stay fixed-width"
);
#endif

/*
 * Decode a complete graph-owned prefix into the existing value packet.
 *
 * The graph capture contains raw N64 work-list words, not GX vertices or
 * renderer state. The decoder is therefore an explicit owner boundary: this
 * adapter never guesses command meanings, follows captured pointers, or
 * fabricates a packet from an incomplete prefix. The capture pointer is valid
 * only for the duration of the callback; no pointer is retained.
 */
typedef int (*AcgcGxSemanticPacketDecodeCallback)(
    void* context,
    const GraphTaskSubmissionCapture* capture,
    AcgcGxSemanticPacket* packet
);

/*
 * Consume one graph capture and, when a complete capture has a decoder, write
 * one validated packet. The packet is cleared before use and remains zero on
 * every non-accepted result. result may be NULL when only the status is
 * needed.
 */
AcgcGxSemanticPacketAdapterStatus
acgc_gx_semantic_packet_adapter_consume_capture(
    const GraphTaskSubmissionCapture* capture,
    AcgcGxSemanticPacket* packet,
    AcgcGxSemanticPacketAdapterResult* result,
    AcgcGxSemanticPacketDecodeCallback decoder,
    void* decoder_context
);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_GX_SEMANTIC_PACKET_ADAPTER_H */
