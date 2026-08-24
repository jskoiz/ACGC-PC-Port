#include "pc_gx_cumulative_gatherer.h"
#include "pc_gx_internal.h"
#include "pc_gx_texture_raw_state.h"
#include "pc_settings.h"
#include "pc_texture_pack.h"

#include "acgc/gx_canonical_state.h"
#include "acgc/gx_semantic_packet.h"

#include <dolphin/gx/GXEnum.h>
#include <dolphin/gx/GXStruct.h>
#include <dolphin/gx/GXGeometry.h>
#include <dolphin/gx/GXVert.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int g_pc_window_w = PC_SCREEN_WIDTH;
int g_pc_window_h = PC_SCREEN_HEIGHT;
int g_pc_widescreen_stretch = 0;
int g_pc_model_viewer_no_cull = 0;

PCSettings g_pc_settings = {
    .texture_filtering = 1
};

int pc_texture_pack_active(void) {
    return 0;
}

GLuint pc_texture_pack_lookup(
    const void* data,
    int data_size,
    int width,
    int height,
    unsigned int format,
    const void* tlut_data,
    int tlut_entries,
    int tlut_is_be,
    int* out_width,
    int* out_height
) {
    (void)data;
    (void)data_size;
    (void)width;
    (void)height;
    (void)format;
    (void)tlut_data;
    (void)tlut_entries;
    (void)tlut_is_be;
    (void)out_width;
    (void)out_height;
    return 0;
}

void pc_gx_tev_seq_reset(void) {
}

void pc_gx_tev_init(void) {
}

void pc_gx_tev_shutdown(void) {
}

static PCGXShaderVariant s_fixture_shader_variant;

PCGXShaderVariant* pc_gx_tev_get_variant(void) {
    return &s_fixture_shader_variant;
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct FlushObservation {
    int cumulative_callbacks;
    int cumulative_envelope_valid;
    int cumulative_borrow_active;
    int cumulative_registration_rejected;
    int cumulative_clear_rejected;
    int cumulative_nested_gather_rejected;
    int cumulative_lifecycle_init_preserved;
    int cumulative_lifecycle_shutdown_preserved;
    int cumulative_lifecycle_borrow_active;
    int cumulative_lifecycle_nested_gather_rejected;
    size_t cumulative_byte_size;
    int old_texture_callbacks;
    int geometry_calls;
    int geometry_known;
    int geometry_invalid;
    int geometry_vertex_count;
    int geometry_in_begin;
    int semantic_calls;
    int semantic_valid;
    int resource_callbacks;
    int resource_borrow_active;
    int resource_attempt_id_valid;
    int resource_registration_rejected;
} FlushObservation;

static PCGXCumulativeSnapshotStorage s_nested_storage;
static uint8_t s_callback_envelope_copy[PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES];

static uint32_t read_le32(const uint8_t* source) {
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) |
        ((uint32_t)source[3] << 24);
}

static int envelope_is_valid(
    const uint8_t* envelope,
    size_t envelope_byte_size
) {
    size_t payload_offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    uint32_t index;

    if (envelope == NULL ||
        envelope_byte_size < payload_offset ||
        !acgc_gx_canonical_envelope_validate(
            (const AcgcGxCanonicalEnvelope*)envelope,
            envelope_byte_size
        ) ||
        read_le32(envelope) != ACGC_GX_CANONICAL_ENVELOPE_MAGIC ||
        read_le32(envelope + 16) !=
            ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT) {
        return 0;
    }

    for (index = 0; index < PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT; index++) {
        size_t directory_offset =
            ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET +
            index * ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE;
        size_t section_offset = read_le32(envelope + directory_offset + 8);
        size_t section_size = read_le32(envelope + directory_offset + 12);

        if (read_le32(envelope + directory_offset) != index + 1 ||
            read_le32(envelope + directory_offset + 4) != 1 ||
            section_offset != payload_offset ||
            section_size == 0 ||
            (section_size % sizeof(uint32_t)) != 0 ||
            section_offset > envelope_byte_size ||
            section_size > envelope_byte_size - section_offset) {
            return 0;
        }
        payload_offset += section_size;
    }
    return payload_offset == envelope_byte_size;
}

