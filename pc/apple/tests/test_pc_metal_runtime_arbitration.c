#include "acgc/apple_canonical_plan_handoff.h"
#include "acgc/gx_semantic_packet.h"
#include "acgc/metal_packet_consumer.h"
#include "acgc/metal_sink.h"
#include "acgc/pc_metal_runtime.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 0; \
    } \
} while (0)

typedef void (*TestCumulativeSnapshotCallback)(
    void* context,
    const uint8_t* envelope,
    size_t envelope_byte_size
);
typedef void (*TestCumulativeSnapshotAttemptCallback)(
    void* context,
    uint64_t attempt_id,
    int result
);
typedef void (*TestSemanticHandoffCallback)(
    void* context,
    const AcgcGxSemanticPacket* packet
);
typedef void (*TestSemanticV2HandoffCallback)(
    void* context,
    const AcgcGxSemanticPacketV2* packet
);
typedef void (*TestSemanticV3HandoffCallback)(
    void* context,
    const AcgcGxSemanticPacketV3* packet
);
typedef void (*TestSemanticV4HandoffCallback)(
    void* context,
    const AcgcGxSemanticPacketV4* packet
);

extern void pc_metal_runtime_consume_canonical_plan_fixture(
    uint64_t attempt_id,
    AcgcAppleCanonicalPlanHandoffResult result,
    const AcgcAppleCanonicalPlan* plan
);
extern void pc_metal_runtime_observe_output_fixture(
    const AcgcMetalPacketConsumerOutput* output,
    AcgcMetalPacketConsumerStatus status
);
extern int pc_metal_runtime_callback_active_fixture(void);
extern int pc_metal_runtime_runtime_callback_registered_fixture(void);
extern int pc_metal_runtime_source_provider_registered_fixture(void);
extern void pc_metal_runtime_inject_canonical_resource_stage_fixture(
    uint64_t attempt_id,
    int valid
);
extern void pc_metal_runtime_set_resource_registration_result_fixture(
    int result
);
extern int pc_metal_runtime_install_foreign_resource_callback_fixture(void);
extern int pc_metal_runtime_clear_foreign_resource_callback_fixture(void);
extern int pc_metal_runtime_resource_callback_owner_fixture(void);
extern uint32_t pc_metal_runtime_resource_register_count_fixture(void);
extern uint32_t pc_metal_runtime_resource_clear_count_fixture(void);
extern void pc_metal_runtime_reset_resource_registration_fixture(void);

static TestCumulativeSnapshotCallback s_cumulative_callback;
static TestCumulativeSnapshotAttemptCallback s_attempt_callback;
static void* s_cumulative_context;
static int s_cumulative_register_result = 1;
static int s_cumulative_clear_result = 1;
static uint32_t s_cumulative_register_count;
static uint32_t s_cumulative_clear_count;
static AcgcAppleCanonicalPlanStatus s_plan_build_status =
    ACGC_APPLE_CANONICAL_PLAN_OK;
static AcgcAppleCanonicalPlan s_plan;

static TestSemanticHandoffCallback s_semantic_callback;
static TestSemanticV2HandoffCallback s_semantic_v2_callback;
static TestSemanticV3HandoffCallback s_semantic_v3_callback;
static TestSemanticV4HandoffCallback s_semantic_v4_callback;
static void* s_semantic_context;
static void* s_semantic_v2_context;
static void* s_semantic_v3_context;
static void* s_semantic_v4_context;

static uint32_t s_sink_initialized;
static uint32_t s_sink_submit_count;
static uint32_t s_sink_completed_count;
static uint32_t s_sink_readback_count;
static AcgcMetalSinkStatus s_sink_status = ACGC_METAL_SINK_OK;
static AcgcMetalSinkStatus s_sink_last_status = ACGC_METAL_SINK_NOT_INITIALIZED;
static AcgcMetalSinkValidationReason s_sink_last_validation_reason =
    ACGC_METAL_SINK_VALIDATION_REASON_NONE;
static AcgcMetalPacketConsumerOutput s_last_sink_output;
static int s_sink_reenter;
static int s_reentry_init_attempted;
static int s_reentry_shutdown_attempted;
static int s_reentry_set_result;
static int s_reentry_clear_result;
static int s_nested_callback_active;
static int s_sink_internal_failure;
static int s_foreign_plan_consumer_calls;
static int s_foreign_plan_context;

enum {
    RESOURCE_OWNER_NONE = 0,
    RESOURCE_OWNER_RUNTIME = 1,
    RESOURCE_OWNER_FOREIGN = 2
};

static void noop_plan_consumer(
    void* context,
    uint64_t attempt_id,
    AcgcAppleCanonicalPlanHandoffResult result,
    const AcgcAppleCanonicalPlan* plan
) {
    (void)context;
    (void)attempt_id;
    (void)result;
    (void)plan;
}

static void foreign_plan_consumer(
    void* context,
    uint64_t attempt_id,
    AcgcAppleCanonicalPlanHandoffResult result,
    const AcgcAppleCanonicalPlan* plan
) {
    (void)attempt_id;
    (void)result;
    (void)plan;
    if (context == &s_foreign_plan_context) {
        s_foreign_plan_consumer_calls++;
    }
}

