#include "pc_gx_internal.h"

#include <acgc/gx_canonical_channel_state.h>
#include <dolphin/gx/GXEnum.h>
#include <dolphin/gx/GXLighting.h>
#include <dolphin/gx/GXStruct.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int g_pc_window_w = PC_SCREEN_WIDTH;
int g_pc_window_h = PC_SCREEN_HEIGHT;
int g_pc_widescreen_stretch = 0;

void pc_gx_tev_seq_reset(void) {
}

static PCGXShaderVariant g_fixture_shader_variant;

PCGXShaderVariant* pc_gx_tev_get_variant(void) {
    return &g_fixture_shader_variant;
}

static void fixture_gl_bind_vertex_array(GLuint array) {
    (void)array;
}

static void fixture_gl_bind_buffer(GLenum target, GLuint buffer) {
    (void)target;
    (void)buffer;
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct {
    int calls;
    int in_begin;
    int current_vertex_idx;
    int pending_verts;
    PCGXRawChannels before;
} ChannelsFlushObservation;

static GXColor fixture_color(
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t alpha
) {
    GXColor color = { red, green, blue, alpha };
    return color;
}

static const PCGXRawChannels* raw_channels(void) {
    return pc_gx_raw_channels_shadow_fixture();
}

static void reset_state(void) {
    /* The fixture resets the whole host state as the pc_gx_init() boundary
     * does. Production code has no raw-Channels reset other than pc_gx_init. */
    pc_gx_clear_geometry_flush_fixture_observer();
    memset(&g_gx, 0, sizeof(g_gx));
    glad_glBindVertexArray = fixture_gl_bind_vertex_array;
    glad_glBindBuffer = fixture_gl_bind_buffer;
}

static void prepare_completed_batch(ChannelsFlushObservation* observation) {
    memset(observation, 0, sizeof(*observation));
    g_gx.dirty = 0;
    g_gx.in_begin = 1;
    g_gx.expected_vertex_count = 1;
    g_gx.current_vertex_idx = 1;
    g_gx.pending_verts = 0;
    g_gx.vertex_pending = 0;
    g_gx.current_primitive = GX_TRIANGLES;
    g_gx.pending_prim = GX_TRIANGLES;
}

static void observe_channels_flush(void* context) {
    ChannelsFlushObservation* observation =
        (ChannelsFlushObservation*)context;

    observation->calls++;
    observation->in_begin = g_gx.in_begin;
    observation->current_vertex_idx = g_gx.current_vertex_idx;
    observation->pending_verts = g_gx.pending_verts;
    observation->before = *raw_channels();
}

static void begin_observed_batch(ChannelsFlushObservation* observation) {
    prepare_completed_batch(observation);
    pc_gx_set_geometry_flush_fixture_observer(
        observe_channels_flush,
        observation
    );
}

static void finish_observed_batch(void) {
    pc_gx_clear_geometry_flush_fixture_observer();
}

static void fill_record0(void) {
    GXSetNumChans(1);
    GXSetChanCtrl(
        GX_COLOR0A0,
        GX_FALSE,
        GX_SRC_REG,
        GX_SRC_REG,
        0,
        GX_DF_NONE,
        GX_AF_NONE
    );
    GXSetChanAmbColor(
        GX_COLOR0A0,
        fixture_color(0x11, 0x22, 0x33, 0x44)
    );
    GXSetChanMatColor(
        GX_COLOR0A0,
        fixture_color(0x55, 0x66, 0x77, 0x88)
    );
}

static void fill_record1(void) {
    GXSetChanCtrl(
        GX_COLOR1A1,
        GX_TRUE,
        GX_SRC_REG,
        GX_SRC_VTX,
        GX_LIGHT0 | GX_LIGHT7,
        GX_DF_CLAMP,
        GX_AF_SPOT
    );
    GXSetChanAmbColor(
        GX_COLOR1A1,
        fixture_color(0x91, 0x92, 0x93, 0x94)
    );
    GXSetChanMatColor(
        GX_COLOR1A1,
        fixture_color(0xA1, 0xA2, 0xA3, 0xA4)
    );
}

static int test_initial_unknownness_and_counts(void) {
    AcgcGxCanonicalChannelState state;
    uint32_t count;

    reset_state();
    CHECK(raw_channels() == &g_gx.raw_channels);
    CHECK(raw_channels()->active_count == 0);
    CHECK(raw_channels()->active_count_known == 0);
    CHECK(raw_channels()->invalid == 0);
    CHECK(!pc_gx_raw_channels_build_canonical(&state));

    GXSetNumChans(0);
    CHECK(raw_channels()->active_count_known == 1);
    CHECK(pc_gx_raw_channels_build_canonical(&state));
    CHECK(state.active_count == 0);
    CHECK(state.record_valid_mask == 0);
    CHECK(memcmp(&state.records[0], &(AcgcGxCanonicalChannelRecord){0},
                 sizeof(state.records[0])) == 0);

    for (count = 1; count <= 2; count++) {
        reset_state();
        GXSetNumChans((u8)count);
        CHECK(raw_channels()->active_count == count);
        CHECK(raw_channels()->active_count_known == 1);
        CHECK(raw_channels()->invalid == 0);
        CHECK(!pc_gx_raw_channels_build_canonical(&state));
    }

    reset_state();
    GXSetNumChans(3);
    CHECK(raw_channels()->invalid == 1);
    CHECK(!pc_gx_raw_channels_build_canonical(&state));
    return 0;
}

static int test_combined_separate_partial_and_disabled_vtx(void) {
    AcgcGxCanonicalChannelState state;
    const PCGXRawChannelRecord* record0;
    const PCGXRawChannelRecord* record1;

    reset_state();
    GXSetNumChans(2);
    GXSetChanCtrl(
        GX_COLOR0,
        GX_FALSE,
        GX_SRC_REG,
        GX_SRC_REG,
        GX_LIGHT0,
        GX_DF_SIGN,
        GX_AF_NONE
    );
    GXSetChanCtrl(
        GX_ALPHA0,
        GX_TRUE,
        GX_SRC_VTX,
        GX_SRC_VTX,
        GX_LIGHT1,
        GX_DF_CLAMP,
        GX_AF_SPOT
    );
    GXSetChanCtrl(
        GX_COLOR1A1,
        GX_FALSE,
        GX_SRC_VTX,
        GX_SRC_VTX,
        0,
        GX_DF_NONE,
        GX_AF_NONE
    );

    /* COLOR0 owns RGB, then ALPHA0 owns A; the first call cannot invent A. */
    GXSetChanAmbColor(
        GX_COLOR0,
        fixture_color(0x11, 0x22, 0x33, 0x99)
    );
    record0 = &raw_channels()->records[0];
    CHECK(record0->ambient_known_mask ==
          (PC_GX_RAW_CHANNEL_COMPONENT_R |
           PC_GX_RAW_CHANNEL_COMPONENT_G |
           PC_GX_RAW_CHANNEL_COMPONENT_B));
    CHECK(record0->ambient_rgba8 == UINT32_C(0x00332211));
    GXSetChanAmbColor(
        GX_ALPHA0,
        fixture_color(0xAA, 0xBB, 0xCC, 0x44)
    );
    GXSetChanMatColor(
        GX_COLOR0A0,
        fixture_color(0x55, 0x66, 0x77, 0x88)
    );
    GXSetChanAmbColor(
        GX_COLOR1A1,
        fixture_color(0x91, 0x92, 0x93, 0x94)
    );
    GXSetChanMatColor(
        GX_COLOR1A1,
        fixture_color(0xA1, 0xA2, 0xA3, 0xA4)
    );

    CHECK(pc_gx_raw_channels_build_canonical(&state));
    CHECK(sizeof(state) == ACGC_GX_CANONICAL_CHANNEL_STATE_SIZE);
    CHECK(state.active_count == 2);
    CHECK(state.record_valid_mask == UINT32_C(0x3));
    CHECK(state.records[0].channel_index == 0);
    CHECK(state.records[0].ambient_rgba8 == UINT32_C(0x44332211));
    CHECK(state.records[0].material_rgba8 == UINT32_C(0x88776655));
    CHECK(state.records[0].color.enable == 0);
    CHECK(state.records[0].color.ambient_source ==
          ACGC_GX_CANONICAL_CHANNEL_SOURCE_REG);
    CHECK(state.records[0].alpha.enable == 1);
    CHECK(state.records[0].alpha.ambient_source ==
          ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX);
    CHECK(state.records[1].ambient_rgba8 == UINT32_C(0x94939291));
    CHECK(state.records[1].material_rgba8 == UINT32_C(0xA4A3A2A1));
    CHECK(state.records[1].color.material_source ==
          ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX);
    CHECK(state.records[1].alpha.material_source ==
          ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX);
    CHECK(acgc_gx_canonical_channel_state_validate(&state));

    record1 = &raw_channels()->records[1];
    CHECK(record1->color.known_mask ==
          PC_GX_RAW_CHANNEL_CONTROL_KNOWN_ALL);
    CHECK(record1->alpha.known_mask ==
          PC_GX_RAW_CHANNEL_CONTROL_KNOWN_ALL);
    return 0;
}

static int test_count_changes_preserve_private_records(void) {
    AcgcGxCanonicalChannelState state;
    PCGXRawChannelRecord saved_records[PC_GX_RAW_CHANNEL_RECORD_COUNT];

    reset_state();
    fill_record0();
    GXSetNumChans(2);
    fill_record1();
    memcpy(saved_records, raw_channels()->records, sizeof(saved_records));

    GXSetNumChans(0);
    CHECK(raw_channels()->active_count == 0);
    CHECK(memcmp(raw_channels()->records, saved_records,
                 sizeof(saved_records)) == 0);
    CHECK(pc_gx_raw_channels_build_canonical(&state));
    CHECK(state.active_count == 0);
    CHECK(state.record_valid_mask == 0);
    CHECK(acgc_gx_canonical_channel_state_validate(&state));
    CHECK(memcmp(&state.records[0], &(AcgcGxCanonicalChannelRecord){0},
                 sizeof(state.records[0])) == 0);
    CHECK(memcmp(&state.records[1], &(AcgcGxCanonicalChannelRecord){0},
                 sizeof(state.records[1])) == 0);

    GXSetNumChans(1);
    CHECK(memcmp(raw_channels()->records, saved_records,
                 sizeof(saved_records)) == 0);
    CHECK(pc_gx_raw_channels_build_canonical(&state));
    CHECK(state.active_count == 1);
    CHECK(state.record_valid_mask == 1);
    CHECK(memcmp(&state.records[1], &(AcgcGxCanonicalChannelRecord){0},
                 sizeof(state.records[1])) == 0);
    CHECK(state.records[0].ambient_rgba8 == saved_records[0].ambient_rgba8);
    CHECK(state.records[0].material_rgba8 == saved_records[0].material_rgba8);
    CHECK(raw_channels()->records[0].color.known_mask ==
          PC_GX_RAW_CHANNEL_CONTROL_KNOWN_ALL);
    CHECK(raw_channels()->records[0].alpha.known_mask ==
          PC_GX_RAW_CHANNEL_CONTROL_KNOWN_ALL);

    GXSetNumChans(2);
    CHECK(memcmp(raw_channels()->records, saved_records,
                 sizeof(saved_records)) == 0);
    CHECK(pc_gx_raw_channels_build_canonical(&state));
    CHECK(state.active_count == 2);
    CHECK(state.record_valid_mask == UINT32_C(0x3));
    CHECK(state.records[1].ambient_rgba8 == saved_records[1].ambient_rgba8);
    CHECK(state.records[1].material_rgba8 == saved_records[1].material_rgba8);
    CHECK(raw_channels()->records[1].color.known_mask ==
          PC_GX_RAW_CHANNEL_CONTROL_KNOWN_ALL);
    CHECK(raw_channels()->records[1].alpha.known_mask ==
          PC_GX_RAW_CHANNEL_CONTROL_KNOWN_ALL);
    return 0;
}

static int test_equal_legacy_values_establish_raw_truth(void) {
    AcgcGxCanonicalChannelState state;
    uint32_t dirty_before;

    reset_state();
    GXSetNumChans(1);
    dirty_before = g_gx.dirty;
    GXSetChanCtrl(
        GX_COLOR0A0,
        GX_FALSE,
        GX_SRC_REG,
        GX_SRC_REG,
        0,
        GX_DF_NONE,
        GX_AF_SPEC
    );
    GXSetChanAmbColor(GX_COLOR0A0, fixture_color(0, 0, 0, 0));
    GXSetChanMatColor(GX_COLOR0A0, fixture_color(0, 0, 0, 0));
    CHECK(g_gx.dirty == dirty_before);
    CHECK(raw_channels()->records[0].color.known_mask ==
          PC_GX_RAW_CHANNEL_CONTROL_KNOWN_ALL);
    CHECK(raw_channels()->records[0].alpha.known_mask ==
          PC_GX_RAW_CHANNEL_CONTROL_KNOWN_ALL);
    CHECK(raw_channels()->records[0].ambient_known_mask ==
          PC_GX_RAW_CHANNEL_COMPONENT_ALL);
    CHECK(raw_channels()->records[0].material_known_mask ==
          PC_GX_RAW_CHANNEL_COMPONENT_ALL);
    CHECK(pc_gx_raw_channels_build_canonical(&state));
    CHECK(state.records[0].alpha.attenuation_function ==
          ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_SPEC);
    return 0;
}

static int test_invalid_domains_and_sticky_failure(void) {
    AcgcGxCanonicalChannelState state;
    AcgcGxCanonicalChannelState sentinel;

    reset_state();
    memset(&sentinel, 0xA5, sizeof(sentinel));
    state = sentinel;
    GXSetChanCtrl(
        GX_COLOR0A0,
        GX_FALSE,
        GX_SRC_REG,
        GX_SRC_REG,
        GX_LIGHT0 | GX_LIGHT7 | GX_MAX_LIGHT,
        GX_DF_NONE,
        GX_AF_NONE
    );
    CHECK(raw_channels()->invalid == 1);
    CHECK(!pc_gx_raw_channels_build_canonical(&state));
    CHECK(memcmp(&state, &sentinel, sizeof(state)) == 0);

    reset_state();
    pc_gx_raw_channels_set_control(
        GX_COLOR0A0, 2, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE
    );
    CHECK(raw_channels()->invalid == 1);

    reset_state();
    pc_gx_raw_channels_set_control(
        GX_COLOR0A0, 0, 2, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE
    );
    CHECK(raw_channels()->invalid == 1);

    reset_state();
    pc_gx_raw_channels_set_control(
        GX_COLOR0A0, 0, GX_SRC_REG, 2, 0, GX_DF_NONE, GX_AF_NONE
    );
    CHECK(raw_channels()->invalid == 1);

    reset_state();
    pc_gx_raw_channels_set_control(
        GX_COLOR0A0, 0, GX_SRC_REG, GX_SRC_REG, 0, 3, GX_AF_NONE
    );
    CHECK(raw_channels()->invalid == 1);

    reset_state();
    pc_gx_raw_channels_set_control(
        GX_COLOR0A0, 0, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, 3
    );
    CHECK(raw_channels()->invalid == 1);

    reset_state();
    pc_gx_raw_channels_set_control(
        GX_COLOR0A0, 0, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_SIGN, GX_AF_SPEC
    );
    CHECK(raw_channels()->invalid == 1);

    reset_state();
    GXSetChanCtrl(
        GX_COLOR_NULL,
        GX_FALSE,
        GX_SRC_REG,
        GX_SRC_REG,
        0,
        GX_DF_NONE,
        GX_AF_NONE
    );
    CHECK(raw_channels()->invalid == 1);

    reset_state();
    fill_record0();
    pc_gx_raw_channels_set_control(
        GX_COLOR0A0, 2, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE
    );
    CHECK(raw_channels()->invalid == 1);
    GXSetNumChans(1);
    GXSetChanAmbColor(GX_COLOR0A0, fixture_color(1, 2, 3, 4));
    CHECK(raw_channels()->invalid == 1);
    CHECK(!pc_gx_raw_channels_build_canonical(&state));

    /* A fresh host-init reset is the only production recovery path. */
    reset_state();
    CHECK(raw_channels()->invalid == 0);
    CHECK(raw_channels()->active_count_known == 0);
    return 0;
}

static int test_old_batch_is_observed_before_new_state(void) {
    ChannelsFlushObservation observation;

    reset_state();
    fill_record0();
    g_gx.dirty = 0;
    begin_observed_batch(&observation);
    GXSetChanCtrl(
        GX_COLOR0,
        GX_TRUE,
        GX_SRC_REG,
        GX_SRC_REG,
        GX_LIGHT0,
        GX_DF_SIGN,
        GX_AF_NONE
    );
    finish_observed_batch();

    CHECK(observation.calls == 1);
    CHECK(observation.in_begin == 0);
    CHECK(observation.current_vertex_idx == 1);
    CHECK(observation.pending_verts == 0);
    CHECK(observation.before.active_count == 1);
    CHECK(observation.before.invalid == 0);
    CHECK(observation.before.records[0].color.enable == 0);
    CHECK(observation.before.records[0].color.light_mask == 0);
    CHECK(raw_channels()->records[0].color.enable == 1);
    CHECK(raw_channels()->records[0].color.light_mask == GX_LIGHT0);
    CHECK(g_gx.pending_verts == 1);
    return 0;
}

static void prepare_channel_envelope(AcgcGxCanonicalEnvelope* envelope) {
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    (void)acgc_gx_canonical_envelope_init(envelope);
    envelope->header.present_state_mask =
        ACGC_GX_CANONICAL_CHANNEL_SECTION_MASK;
    envelope->header.required_state_mask =
        ACGC_GX_CANONICAL_CHANNEL_SECTION_MASK;
    envelope->header.payload_byte_size =
        ACGC_GX_CANONICAL_CHANNEL_SECTION_BYTE_SIZE;
    envelope->header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET +
        ACGC_GX_CANONICAL_CHANNEL_SECTION_BYTE_SIZE;
    entry = &envelope->directory[
        ACGC_GX_CANONICAL_CHANNEL_SECTION_ID - 1];
    entry->section_version = ACGC_GX_CANONICAL_CHANNEL_SECTION_VERSION;
    entry->byte_offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    entry->byte_size = ACGC_GX_CANONICAL_CHANNEL_SECTION_BYTE_SIZE;
    entry->count = ACGC_GX_CANONICAL_CHANNEL_SECTION_COUNT;
    entry->capacity = ACGC_GX_CANONICAL_CHANNEL_SECTION_CAPACITY;
    entry->valid_mask = ACGC_GX_CANONICAL_CHANNEL_SECTION_MASK;
}

static int test_final_state_and_exact_envelope(void) {
    AcgcGxCanonicalChannelState state;
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    reset_state();
    fill_record0();
    CHECK(pc_gx_raw_channels_build_canonical(&state));
    CHECK(sizeof(state) == 136);
    CHECK(acgc_gx_canonical_channel_state_validate(&state));

    prepare_channel_envelope(&envelope);
    CHECK(acgc_gx_canonical_channel_metadata_validate(
        &envelope,
        envelope.header.total_byte_size
    ));
    entry = &envelope.directory[
        ACGC_GX_CANONICAL_CHANNEL_SECTION_ID - 1];
    CHECK(entry->valid_mask == ACGC_GX_CANONICAL_CHANNEL_SECTION_MASK);
    entry->valid_mask = UINT32_C(2);
    CHECK(!acgc_gx_canonical_channel_metadata_validate(
        &envelope,
        envelope.header.total_byte_size
    ));
    return 0;
}

int main(void) {
    if (test_initial_unknownness_and_counts() != 0 ||
        test_combined_separate_partial_and_disabled_vtx() != 0 ||
        test_count_changes_preserve_private_records() != 0 ||
        test_equal_legacy_values_establish_raw_truth() != 0 ||
        test_invalid_domains_and_sticky_failure() != 0 ||
        test_old_batch_is_observed_before_new_state() != 0 ||
        test_final_state_and_exact_envelope() != 0) {
        return 1;
    }

    puts("pc GX raw Channels shadow fixture: PASS");
    puts("proof boundary: setter-owned CPU Channels provenance, exact canonical validator handoff, and flush ordering only; no renderer, Metal, device, pixel, or playability claim");
    return 0;
}