static void observe_cumulative_snapshot(
    void* context,
    const uint8_t* envelope,
    size_t envelope_byte_size
) {
    FlushObservation* observation = (FlushObservation*)context;

    observation->cumulative_callbacks++;
    observation->cumulative_byte_size = envelope_byte_size;
    observation->cumulative_envelope_valid = envelope_is_valid(
        envelope,
        envelope_byte_size
    );

    if (observation->cumulative_envelope_valid &&
        envelope_byte_size <= sizeof(s_callback_envelope_copy)) {
        memcpy(s_callback_envelope_copy, envelope, envelope_byte_size);

        /* These lifecycle calls are forbidden by the public contract.  They
         * must nevertheless fail closed if an accidental callback attempts
         * them, preserving both the live envelope and the gather guard. */
        pc_gx_init();
        observation->cumulative_lifecycle_init_preserved =
            memcmp(
                s_callback_envelope_copy,
                envelope,
                envelope_byte_size
            ) == 0 && envelope_is_valid(envelope, envelope_byte_size);
        pc_gx_shutdown();
        observation->cumulative_lifecycle_shutdown_preserved =
            memcmp(
                s_callback_envelope_copy,
                envelope,
                envelope_byte_size
            ) == 0 && envelope_is_valid(envelope, envelope_byte_size);
    }
    observation->cumulative_lifecycle_borrow_active =
        pc_gx_texture_raw_borrow_is_active();
    observation->cumulative_lifecycle_nested_gather_rejected =
        pc_gx_cumulative_snapshot_gather(
            &g_gx.raw_geometry.completed,
            &s_nested_storage
        ) == 0;
    observation->cumulative_borrow_active =
        pc_gx_texture_raw_borrow_is_active();
    observation->cumulative_registration_rejected =
        pc_gx_set_cumulative_snapshot_callback(
            observe_cumulative_snapshot,
            observation
        ) == 0;
    observation->cumulative_clear_rejected =
        pc_gx_clear_cumulative_snapshot_callback() == 0;
    observation->cumulative_nested_gather_rejected =
        pc_gx_cumulative_snapshot_gather(
            &g_gx.raw_geometry.completed,
            &s_nested_storage
        ) == 0;
}

static void observe_old_texture_dynamic_snapshot(
    void* context,
    const AcgcGxCanonicalTextureState* texture,
    const AcgcGxCanonicalDynamicState* dynamic,
    const PCGXTextureDynamicLease* lease
) {
    FlushObservation* observation = (FlushObservation*)context;

    (void)texture;
    (void)dynamic;
    (void)lease;
    observation->old_texture_callbacks++;
}

static int observe_canonical_resources(
    void* context,
    uint64_t attempt_id,
    const uint8_t* envelope,
    size_t envelope_byte_size,
    const AcgcGxCanonicalTextureState* texture,
    const AcgcGxCanonicalDynamicState* dynamic,
    const PCGXTextureDynamicLease* lease
) {
    FlushObservation* observation = (FlushObservation*)context;

    observation->resource_callbacks++;
    observation->resource_borrow_active =
        pc_gx_texture_raw_borrow_is_active();
    observation->resource_attempt_id_valid = attempt_id != 0;
    observation->resource_registration_rejected =
        pc_gx_set_cumulative_snapshot_resource_callback(
            observe_canonical_resources,
            observation
        ) == 0;
    if (envelope == NULL || envelope_byte_size == 0 || texture == NULL ||
        dynamic == NULL || lease == NULL) {
        return 0;
    }
    return 1;
}

static void observe_geometry_flush(void* context) {
    FlushObservation* observation = (FlushObservation*)context;
    const PCGXRawGeometryBatch* completed = &g_gx.raw_geometry.completed;

    observation->geometry_calls++;
    observation->geometry_known = completed->known;
    observation->geometry_invalid = completed->invalid;
    observation->geometry_vertex_count = (int)completed->vertex_count;
    observation->geometry_in_begin = g_gx.in_begin;
}