int pc_gx_set_cumulative_snapshot_callbacks(
    TestCumulativeSnapshotCallback callback,
    TestCumulativeSnapshotAttemptCallback attempt_callback,
    void* context
) {
    s_cumulative_register_count++;
    if (!s_cumulative_register_result || callback == NULL ||
        attempt_callback == NULL) {
        return 0;
    }
    s_cumulative_callback = callback;
    s_attempt_callback = attempt_callback;
    s_cumulative_context = context;
    return 1;
}

int pc_gx_clear_cumulative_snapshot_callbacks(void) {
    s_cumulative_clear_count++;
    if (!s_cumulative_clear_result) {
        return 0;
    }
    s_cumulative_callback = NULL;
    s_attempt_callback = NULL;
    s_cumulative_context = NULL;
    return 1;
}

AcgcAppleCanonicalPlanStatus acgc_apple_canonical_plan_build(
    const uint8_t* envelope_bytes,
    size_t envelope_byte_size,
    AcgcAppleCanonicalPlan* output
) {
    if (s_plan_build_status != ACGC_APPLE_CANONICAL_PLAN_OK) {
        return s_plan_build_status;
    }
    if (envelope_bytes == NULL || envelope_byte_size == 0 || output == NULL) {
        return ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT;
    }
    *output = s_plan;
    return ACGC_APPLE_CANONICAL_PLAN_OK;
}

void pc_gx_set_semantic_packet_handoff(
    TestSemanticHandoffCallback callback,
    void* context
) {
    s_semantic_callback = callback;
    s_semantic_context = context;
}

void pc_gx_clear_semantic_packet_handoff(void) {
    s_semantic_callback = NULL;
    s_semantic_context = NULL;
}

void pc_gx_set_semantic_packet_v2_handoff(
    TestSemanticV2HandoffCallback callback,
    void* context
) {
    s_semantic_v2_callback = callback;
    s_semantic_v2_context = context;
}

void pc_gx_clear_semantic_packet_v2_handoff(void) {
    s_semantic_v2_callback = NULL;
    s_semantic_v2_context = NULL;
}

void pc_gx_set_semantic_packet_v3_handoff(
    TestSemanticV3HandoffCallback callback,
    void* context
) {
    s_semantic_v3_callback = callback;
    s_semantic_v3_context = context;
}

void pc_gx_clear_semantic_packet_v3_handoff(void) {
    s_semantic_v3_callback = NULL;
    s_semantic_v3_context = NULL;
}

void pc_gx_set_semantic_packet_v4_handoff(
    TestSemanticV4HandoffCallback callback,
    void* context
) {
    s_semantic_v4_callback = callback;
    s_semantic_v4_context = context;
}

void pc_gx_clear_semantic_packet_v4_handoff(void) {
    s_semantic_v4_callback = NULL;
    s_semantic_v4_context = NULL;
}

AcgcMetalSinkStatus acgc_metal_sink_init(void) {
    s_sink_initialized = 1;
    s_sink_last_status = ACGC_METAL_SINK_OK;
    s_sink_last_validation_reason = ACGC_METAL_SINK_VALIDATION_REASON_NONE;
    return ACGC_METAL_SINK_OK;
}

void acgc_metal_sink_shutdown(void) {
    s_sink_initialized = 0;
    s_sink_last_status = ACGC_METAL_SINK_NOT_INITIALIZED;
    s_sink_last_validation_reason = ACGC_METAL_SINK_VALIDATION_REASON_NONE;
}

AcgcMetalSinkStatus acgc_metal_sink_submit(
    const AcgcMetalPacketConsumerOutput* output
) {
    if (!s_sink_initialized || output == NULL) {
        s_sink_last_status = ACGC_METAL_SINK_INVALID_OUTPUT;
        s_sink_last_validation_reason = output == NULL
            ? ACGC_METAL_SINK_VALIDATION_REASON_NULL_OR_SOURCE_KIND
            : ACGC_METAL_SINK_VALIDATION_REASON_NONE;
        return s_sink_last_status;
    }
    s_sink_submit_count++;
    s_last_sink_output = *output;
    if (s_sink_reenter) {
        AcgcAppleCanonicalPlanHandoffSnapshot handoff_snapshot;
        AcgcPcMetalRuntimeSnapshot runtime_snapshot;

        s_reentry_init_attempted = 1;
        pc_metal_runtime_init();
        s_reentry_shutdown_attempted = 1;
        pc_metal_runtime_shutdown();
        s_reentry_set_result = acgc_apple_canonical_plan_handoff_set_consumer(
            noop_plan_consumer,
            NULL
        );
        s_reentry_clear_result =
            acgc_apple_canonical_plan_handoff_clear_consumer();
        s_nested_callback_active = pc_metal_runtime_callback_active_fixture();
        pc_metal_runtime_consume_canonical_plan_fixture(
            0xFFFFFFFFFFFFFFF0ULL,
            ACGC_APPLE_CANONICAL_PLAN_HANDOFF_PLAN_PUBLISHED,
            &s_plan
        );
        pc_metal_runtime_get_snapshot(&runtime_snapshot);
        (void)runtime_snapshot;
        if (!acgc_apple_canonical_plan_handoff_get_snapshot(&handoff_snapshot) ||
            handoff_snapshot.consumer_registered != 1) {
            s_sink_internal_failure = 1;
        }
    }
    s_sink_last_status = s_sink_status;
    s_sink_last_validation_reason = ACGC_METAL_SINK_VALIDATION_REASON_NONE;
    if (s_sink_status == ACGC_METAL_SINK_OK) {
        s_sink_completed_count++;
        s_sink_readback_count++;
    }
    return s_sink_status;
}

