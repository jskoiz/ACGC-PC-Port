#include "acgc/gx_semantic_packet_adapter.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct DecoderProbe {
    int calls;
    uint32_t graph_frame;
    uint32_t first_word;
} DecoderProbe;

static uint32_t bits_from_float(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void set_vertex(
    AcgcGxSemanticVertex* vertex,
    float x,
    float y,
    float z,
    uint32_t color
) {
    memset(vertex, 0, sizeof(*vertex));
    vertex->position[0] = bits_from_float(x);
    vertex->position[1] = bits_from_float(y);
    vertex->position[2] = bits_from_float(z);
    vertex->normal[1] = bits_from_float(1.0f);
    vertex->color_rgba8 = color;
}

static int decode_triangle(
    void* context,
    const GraphTaskSubmissionCapture* capture,
    AcgcGxSemanticPacket* packet
) {
    DecoderProbe* probe = (DecoderProbe*)context;

    probe->calls++;
    probe->graph_frame = capture->graph_frame;
    probe->first_word = capture->words[0];
    if (!acgc_gx_semantic_packet_init(packet)) {
        return 0;
    }
    packet->vertex_count = 3;
    packet->material.flags = ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR;
    set_vertex(&packet->vertices[0], 0.0f, 0.75f, 0.0f, UINT32_C(0xF94144FF));
    set_vertex(&packet->vertices[1], -0.75f, -0.65f, 0.0f, UINT32_C(0x43AA8BFF));
    set_vertex(&packet->vertices[2], 0.75f, -0.65f, 0.0f, UINT32_C(0x577590FF));
    return 1;
}

static int reject_decode(
    void* context,
    const GraphTaskSubmissionCapture* capture,
    AcgcGxSemanticPacket* packet
) {
    (void)context;
    (void)capture;
    (void)packet;
    return 0;
}

static int malformed_decode(
    void* context,
    const GraphTaskSubmissionCapture* capture,
    AcgcGxSemanticPacket* packet
) {
    (void)context;
    (void)capture;
    (void)acgc_gx_semantic_packet_init(packet);
    packet->vertex_count = 3;
    packet->vertices[0].position[0] = UINT32_C(0x7FC00000);
    return 1;
}

static GraphTaskSubmissionCapture complete_capture(void) {
    GraphTaskSubmissionCapture capture = {0};
    uint32_t i;

    capture.version = ACGC_GRAPH_SUBMISSION_CAPTURE_VERSION;
    capture.graph_frame = 19;
    capture.source_word_capacity = ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS;
    capture.captured_word_count = ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS;
    for (i = 0; i < capture.captured_word_count; ++i) {
        capture.words[i] = UINT32_C(0x10000000) + i;
    }
    return capture;
}

int main(void) {
    AcgcGxSemanticPacket packet;
    AcgcGxSemanticPacketAdapterResult result;
    DecoderProbe probe = {0};
    GraphTaskSubmissionCapture capture = complete_capture();
    GraphTaskSubmissionCapture live_capture = {0};
    AcgcGxSemanticPacketAdapterStatus status;
    uint32_t i;

    status = acgc_gx_semantic_packet_adapter_consume_capture(
        &capture,
        &packet,
        &result,
        decode_triangle,
        &probe
    );
    CHECK(status == ACGC_GX_SEMANTIC_PACKET_ADAPTER_ACCEPTED);
    CHECK(result.version == ACGC_GX_SEMANTIC_PACKET_ADAPTER_RESULT_VERSION);
    CHECK(result.status == (uint32_t)status);
    CHECK(result.graph_frame == 19);
    CHECK(result.source_word_capacity == ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS);
    CHECK(result.captured_word_count == ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS);
    CHECK(probe.calls == 1);
    CHECK(probe.graph_frame == 19);
    CHECK(probe.first_word == UINT32_C(0x10000000));
    CHECK(acgc_gx_semantic_packet_validate(&packet));

    memset(&packet, 0xA5, sizeof(packet));
    status = acgc_gx_semantic_packet_adapter_consume_capture(
        &capture,
        &packet,
        &result,
        NULL,
        NULL
    );
    CHECK(status == ACGC_GX_SEMANTIC_PACKET_ADAPTER_DECODER_UNAVAILABLE);
    CHECK(result.status == (uint32_t)status);
    CHECK(packet.version == 0);
    CHECK(packet.vertex_count == 0);

    memset(&packet, 0xA5, sizeof(packet));
    status = acgc_gx_semantic_packet_adapter_consume_capture(
        &capture,
        &packet,
        &result,
        reject_decode,
        NULL
    );
    CHECK(status == ACGC_GX_SEMANTIC_PACKET_ADAPTER_DECODER_REJECTED);
    CHECK(packet.version == 0);
    CHECK(packet.vertex_count == 0);

    memset(&packet, 0xA5, sizeof(packet));
    status = acgc_gx_semantic_packet_adapter_consume_capture(
        &capture,
        &packet,
        &result,
        malformed_decode,
        NULL
    );
    CHECK(status == ACGC_GX_SEMANTIC_PACKET_ADAPTER_INVALID_PACKET);
    CHECK(packet.version == 0);
    CHECK(packet.vertex_count == 0);

    /* This is the observed live shape: 8 captured words from a 256-word list. */
    live_capture.version = ACGC_GRAPH_SUBMISSION_CAPTURE_VERSION;
    live_capture.graph_frame = 0;
    live_capture.source_word_capacity = 256;
    live_capture.captured_word_count = ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS;
    live_capture.words[0] = UINT32_C(0xDE010000);
    live_capture.words[1] = UINT32_C(0xF0002000);
    CHECK(live_capture.words[0] == UINT32_C(0xDE010000));
    CHECK(live_capture.words[1] == UINT32_C(0xF0002000));
    for (i = 2; i < ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS; ++i) {
        CHECK(live_capture.words[i] == 0);
    }
    memset(&packet, 0xA5, sizeof(packet));
    probe.calls = 0;
    status = acgc_gx_semantic_packet_adapter_consume_capture(
        &live_capture,
        &packet,
        &result,
        decode_triangle,
        &probe
    );
    CHECK(status == ACGC_GX_SEMANTIC_PACKET_ADAPTER_INCOMPLETE_CAPTURE);
    CHECK(result.graph_frame == 0);
    CHECK(result.source_word_capacity == 256);
    CHECK(result.captured_word_count == ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS);
    CHECK(probe.calls == 0);
    CHECK(packet.version == 0);
    CHECK(packet.vertex_count == 0);

    capture.captured_word_count = 0;
    memset(&packet, 0xA5, sizeof(packet));
    status = acgc_gx_semantic_packet_adapter_consume_capture(
        &capture,
        &packet,
        &result,
        decode_triangle,
        &probe
    );
    CHECK(status == ACGC_GX_SEMANTIC_PACKET_ADAPTER_EMPTY_CAPTURE);
    CHECK(packet.version == 0);

    capture = complete_capture();
    capture.version = 0;
    status = acgc_gx_semantic_packet_adapter_consume_capture(
        &capture,
        &packet,
        &result,
        decode_triangle,
        &probe
    );
    CHECK(status == ACGC_GX_SEMANTIC_PACKET_ADAPTER_INVALID_CAPTURE);

    capture = complete_capture();
    capture.captured_word_count = ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS + 1;
    status = acgc_gx_semantic_packet_adapter_consume_capture(
        &capture,
        &packet,
        &result,
        decode_triangle,
        &probe
    );
    CHECK(status == ACGC_GX_SEMANTIC_PACKET_ADAPTER_INVALID_CAPTURE);

    capture = complete_capture();
    capture.source_word_capacity = 2;
    status = acgc_gx_semantic_packet_adapter_consume_capture(
        &capture,
        &packet,
        &result,
        decode_triangle,
        &probe
    );
    CHECK(status == ACGC_GX_SEMANTIC_PACKET_ADAPTER_INVALID_CAPTURE);

    CHECK(
        acgc_gx_semantic_packet_adapter_consume_capture(
            NULL,
            &packet,
            &result,
            decode_triangle,
            &probe
        ) == ACGC_GX_SEMANTIC_PACKET_ADAPTER_INVALID_ARGUMENT
    );
    CHECK(
        acgc_gx_semantic_packet_adapter_consume_capture(
            &capture,
            NULL,
            &result,
            decode_triangle,
            &probe
        ) == ACGC_GX_SEMANTIC_PACKET_ADAPTER_INVALID_ARGUMENT
    );

    puts("GX semantic packet adapter tests: PASS (complete handoff and fail-closed live-prefix gate)");
    return 0;
}