static void observe_semantic_packet(
    void* context,
    const AcgcGxSemanticPacket* packet
) {
    FlushObservation* observation = (FlushObservation*)context;

    observation->semantic_calls++;
    if (packet != NULL && acgc_gx_semantic_packet_validate(packet)) {
        observation->semantic_valid++;
    }
}

static void initialize_raw_state(void) {
    uint32_t index;

    memset(&g_gx, 0, sizeof(g_gx));
    memset(&s_nested_storage, 0, sizeof(s_nested_storage));
    memset(&s_fixture_shader_variant, 0, sizeof(s_fixture_shader_variant));

    /* Canonical raw state is initialized exactly as the accepted gatherer
     * fixture does; the vertex batch below is captured through GXBegin/GXEnd. */
    g_gx.raw_transform.projection.type = GX_PERSPECTIVE;
    g_gx.raw_transform.projection.known = 1;
    g_gx.raw_transform.projection.coefficients[0] =
        UINT32_C(0x3F800000);
    g_gx.raw_transform.current_position_id = 0;
    g_gx.raw_transform.current_position_known = 1;
    g_gx.raw_transform.position[0].known = 1;
    g_gx.raw_transform.position[0].words[0] = UINT32_C(0x3F800000);
    g_gx.raw_transform.position[0].words[5] = UINT32_C(0x3F800000);
    g_gx.raw_transform.position[0].words[10] = UINT32_C(0x3F800000);

    pc_gx_raw_channels_initialize();
    pc_gx_raw_channels_set_num(0);
    pc_gx_raw_lighting_initialize();

    pc_gx_raw_texgen_shadow_reset_fixture();
    g_gx.raw_texgen.active_texgen_count = 0;
    g_gx.raw_texgen.active_texgen_count_known = 1;

    memset(&g_gx.raw_tev_indirect, 0, sizeof(g_gx.raw_tev_indirect));
    g_gx.raw_tev_indirect.active_tev_stage_count = 1;
    g_gx.raw_tev_indirect.active_tev_stage_count_known = 1;
    g_gx.raw_tev_indirect.stages[0].known_mask =
        PC_GX_RAW_TEV_STAGE_KNOWN_MASK;
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_REGISTER_COUNT; index++) {
        g_gx.raw_tev_indirect.registers[index].valid = 1;
        g_gx.raw_tev_indirect.registers[index].source =
            PCGX_TEV_RAW_SOURCE_COLOR_U8;
        g_gx.raw_tev_indirect.registers[index].known_mask =
            PC_GX_RAW_TEV_COMPONENT_KNOWN_MASK;
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_KONST_COUNT; index++) {
        g_gx.raw_tev_indirect.konst[index].valid = 1;
        g_gx.raw_tev_indirect.konst[index].source =
            PCGX_TEV_RAW_SOURCE_KCOLOR_U8;
        g_gx.raw_tev_indirect.konst[index].known_mask =
            PC_GX_RAW_TEV_COMPONENT_KNOWN_MASK;
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_SWAP_TABLE_COUNT; index++) {
        g_gx.raw_tev_indirect.swap_tables[index].known_mask =
            PC_GX_RAW_TEV_RECORD_KNOWN_MASK;
    }
    g_gx.raw_tev_indirect.active_indirect_stage_count = 0;
    g_gx.raw_tev_indirect.active_indirect_stage_count_known = 1;

    memset(&g_gx.raw_blend, 0, sizeof(g_gx.raw_blend));
    g_gx.raw_blend.known = 1;
    memset(&g_gx.raw_alpha, 0, sizeof(g_gx.raw_alpha));
    g_gx.raw_alpha.known_mask = PC_GX_RAW_ALPHA_KNOWN_ALL;
    memset(&g_gx.raw_depth, 0, sizeof(g_gx.raw_depth));
    g_gx.raw_depth.known = 1;
    memset(&g_gx.raw_raster, 0, sizeof(g_gx.raw_raster));
    g_gx.raw_raster.known_mask = PC_GX_RAW_RASTER_KNOWN_ALL;
    g_gx.raw_raster.line_texcoord_known_mask =
        PC_GX_RAW_RASTER_TEXCOORD_KNOWN_ALL;
    g_gx.raw_raster.point_texcoord_known_mask =
        PC_GX_RAW_RASTER_TEXCOORD_KNOWN_ALL;
    memset(&g_gx.raw_fog, 0, sizeof(g_gx.raw_fog));
    g_gx.raw_fog.known_mask = PC_GX_RAW_FOG_KNOWN_ALL;
    pc_gx_texture_raw_initialize();

    /* The pre-existing v1 semantic handoff is intentionally made valid so
     * the fixture proves that CPU-only continuation remains in order. */
    g_gx.current_mtx = 0;
    g_gx.projection_type = GX_PERSPECTIVE;
    g_gx.num_chans = 0;
    g_gx.num_tex_gens = 0;
    g_gx.num_tev_stages = 1;
    g_gx.num_ind_stages = 0;
    g_gx.fog_type = GX_FOG_NONE;
    g_gx.alpha_comp0 = GX_ALWAYS;
    g_gx.alpha_comp1 = GX_ALWAYS;
    g_gx.alpha_op = GX_AOP_AND;
    g_gx.alpha_ref0 = 0;
    g_gx.alpha_ref1 = 0;
    g_gx.cull_mode = GX_CULL_NONE;
    g_gx.z_compare_enable = 1;
    g_gx.z_compare_func = GX_LEQUAL;
    g_gx.z_update_enable = 1;
    g_gx.color_update_enable = 1;
    g_gx.alpha_update_enable = 1;
    g_gx.blend_mode = GX_BM_NONE;
    g_gx.blend_src = GX_BL_ONE;
    g_gx.blend_dst = GX_BL_ZERO;
    g_gx.blend_logic_op = GX_LO_CLEAR;

    for (index = 0; index < 4; index++) {
        g_gx.projection_mtx[index][index] = 1.0f;
    }
    for (index = 0; index < 3; index++) {
        g_gx.pos_mtx[0][index][index] = 1.0f;
        g_gx.nrm_mtx[0][index][index] = 1.0f;
    }

    g_gx.tev_stages[0].color_a = GX_CC_ZERO;
    g_gx.tev_stages[0].color_b = GX_CC_ZERO;
    g_gx.tev_stages[0].color_c = GX_CC_ZERO;
    g_gx.tev_stages[0].color_d = GX_CC_RASC;
    g_gx.tev_stages[0].alpha_a = GX_CA_ZERO;
    g_gx.tev_stages[0].alpha_b = GX_CA_ZERO;
    g_gx.tev_stages[0].alpha_c = GX_CA_ZERO;
    g_gx.tev_stages[0].alpha_d = GX_CA_RASA;
    g_gx.tev_stages[0].color_op = GX_TEV_ADD;
    g_gx.tev_stages[0].color_bias = GX_TB_ZERO;
    g_gx.tev_stages[0].color_scale = GX_CS_SCALE_1;
    g_gx.tev_stages[0].color_clamp = GX_TRUE;
    g_gx.tev_stages[0].color_out = GX_TEVPREV;
    g_gx.tev_stages[0].alpha_op = GX_TEV_ADD;
    g_gx.tev_stages[0].alpha_bias = GX_TB_ZERO;
    g_gx.tev_stages[0].alpha_scale = GX_CS_SCALE_1;
    g_gx.tev_stages[0].alpha_clamp = GX_TRUE;
    g_gx.tev_stages[0].alpha_out = GX_TEVPREV;
    g_gx.tev_stages[0].tex_coord = GX_TEXCOORD_NULL;
    g_gx.tev_stages[0].tex_map = GX_TEXMAP_NULL;
    g_gx.tev_stages[0].color_chan = GX_COLOR0A0;
    g_gx.tev_stages[0].ras_swap = GX_TEV_SWAP0;
    g_gx.tev_stages[0].tex_swap = GX_TEV_SWAP0;
}