void acgc_metal_sink_get_snapshot(AcgcMetalSinkSnapshot* snapshot) {
    if (snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->initialized = s_sink_initialized;
    snapshot->available = s_sink_initialized;
    snapshot->submit_count = s_sink_submit_count;
    snapshot->completed_count = s_sink_completed_count;
    snapshot->readback_count = s_sink_readback_count;
    snapshot->last_status = s_sink_last_status;
    snapshot->last_validation_reason = (uint32_t)s_sink_last_validation_reason;
}

static uint32_t bits_from_float(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int make_base_plan(AcgcAppleCanonicalPlan* plan) {
    uint32_t index;

    if (plan == NULL) {
        return 0;
    }
    memset(plan, 0, sizeof(*plan));
    plan->geometry.primitive = ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES;
    plan->geometry.vtxfmt = 0;
    plan->geometry.vertex_count = 3;
    plan->geometry.present_mask =
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0);
    plan->geometry.component_mask =
        ACGC_APPLE_CANONICAL_PLAN_COMPONENT_POSITION |
        ACGC_APPLE_CANONICAL_PLAN_COMPONENT_COLOR0;
    for (index = 0; index < 3; index++) {
        plan->geometry.vertices[index].present_mask = plan->geometry.present_mask;
        plan->geometry.vertices[index].component_mask =
            plan->geometry.component_mask;
        plan->geometry.vertices[index].position_matrix_id = 0;
    }
    plan->geometry.vertices[0].position[0] = bits_from_float(0.0f);
    plan->geometry.vertices[0].position[1] = bits_from_float(0.0f);
    plan->geometry.vertices[0].position[2] = bits_from_float(0.0f);
    plan->geometry.vertices[1].position[0] = bits_from_float(1.0f);
    plan->geometry.vertices[1].position[1] = bits_from_float(0.0f);
    plan->geometry.vertices[1].position[2] = bits_from_float(0.0f);
    plan->geometry.vertices[2].position[0] = bits_from_float(0.0f);
    plan->geometry.vertices[2].position[1] = bits_from_float(1.0f);
    plan->geometry.vertices[2].position[2] = bits_from_float(0.0f);
    plan->geometry.vertices[0].color_rgba8[0] = UINT32_C(0x44332211);
    plan->geometry.vertices[1].color_rgba8[0] = UINT32_C(0x88776655);
    plan->geometry.vertices[2].color_rgba8[0] = UINT32_C(0xCCBBAA99);

    plan->transform.projection_type =
        ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_ORTHOGRAPHIC;
    plan->transform.projection[0] = bits_from_float(1.0f);
    plan->transform.projection[1] = bits_from_float(0.0f);
    plan->transform.projection[2] = bits_from_float(1.0f);
    plan->transform.projection[3] = bits_from_float(0.0f);
    plan->transform.projection[4] = bits_from_float(1.0f);
    plan->transform.projection[5] = bits_from_float(0.0f);
    plan->transform.known_mask =
        ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_KNOWN_MASK |
        ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_KNOWN_MASK |
        ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(0);
    plan->transform.current_position_id = 0;
    plan->transform.position[0][0] = bits_from_float(1.0f);
    plan->transform.position[0][5] = bits_from_float(1.0f);
    plan->transform.position[0][10] = bits_from_float(1.0f);

    plan->channels.active_count = 1;
    plan->channels.record_valid_mask = 1;
    plan->channels.records[0].channel_index = 0;
    plan->channels.records[0].color.enable =
        ACGC_GX_CANONICAL_CHANNEL_BOOLEAN_FALSE;
    plan->channels.records[0].color.ambient_source =
        ACGC_GX_CANONICAL_CHANNEL_SOURCE_REG;
    plan->channels.records[0].color.material_source =
        ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX;
    plan->channels.records[0].color.diffuse_function =
        ACGC_GX_CANONICAL_CHANNEL_DIFFUSE_NONE;
    plan->channels.records[0].color.attenuation_function =
        ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_NONE;
    plan->channels.records[0].alpha.enable =
        ACGC_GX_CANONICAL_CHANNEL_BOOLEAN_FALSE;
    plan->channels.records[0].alpha.ambient_source =
        ACGC_GX_CANONICAL_CHANNEL_SOURCE_REG;
    plan->channels.records[0].alpha.material_source =
        ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX;
    plan->channels.records[0].alpha.diffuse_function =
        ACGC_GX_CANONICAL_CHANNEL_DIFFUSE_NONE;
    plan->channels.records[0].alpha.attenuation_function =
        ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_NONE;

    plan->texgens.header.texgen_capacity = ACGC_GX_CANONICAL_TEXGEN_CAPACITY;
    plan->texgens.header.ordinary_matrix_capacity =
        ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_CAPACITY;
    plan->texgens.header.post_matrix_capacity =
        ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_CAPACITY;
    plan->texgens.header.su_capacity = ACGC_GX_CANONICAL_TEXGEN_SU_CAPACITY;
    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_COUNT;
         index++) {
        plan->texgens.ordinary_matrix[index].logical_id =
            ACGC_GX_CANONICAL_TEXGEN_ORDINARY_LOGICAL_ID(index);
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_COUNT;
         index++) {
        plan->texgens.post_matrix[index].logical_id =
            ACGC_GX_CANONICAL_TEXGEN_POST_LOGICAL_ID(index);
    }

    plan->texture.header.record_byte_offset =
        ACGC_GX_CANONICAL_TEXTURE_HEADER_BYTE_SIZE;
    plan->texture.header.record_count = ACGC_GX_CANONICAL_TEXTURE_STATE_COUNT;
    plan->texture.header.record_capacity =
        ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY;
    plan->texture.header.record_word_count =
        ACGC_GX_CANONICAL_TEXTURE_RECORD_WORD_COUNT;
    plan->texture.header.resource_id_scheme =
        ACGC_GX_CANONICAL_TEXTURE_RESOURCE_ID_SCHEME;

    plan->tev.header.version = ACGC_GX_CANONICAL_TEV_STATE_VERSION;
    plan->tev.header.section_id = ACGC_GX_CANONICAL_TEV_SECTION_ID;
    plan->tev.header.section_mask = ACGC_GX_CANONICAL_TEV_SECTION_MASK;
    plan->tev.header.byte_size = ACGC_GX_CANONICAL_TEV_STATE_SIZE;
    plan->tev.header.active_stage_count = 1;
    plan->tev.header.stage_capacity = ACGC_GX_CANONICAL_TEV_STAGE_CAPACITY;
    plan->tev.header.component_valid_mask =
        ACGC_GX_CANONICAL_TEV_COMPONENT_VALID_MASK;
    plan->tev.header.stage_offset = ACGC_GX_CANONICAL_TEV_STAGE_OFFSET;
    plan->tev.header.stage_record_size =
        ACGC_GX_CANONICAL_TEV_STAGE_RECORD_SIZE;
    plan->tev.header.register_offset = ACGC_GX_CANONICAL_TEV_REGISTER_OFFSET;
    plan->tev.header.register_record_size =
        ACGC_GX_CANONICAL_TEV_REGISTER_RECORD_SIZE;
    plan->tev.header.konst_offset = ACGC_GX_CANONICAL_TEV_KONST_OFFSET;
    plan->tev.header.konst_record_size =
        ACGC_GX_CANONICAL_TEV_KONST_RECORD_SIZE;
    plan->tev.header.swap_table_offset =
        ACGC_GX_CANONICAL_TEV_SWAP_TABLE_OFFSET;
    plan->tev.header.swap_table_record_size =
        ACGC_GX_CANONICAL_TEV_SWAP_TABLE_RECORD_SIZE;
    plan->tev.stages[0].color_a = ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX;
    plan->tev.stages[0].color_b = ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX;
    plan->tev.stages[0].color_c = ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX;
    plan->tev.stages[0].color_d = 10;
    plan->tev.stages[0].alpha_a = ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX;
    plan->tev.stages[0].alpha_b = ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX;
    plan->tev.stages[0].alpha_c = ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX;
    plan->tev.stages[0].alpha_d = 5;
    plan->tev.stages[0].color_op = ACGC_GX_CANONICAL_TEV_OPERATION_ADD;
    plan->tev.stages[0].color_bias = ACGC_GX_CANONICAL_TEV_BIAS_MIN;
    plan->tev.stages[0].color_scale = ACGC_GX_CANONICAL_TEV_SCALE_MIN;
    plan->tev.stages[0].color_clamp = ACGC_GX_CANONICAL_TEV_BOOLEAN_MAX;
    plan->tev.stages[0].color_out = ACGC_GX_CANONICAL_TEV_REGISTER_INDEX_MIN;
    plan->tev.stages[0].alpha_op = ACGC_GX_CANONICAL_TEV_OPERATION_ADD;
    plan->tev.stages[0].alpha_bias = ACGC_GX_CANONICAL_TEV_BIAS_MIN;
    plan->tev.stages[0].alpha_scale = ACGC_GX_CANONICAL_TEV_SCALE_MIN;
    plan->tev.stages[0].alpha_clamp = ACGC_GX_CANONICAL_TEV_BOOLEAN_MAX;
    plan->tev.stages[0].alpha_out = ACGC_GX_CANONICAL_TEV_REGISTER_INDEX_MIN;
    plan->tev.stages[0].tex_coord = ACGC_GX_CANONICAL_TEV_TEXCOORD_NULL;
    plan->tev.stages[0].tex_map = ACGC_GX_CANONICAL_TEV_TEXMAP_NULL;
    plan->tev.stages[0].color_chan = 0;

    plan->blend.mode = 1;
    plan->blend.source_factor = ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_SOURCE_ALPHA;
    plan->blend.destination_factor =
        ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_SOURCE_ALPHA;
    plan->alpha.comp0 = ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX;
    plan->alpha.ref0 = 0;
    plan->alpha.op = ACGC_GX_CANONICAL_ALPHA_OPERATOR_MIN;
    plan->alpha.comp1 = ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX;
    plan->alpha.ref1 = 0;
    plan->alpha.color_update_enable = ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX;
    plan->alpha.alpha_update_enable = ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX;
    plan->depth.z_compare_enable = ACGC_GX_CANONICAL_DEPTH_BOOLEAN_MAX;
    plan->depth.z_compare_func = 3;
    plan->depth.z_update_enable = ACGC_GX_CANONICAL_DEPTH_BOOLEAN_MAX;

    plan->raster.viewport_bits[0] = bits_from_float(0.0f);
    plan->raster.viewport_bits[1] = bits_from_float(0.0f);
    plan->raster.viewport_bits[2] = bits_from_float(64.0f);
    plan->raster.viewport_bits[3] = bits_from_float(64.0f);
    plan->raster.viewport_bits[4] = bits_from_float(0.0f);
    plan->raster.viewport_bits[5] = bits_from_float(1.0f);
    plan->raster.scissor[2] = 64;
    plan->raster.scissor[3] = 64;
    plan->raster.clip_mode = ACGC_GX_CANONICAL_RASTER_CLIP_MODE_ENABLE;
    plan->raster.cull_mode = ACGC_GX_CANONICAL_RASTER_CULL_MODE_BACK;

    plan->indirect.header.version = ACGC_GX_CANONICAL_INDIRECT_STATE_VERSION;
    plan->indirect.header.section_id = ACGC_GX_CANONICAL_INDIRECT_SECTION_ID;
    plan->indirect.header.section_mask =
        ACGC_GX_CANONICAL_INDIRECT_SECTION_MASK;
    plan->indirect.header.byte_size = ACGC_GX_CANONICAL_INDIRECT_STATE_SIZE;
    plan->indirect.header.order_capacity =
        ACGC_GX_CANONICAL_INDIRECT_ORDER_CAPACITY;
    plan->indirect.header.order_record_size =
        ACGC_GX_CANONICAL_INDIRECT_ORDER_RECORD_SIZE;
    plan->indirect.header.order_offset = ACGC_GX_CANONICAL_INDIRECT_ORDER_OFFSET;
    plan->indirect.header.matrix_capacity =
        ACGC_GX_CANONICAL_INDIRECT_MATRIX_CAPACITY;
    plan->indirect.header.matrix_record_size =
        ACGC_GX_CANONICAL_INDIRECT_MATRIX_RECORD_SIZE;
    plan->indirect.header.matrix_offset =
        ACGC_GX_CANONICAL_INDIRECT_MATRIX_OFFSET;

    plan->dynamic.header.owner_epoch = 1;
    plan->dynamic.header.record_byte_offset =
        ACGC_GX_CANONICAL_DYNAMIC_HEADER_BYTE_SIZE;
    plan->dynamic.header.record_count = ACGC_GX_CANONICAL_DYNAMIC_STATE_COUNT;
    plan->dynamic.header.record_capacity =
        ACGC_GX_CANONICAL_DYNAMIC_STATE_CAPACITY;
    plan->dynamic.header.record_word_count =
        ACGC_GX_CANONICAL_DYNAMIC_RECORD_WORD_COUNT;
    plan->dynamic.header.resource_id_scheme =
        ACGC_GX_CANONICAL_DYNAMIC_RESOURCE_ID_SCHEME;
    return 1;
}

