#include "acgc/gx_semantic_packet_adapter.h"

#include <string.h>

static void clear_packet(AcgcGxSemanticPacket* packet) {
    if (packet != NULL) {
        memset(packet, 0, sizeof(*packet));
    }
}

static void initialize_result(
    AcgcGxSemanticPacketAdapterResult* result,
    const GraphTaskSubmissionCapture* capture
) {
    if (result == NULL) {
        return;
    }

    memset(result, 0, sizeof(*result));
    result->version = ACGC_GX_SEMANTIC_PACKET_ADAPTER_RESULT_VERSION;
    if (capture != NULL) {
        result->graph_frame = capture->graph_frame;
        result->source_word_capacity = capture->source_word_capacity;
        result->captured_word_count = capture->captured_word_count;
    }
}

static AcgcGxSemanticPacketAdapterStatus set_status(
    AcgcGxSemanticPacketAdapterResult* result,
    AcgcGxSemanticPacketAdapterStatus status
) {
    if (result != NULL) {
        result->status = (uint32_t)status;
    }
    return status;
}

AcgcGxSemanticPacketAdapterStatus
acgc_gx_semantic_packet_adapter_consume_capture(
    const GraphTaskSubmissionCapture* capture,
    AcgcGxSemanticPacket* packet,
    AcgcGxSemanticPacketAdapterResult* result,
    AcgcGxSemanticPacketDecodeCallback decoder,
    void* decoder_context
) {
    initialize_result(result, capture);
    clear_packet(packet);

    if (capture == NULL || packet == NULL) {
        return set_status(
            result,
            ACGC_GX_SEMANTIC_PACKET_ADAPTER_INVALID_ARGUMENT
        );
    }

    if (capture->version != ACGC_GRAPH_SUBMISSION_CAPTURE_VERSION ||
        capture->captured_word_count > ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS ||
        capture->captured_word_count > capture->source_word_capacity) {
        return set_status(
            result,
            ACGC_GX_SEMANTIC_PACKET_ADAPTER_INVALID_CAPTURE
        );
    }

    if (capture->captured_word_count == 0) {
        return set_status(
            result,
            ACGC_GX_SEMANTIC_PACKET_ADAPTER_EMPTY_CAPTURE
        );
    }

    /* A short fixed snapshot cannot prove that its raw command prefix ends. */
    if (capture->captured_word_count < capture->source_word_capacity) {
        return set_status(
            result,
            ACGC_GX_SEMANTIC_PACKET_ADAPTER_INCOMPLETE_CAPTURE
        );
    }

    if (decoder == NULL) {
        return set_status(
            result,
            ACGC_GX_SEMANTIC_PACKET_ADAPTER_DECODER_UNAVAILABLE
        );
    }

    if (!decoder(decoder_context, capture, packet)) {
        clear_packet(packet);
        return set_status(
            result,
            ACGC_GX_SEMANTIC_PACKET_ADAPTER_DECODER_REJECTED
        );
    }

    if (!acgc_gx_semantic_packet_validate(packet)) {
        clear_packet(packet);
        return set_status(
            result,
            ACGC_GX_SEMANTIC_PACKET_ADAPTER_INVALID_PACKET
        );
    }

    return set_status(
        result,
        ACGC_GX_SEMANTIC_PACKET_ADAPTER_ACCEPTED
    );
}