static void reset_observation(FlushObservation* observation) {
    memset(observation, 0, sizeof(*observation));
}

static void configure_direct_position(void) {
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
}

static void emit_position_triangle(unsigned int count) {
    if (count > 0) GXPosition3f32(1.0f, 2.0f, 3.0f);
    if (count > 1) GXPosition3f32(4.0f, 5.0f, 6.0f);
    if (count > 2) GXPosition3f32(7.0f, 8.0f, 9.0f);
}

static void install_flush_observers(FlushObservation* observation) {
    pc_gx_set_semantic_packet_handoff(
        observe_semantic_packet,
        observation
    );
    pc_gx_set_geometry_flush_fixture_observer(
        observe_geometry_flush,
        observation
    );
}

static void clear_flush_observers(void) {
    pc_gx_clear_semantic_packet_handoff();
    pc_gx_clear_geometry_flush_fixture_observer();
    pc_gx_clear_texture_dynamic_snapshot_callback();
    pc_gx_clear_cumulative_snapshot_callback();
    pc_gx_clear_cumulative_snapshot_resource_callback();
}

static int prime_old_callback(FlushObservation* observation) {
    pc_gx_set_texture_dynamic_snapshot_callback(
        observe_old_texture_dynamic_snapshot,
        observation
    );
    CHECK(pc_gx_try_texture_dynamic_snapshot() == 1);
    CHECK(observation->old_texture_callbacks == 1);
    observation->old_texture_callbacks = 0;
    return 0;
}