static int make_semantic_packet(AcgcGxSemanticPacket* packet) {
    uint32_t vertex;

    if (packet == NULL || !acgc_gx_semantic_packet_init(packet)) {
        return 0;
    }
    packet->primitive = ACGC_GX_SEMANTIC_PRIMITIVE_TRIANGLES;
    packet->vertex_count = 3;
    packet->material.flags = ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR;
    for (vertex = 0; vertex < 3; vertex++) {
        packet->vertices[vertex].position[0] = bits_from_float((float)vertex);
        packet->vertices[vertex].position[1] = bits_from_float(0.0f);
        packet->vertices[vertex].position[2] = bits_from_float(0.0f);
        packet->vertices[vertex].color_rgba8 = UINT32_C(0xAABBCCDD);
    }
    return acgc_gx_semantic_packet_validate(packet);
}

static void emit_cumulative_attempt(uint64_t attempt_id, int result) {
    const uint8_t envelope[] = { 0xAC, 0x0C, 0x01, 0x00 };

    if (s_cumulative_callback != NULL) {
        s_cumulative_callback(
            s_cumulative_context,
            envelope,
            sizeof(envelope)
        );
    }
    s_attempt_callback(s_cumulative_context, attempt_id, result);
}

static void emit_no_publication(uint64_t attempt_id) {
    s_attempt_callback(s_cumulative_context, attempt_id, 0);
}