static int test_registered_flush_and_no_duplicate_publication(void) {
    FlushObservation observation;
    size_t first_byte_size;

    clear_flush_observers();
    initialize_raw_state();
    reset_observation(&observation);
    CHECK(prime_old_callback(&observation) == 0);
    install_flush_observers(&observation);
    CHECK(pc_gx_set_cumulative_snapshot_callback(
        observe_cumulative_snapshot,
        &observation
    ));
    CHECK(pc_gx_set_cumulative_snapshot_resource_callback(
        observe_canonical_resources,
        &observation
    ));

    configure_direct_position();
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    emit_position_triangle(3);
    GXEnd();

    CHECK(observation.cumulative_callbacks == 1);
    CHECK(observation.cumulative_envelope_valid);
    CHECK(observation.cumulative_borrow_active);
    CHECK(observation.cumulative_registration_rejected);
    CHECK(observation.cumulative_clear_rejected);
    CHECK(observation.cumulative_nested_gather_rejected);
    CHECK(observation.cumulative_lifecycle_init_preserved);
    CHECK(observation.cumulative_lifecycle_shutdown_preserved);
    CHECK(observation.cumulative_lifecycle_borrow_active);
    CHECK(observation.cumulative_lifecycle_nested_gather_rejected);
    CHECK(observation.cumulative_byte_size != 0);
    CHECK(observation.geometry_calls == 1);
    CHECK(observation.geometry_known == 1);
    CHECK(observation.geometry_invalid == 0);
    CHECK(observation.geometry_vertex_count == 3);
    CHECK(observation.geometry_in_begin == 0);
    CHECK(observation.semantic_calls == 1);
    CHECK(observation.semantic_valid == 1);
    CHECK(observation.resource_callbacks == 1);
    CHECK(observation.resource_borrow_active);
    CHECK(observation.resource_attempt_id_valid);
    CHECK(observation.resource_registration_rejected);
    CHECK(observation.old_texture_callbacks == 0);
    CHECK(!pc_gx_texture_raw_borrow_is_active());
    first_byte_size = observation.cumulative_byte_size;

    /* Reuse the same production storage for one more real GXBegin/GXEnd
     * boundary; exactly one cumulative publication is observed per flush. */
    configure_direct_position();
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    emit_position_triangle(3);
    GXEnd();

    CHECK(observation.cumulative_callbacks == 2);
    CHECK(observation.cumulative_byte_size == first_byte_size);
    CHECK(observation.geometry_calls == 2);
    CHECK(observation.semantic_calls == 2);
    CHECK(observation.resource_callbacks == 2);
    CHECK(observation.resource_borrow_active);
    CHECK(observation.resource_attempt_id_valid);
    CHECK(observation.old_texture_callbacks == 0);
    CHECK(!pc_gx_texture_raw_borrow_is_active());

    /* A normal lifecycle boundary clears both the callback and its context.
     * The fixture-safe init path re-establishes CPU state but stops before GL;
     * no cumulative callback is registered in the new GX lifetime. */
    pc_gx_shutdown();
    pc_gx_init();
    {
        FlushObservation post_lifecycle_observation;

        initialize_raw_state();
        reset_observation(&post_lifecycle_observation);
        install_flush_observers(&post_lifecycle_observation);

        configure_direct_position();
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        emit_position_triangle(3);
        GXEnd();

        CHECK(post_lifecycle_observation.cumulative_callbacks == 0);
        CHECK(observation.cumulative_callbacks == 2);
        CHECK(post_lifecycle_observation.geometry_calls == 1);
        CHECK(post_lifecycle_observation.semantic_calls == 1);
        CHECK(post_lifecycle_observation.semantic_valid == 1);
        CHECK(!pc_gx_texture_raw_borrow_is_active());
    }
    clear_flush_observers();
    return 0;
}