static void emit_canonical_attempt_with_stage(uint64_t attempt_id) {
    pc_metal_runtime_inject_canonical_resource_stage_fixture(attempt_id, 1);
    emit_cumulative_attempt(
        attempt_id,
        1
    );
}

static int run_tests(void) {
    AcgcGxSemanticPacket semantic_packet;
    AcgcMetalPacketConsumerOutput semantic_output;
    AcgcMetalPacketConsumerOutput alias_before;
    AcgcMetalPacketConsumerCanonicalResourceStage canonical_stage_fixture;
    AcgcMetalPacketConsumerStatus status;
    AcgcPcMetalRuntimeSnapshot runtime_snapshot;
    AcgcAppleCanonicalPlanHandoffSnapshot handoff_snapshot;
    AcgcAppleCanonicalPlan valid_plan;
    TestCumulativeSnapshotCallback captured_callback;
    TestCumulativeSnapshotAttemptCallback captured_attempt_callback;
    void* captured_context;
    uint32_t sink_count_before;
    uint32_t sink_count_after_reinit;
    uint32_t resource_register_count_before;
    uint32_t resource_clear_count_before;

    CHECK(make_base_plan(&valid_plan));
    memset(&canonical_stage_fixture, 0, sizeof(canonical_stage_fixture));
    canonical_stage_fixture.attempt_id = 1;
    canonical_stage_fixture.valid = 1;
    s_plan = valid_plan;
    CHECK(make_semantic_packet(&semantic_packet));

    /* A foreign plan consumer rejects runtime admission before the resource
     * setter, and the foreign consumer remains the only owner. */
    pc_metal_runtime_reset_resource_registration_fixture();
    CHECK(pc_metal_runtime_resource_callback_owner_fixture() ==
          RESOURCE_OWNER_NONE);
    CHECK(acgc_apple_canonical_plan_handoff_shutdown());
    CHECK(acgc_apple_canonical_plan_handoff_init());
    s_foreign_plan_consumer_calls = 0;
    CHECK(acgc_apple_canonical_plan_handoff_set_consumer(
        foreign_plan_consumer,
        &s_foreign_plan_context
    ));
    resource_register_count_before =
        pc_metal_runtime_resource_register_count_fixture();
    pc_metal_runtime_init();
    pc_metal_runtime_get_snapshot(&runtime_snapshot);
    CHECK(runtime_snapshot.registered == 0);
    CHECK(runtime_snapshot.sink_initialized == 0);
    CHECK(s_semantic_callback == NULL);
    CHECK(!pc_metal_runtime_runtime_callback_registered_fixture());
    CHECK(!pc_metal_runtime_source_provider_registered_fixture());
    CHECK(pc_metal_runtime_resource_callback_owner_fixture() ==
          RESOURCE_OWNER_NONE);
    CHECK(pc_metal_runtime_resource_register_count_fixture() ==
          resource_register_count_before);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&handoff_snapshot));
    CHECK(handoff_snapshot.consumer_registered == 1);
    emit_cumulative_attempt(100, 1);
    CHECK(s_foreign_plan_consumer_calls == 1);
    pc_metal_runtime_get_snapshot(&runtime_snapshot);
    CHECK(runtime_snapshot.canonical_attempt_count == 0);
    CHECK(acgc_apple_canonical_plan_handoff_clear_consumer());
    CHECK(acgc_apple_canonical_plan_handoff_shutdown());

    /* A foreign resource owner rejects after the runtime acquires its plan
     * consumer; rollback clears only that plan owner and runtime state. */
    CHECK(acgc_apple_canonical_plan_handoff_init());
    CHECK(pc_metal_runtime_install_foreign_resource_callback_fixture());
    CHECK(pc_metal_runtime_resource_callback_owner_fixture() ==
          RESOURCE_OWNER_FOREIGN);
    resource_register_count_before =
        pc_metal_runtime_resource_register_count_fixture();
    resource_clear_count_before =
        pc_metal_runtime_resource_clear_count_fixture();
    pc_metal_runtime_init();
    pc_metal_runtime_get_snapshot(&runtime_snapshot);
    CHECK(runtime_snapshot.registered == 0);
    CHECK(runtime_snapshot.sink_initialized == 0);
    CHECK(s_semantic_callback == NULL);
    CHECK(!pc_metal_runtime_runtime_callback_registered_fixture());
    CHECK(!pc_metal_runtime_source_provider_registered_fixture());
    CHECK(pc_metal_runtime_resource_callback_owner_fixture() ==
          RESOURCE_OWNER_FOREIGN);
    CHECK(pc_metal_runtime_resource_register_count_fixture() ==
          resource_register_count_before + 1);
    CHECK(pc_metal_runtime_resource_clear_count_fixture() ==
          resource_clear_count_before);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&handoff_snapshot));
    CHECK(handoff_snapshot.consumer_registered == 0);
    CHECK(acgc_apple_canonical_plan_handoff_set_consumer(
        foreign_plan_consumer,
        &s_foreign_plan_context
    ));
    CHECK(acgc_apple_canonical_plan_handoff_clear_consumer());
    CHECK(acgc_apple_canonical_plan_handoff_shutdown());
    CHECK(pc_metal_runtime_clear_foreign_resource_callback_fixture());

    pc_metal_runtime_reset_resource_registration_fixture();
    CHECK(acgc_apple_canonical_plan_handoff_shutdown());
    CHECK(acgc_apple_canonical_plan_handoff_init());
    pc_metal_runtime_init();
    CHECK(s_cumulative_callback != NULL);
    CHECK(s_attempt_callback != NULL);
    CHECK(s_semantic_callback != NULL);
    captured_callback = s_cumulative_callback;
    captured_attempt_callback = s_attempt_callback;
    captured_context = s_cumulative_context;
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&handoff_snapshot));
    CHECK(handoff_snapshot.consumer_registered == 1);
    pc_metal_runtime_get_snapshot(&runtime_snapshot);
    CHECK(runtime_snapshot.registered == 1);
    CHECK(pc_metal_runtime_resource_callback_owner_fixture() ==
          RESOURCE_OWNER_RUNTIME);
    CHECK(pc_metal_runtime_runtime_callback_registered_fixture());
    CHECK(pc_metal_runtime_source_provider_registered_fixture());

    /* A fresh canonical publication wins exactly once and suppresses the
     * later semantic callback belonging to that same synchronous attempt. */
    emit_canonical_attempt_with_stage(1);
    s_semantic_callback(s_semantic_context, &semantic_packet);
    CHECK(s_sink_submit_count == 1);
    pc_metal_runtime_get_snapshot(&runtime_snapshot);
    CHECK(runtime_snapshot.canonical_won_count == 1);
    CHECK(runtime_snapshot.semantic_suppressed_count == 1);
    CHECK(s_last_sink_output.source_kind ==
          ACGC_METAL_PACKET_CONSUMER_SOURCE_CANONICAL_PLAN);
    CHECK(s_last_sink_output.semantic_version == 0);
    CHECK(s_last_sink_output.canonical_resource_stage.attempt_id == 1);
    CHECK(s_last_sink_output.canonical_resource_stage.valid == 1);

    /* A failed gather after a prior win clears the winner before semantic
     * fallback; no old canonical output is reused. */
    emit_no_publication(2);
    s_semantic_callback(s_semantic_context, &semantic_packet);
    CHECK(s_sink_submit_count == 2);
    CHECK(s_last_sink_output.source_kind ==
          ACGC_METAL_PACKET_CONSUMER_SOURCE_SEMANTIC);

    /* Plan-builder rejection falls back to the same semantic v1 path. */
    s_plan_build_status = ACGC_APPLE_CANONICAL_PLAN_SECTION_SEMANTIC;
    emit_canonical_attempt_with_stage(3);
    CHECK(s_sink_submit_count == 2);
    s_semantic_callback(s_semantic_context, &semantic_packet);
    CHECK(s_sink_submit_count == 3);
    s_plan_build_status = ACGC_APPLE_CANONICAL_PLAN_OK;

    /* Consumer prepare rejection preserves the prior sink output until the
     * semantic callback supplies a fresh value. */
    s_plan.geometry.vertex_count = 4;
    emit_canonical_attempt_with_stage(4);
    CHECK(s_sink_submit_count == 3);
    s_semantic_callback(s_semantic_context, &semantic_packet);
    CHECK(s_sink_submit_count == 4);
    CHECK(s_last_sink_output.source_kind ==
          ACGC_METAL_PACKET_CONSUMER_SOURCE_SEMANTIC);
    s_plan = valid_plan;

    /* The real consumer's alias rejection leaves its caller output unchanged. */
    alias_before = s_last_sink_output;
    status = acgc_metal_packet_consumer_prepare_canonical_plan(
        (const AcgcAppleCanonicalPlan*)&alias_before,
        &canonical_stage_fixture,
        &alias_before
    );
    CHECK(status == ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT);
    CHECK(memcmp(&alias_before, &s_last_sink_output, sizeof(alias_before)) == 0);

    /* Duplicate/stale notification invalidates a canonical winner and lets
     * semantic v1 submit instead of suppressing it. */
    emit_canonical_attempt_with_stage(5);
    CHECK(s_sink_submit_count == 5);
    captured_attempt_callback(captured_context, 5, 1);
    s_semantic_callback(s_semantic_context, &semantic_packet);
    CHECK(s_sink_submit_count == 6);
    CHECK(s_last_sink_output.source_kind ==
          ACGC_METAL_PACKET_CONSUMER_SOURCE_SEMANTIC);

    /* A sink failure is not a canonical win, so semantic fallback remains live. */
    s_sink_status = ACGC_METAL_SINK_RESOURCE_FAILURE;
    emit_canonical_attempt_with_stage(6);
    CHECK(s_sink_submit_count == 7);
    s_semantic_callback(s_semantic_context, &semantic_packet);
    CHECK(s_sink_submit_count == 8);
    s_sink_status = ACGC_METAL_SINK_OK;
    pc_metal_runtime_get_snapshot(&runtime_snapshot);
    CHECK(runtime_snapshot.canonical_sink_failure_count == 1);

    /* Callback-time lifecycle/registration/nested-consume attempts are all
     * fail-closed while the fake sink is inside the borrowed callback. */
    s_sink_reenter = 1;
    emit_canonical_attempt_with_stage(7);
    s_sink_reenter = 0;
    CHECK(!s_sink_internal_failure);
    CHECK(s_reentry_init_attempted);
    CHECK(s_reentry_shutdown_attempted);
    CHECK(s_reentry_set_result == 0);
    CHECK(s_reentry_clear_result == 0);
    CHECK(s_nested_callback_active == 1);
    CHECK(s_sink_submit_count == 9);
    emit_no_publication(8);

    /* The existing v1/v2/v3/v4 policy remains source/version-aware. */
    CHECK(acgc_metal_packet_consumer_prepare(
        &semantic_packet,
        NULL,
        &semantic_output
    ) == ACGC_METAL_PACKET_CONSUMER_OK);
    sink_count_before = s_sink_submit_count;
    pc_metal_runtime_observe_output_fixture(
        &semantic_output,
        ACGC_METAL_PACKET_CONSUMER_OK
    );
    CHECK(s_sink_submit_count == sink_count_before + 1);
    semantic_output.semantic_version = ACGC_GX_SEMANTIC_PACKET_V2_VERSION;
    semantic_output.v2_extension_rendering_status =
        ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_RENDERED;
    pc_metal_runtime_observe_output_fixture(
        &semantic_output,
        ACGC_METAL_PACKET_CONSUMER_OK
    );
    CHECK(s_sink_submit_count == sink_count_before + 1);
    semantic_output.semantic_version = ACGC_GX_SEMANTIC_PACKET_V3_VERSION;
    semantic_output.v2_extension_rendering_status =
        ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_APPLICABLE;
    semantic_output.v3_extension_rendering_status =
        ACGC_METAL_PACKET_CONSUMER_V3_EXTENSION_NOT_RENDERED;
    pc_metal_runtime_observe_output_fixture(
        &semantic_output,
        ACGC_METAL_PACKET_CONSUMER_OK
    );
    CHECK(s_sink_submit_count == sink_count_before + 1);
    semantic_output.semantic_version = ACGC_GX_SEMANTIC_PACKET_V4_VERSION;
    semantic_output.v4_extension_rendering_status = 0;
    pc_metal_runtime_observe_output_fixture(
        &semantic_output,
        ACGC_METAL_PACKET_CONSUMER_OK
    );
    CHECK(s_sink_submit_count == sink_count_before + 1);

    /* Normal shutdown clears the consumer and pair; re-init starts without a
     * stale plan or callback context and accepts a new semantic fallback. */
    pc_metal_runtime_shutdown();
    CHECK(pc_metal_runtime_resource_callback_owner_fixture() ==
          RESOURCE_OWNER_NONE);
    CHECK(!pc_metal_runtime_runtime_callback_registered_fixture());
    CHECK(!pc_metal_runtime_source_provider_registered_fixture());
    CHECK(s_semantic_callback == NULL);
    resource_clear_count_before =
        pc_metal_runtime_resource_clear_count_fixture();
    pc_metal_runtime_shutdown();
    CHECK(pc_metal_runtime_resource_clear_count_fixture() ==
          resource_clear_count_before);
    CHECK(acgc_apple_canonical_plan_handoff_shutdown());
    CHECK(s_cumulative_callback == NULL);
    CHECK(s_attempt_callback == NULL);
    captured_callback(captured_context, (const uint8_t*)"x", 1);
    CHECK(acgc_apple_canonical_plan_handoff_init());
    pc_metal_runtime_init();
    CHECK(s_cumulative_callback != NULL);
    CHECK(s_attempt_callback != NULL);
    CHECK(pc_metal_runtime_resource_callback_owner_fixture() ==
          RESOURCE_OWNER_RUNTIME);
    CHECK(pc_metal_runtime_runtime_callback_registered_fixture());
    CHECK(pc_metal_runtime_source_provider_registered_fixture());
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&handoff_snapshot));
    CHECK(handoff_snapshot.consumer_registered == 1);
    emit_no_publication(9);
    s_semantic_callback(s_semantic_context, &semantic_packet);
    CHECK(s_last_sink_output.source_kind ==
          ACGC_METAL_PACKET_CONSUMER_SOURCE_SEMANTIC);
    sink_count_after_reinit = s_sink_submit_count;

    /* A published attempt without an active-borrow stage is rejected before
     * plan preparation and cannot submit to the sink. */
    emit_cumulative_attempt(10, 1);
    CHECK(s_sink_submit_count == sink_count_after_reinit);
    pc_metal_runtime_get_snapshot(&runtime_snapshot);
    CHECK(runtime_snapshot.canonical_last_status ==
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DEPENDENCY_UNSUPPORTED);
    /* A staged resource record for a different attempt is equally invalid. */
    pc_metal_runtime_inject_canonical_resource_stage_fixture(11, 1);
    emit_cumulative_attempt(12, 1);
    CHECK(s_sink_submit_count == sink_count_after_reinit);
    pc_metal_runtime_get_snapshot(&runtime_snapshot);
    CHECK(runtime_snapshot.canonical_last_status ==
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DEPENDENCY_UNSUPPORTED);
    pc_metal_runtime_inject_canonical_resource_stage_fixture(13, 0);
    emit_cumulative_attempt(13, 1);
    CHECK(s_sink_submit_count == sink_count_after_reinit);
    pc_metal_runtime_get_snapshot(&runtime_snapshot);
    CHECK(runtime_snapshot.canonical_last_status ==
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DEPENDENCY_UNSUPPORTED);
    pc_metal_runtime_shutdown();
    CHECK(pc_metal_runtime_resource_callback_owner_fixture() ==
          RESOURCE_OWNER_NONE);
    CHECK(!pc_metal_runtime_runtime_callback_registered_fixture());
    CHECK(!pc_metal_runtime_source_provider_registered_fixture());
    CHECK(acgc_apple_canonical_plan_handoff_shutdown());
    puts("PC Metal runtime arbitration fixture: PASS");
    return 1;
}

int main(void) {
    return run_tests() ? 0 : 1;
}