static int test_unregistered_path_continues(void) {
    FlushObservation observation;

    clear_flush_observers();
    initialize_raw_state();
    reset_observation(&observation);
    CHECK(prime_old_callback(&observation) == 0);
    install_flush_observers(&observation);

    configure_direct_position();
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    emit_position_triangle(3);
    GXEnd();

    CHECK(observation.cumulative_callbacks == 0);
    CHECK(observation.geometry_calls == 1);
    CHECK(observation.geometry_known == 1);
    CHECK(observation.semantic_calls == 1);
    CHECK(observation.semantic_valid == 1);
    CHECK(observation.old_texture_callbacks == 0);
    CHECK(!pc_gx_texture_raw_borrow_is_active());
    clear_flush_observers();
    return 0;
}

static int test_source_backed_gather_failure_continues(void) {
    FlushObservation observation;

    clear_flush_observers();
    initialize_raw_state();
    reset_observation(&observation);
    CHECK(prime_old_callback(&observation) == 0);
    install_flush_observers(&observation);
    CHECK(pc_gx_set_cumulative_snapshot_callback(
        observe_cumulative_snapshot,
        &observation
    ));

    configure_direct_position();
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    emit_position_triangle(2);
    GXEnd();

    /* GXEnd really captured a completed but invalid batch (count != declared
     * count); the observer after the gather proves the legacy CPU continuation
     * still ran, while the test-only boundary prevents any GL call. */
    CHECK(observation.cumulative_callbacks == 0);
    CHECK(observation.geometry_calls == 1);
    CHECK(observation.geometry_known == 0);
    CHECK(observation.geometry_invalid != 0);
    CHECK(observation.old_texture_callbacks == 0);
    CHECK(!pc_gx_texture_raw_borrow_is_active());
    clear_flush_observers();
    return 0;
}

int main(void) {
    CHECK(test_registered_flush_and_no_duplicate_publication() == 0);
    CHECK(test_unregistered_path_continues() == 0);
    CHECK(test_source_backed_gather_failure_continues() == 0);

    puts("pc GX cumulative gatherer flush fixture: PASS");
    puts("proof boundary: source-backed GXBegin/GXEnd completed Geometry, one cumulative publication per flush, old Texture/Dynamic callback suppression, semantic/fixture continuation, and fail-closed gather failure; no GL context, renderer, Metal, device, pixels, assets, or playability claim");
    return 0;
}
