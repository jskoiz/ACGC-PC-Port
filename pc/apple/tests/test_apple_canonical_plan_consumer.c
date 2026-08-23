#include "acgc/metal_packet_consumer.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static uint32_t bits_from_float(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int expect_rejection(
    const AcgcAppleCanonicalPlan* plan,
    AcgcMetalPacketConsumerOutput* output
) {
    AcgcMetalPacketConsumerOutput before;
    AcgcMetalPacketConsumerStatus status;

    if (plan == NULL || output == NULL) {
        return 0;
    }
    before = *output;
    status = acgc_metal_packet_consumer_prepare_canonical_plan(plan, output);
    return status != ACGC_METAL_PACKET_CONSUMER_OK &&
        memcmp(&before, output, sizeof(before)) == 0;
}

static int make_base_plan(AcgcAppleCanonicalPlan* plan) {
    uint32_t index;

    if (plan == NULL) {
        return 0;
    }
    memset(plan, 0, sizeof(*plan));

    plan->geometry.primitive =
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES;
    plan->geometry.vtxfmt = 0;
    plan->geometry.vertex_count = 3;
    plan->geometry.present_mask =
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0);
    plan->geometry.component_mask =
        ACGC_APPLE_CANONICAL_PLAN_COMPONENT_POSITION |
        ACGC_APPLE_CANONICAL_PLAN_COMPONENT_COLOR0;
    for (index = 0; index < 3; index++) {
        plan->geometry.vertices[index].present_mask =
            plan->geometry.present_mask;
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

    /* One valid but disabled COLOR0A0 channel keeps the TEV dependency explicit. */
    plan->channels.active_count = 1;
    plan->channels.record_valid_mask = 1;

    plan->texgens.header.texgen_capacity =
        ACGC_GX_CANONICAL_TEXGEN_CAPACITY;
    plan->texgens.header.ordinary_matrix_capacity =
        ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_CAPACITY;
    plan->texgens.header.post_matrix_capacity =
        ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_CAPACITY;
    plan->texgens.header.su_capacity =
        ACGC_GX_CANONICAL_TEXGEN_SU_CAPACITY;
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
    plan->texture.header.record_count =
        ACGC_GX_CANONICAL_TEXTURE_STATE_COUNT;
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
    plan->tev.stages[0].color_out =
        ACGC_GX_CANONICAL_TEV_REGISTER_INDEX_MIN;
    plan->tev.stages[0].alpha_op = ACGC_GX_CANONICAL_TEV_OPERATION_ADD;
    plan->tev.stages[0].alpha_bias = ACGC_GX_CANONICAL_TEV_BIAS_MIN;
    plan->tev.stages[0].alpha_scale = ACGC_GX_CANONICAL_TEV_SCALE_MIN;
    plan->tev.stages[0].alpha_clamp = ACGC_GX_CANONICAL_TEV_BOOLEAN_MAX;
    plan->tev.stages[0].alpha_out =
        ACGC_GX_CANONICAL_TEV_REGISTER_INDEX_MIN;
    plan->tev.stages[0].tex_coord = ACGC_GX_CANONICAL_TEV_TEXCOORD_NULL;
    plan->tev.stages[0].tex_map = ACGC_GX_CANONICAL_TEV_TEXMAP_NULL;
    plan->tev.stages[0].color_chan = 0;

    plan->blend.mode = 1;
    plan->blend.source_factor =
        ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_SOURCE_ALPHA;
    plan->blend.destination_factor =
        ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_SOURCE_ALPHA;
    plan->alpha.comp0 = ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX;
    plan->alpha.ref0 = 0;
    plan->alpha.op = ACGC_GX_CANONICAL_ALPHA_OPERATOR_MIN;
    plan->alpha.comp1 = ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX;
    plan->alpha.ref1 = 0;
    plan->alpha.color_update_enable = ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX;
    plan->alpha.alpha_update_enable = ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX;
    plan->alpha.z_comp_loc_before_tex = 0;

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
    plan->indirect.header.order_offset =
        ACGC_GX_CANONICAL_INDIRECT_ORDER_OFFSET;
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

static int run_rejection_matrix(const AcgcAppleCanonicalPlan* base,
                                AcgcMetalPacketConsumerOutput* output) {
    AcgcAppleCanonicalPlan mutated;

    mutated = *base;
    mutated.geometry.vertex_count = 4;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.geometry.primitive = ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.geometry.component_mask |=
        ACGC_APPLE_CANONICAL_PLAN_COMPONENT_NORMAL;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.geometry.vertices[0].position[0] = UINT32_C(0x7F800000);
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.geometry.vertices[1].position_matrix_id = 3;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.transform.known_mask &=
        ~ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(0);
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.transform.projection[0] = UINT32_C(0x7FC00000);
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.channels.records[0].color.enable = 1;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.texgens.header.active_texgen_count = 1;
    mutated.texgens.header.known_texgen_count = 1;
    mutated.texgens.header.texgen_known_mask = 1;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.texgens.header.active_texgen_count = 1;
    mutated.texgens.header.known_texgen_count = 1;
    mutated.texgens.header.texgen_known_mask = 1;
    mutated.texgens.texgen[0].function =
        ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP0;
    mutated.texgens.texgen[0].component_known =
        ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.texture.header.known_map_mask = 1;
    mutated.texture.header.known_map_count = 1;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.texture.header.tlut_present_map_mask = 1;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.tev.stages[0].tex_map = 0;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.lighting.loaded_mask = 1;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.blend.mode = ACGC_GX_SEMANTIC_V3_BLEND_MODE_LOGIC;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.blend.source_factor =
        ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_SOURCE_COLOR;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.alpha.comp0 = 0;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.alpha.color_update_enable = 0;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.depth.z_compare_enable = 0;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.raster.scissor[2] = 63;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.raster.clip_mode = ACGC_GX_CANONICAL_RASTER_CLIP_MODE_DISABLE;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.raster.cull_mode = ACGC_GX_CANONICAL_RASTER_CULL_MODE_ALL;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.raster.dither = 1;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.raster.line_width = 1;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.raster.point_size = 1;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.fog.fog_type = ACGC_GX_CANONICAL_FOG_TYPE_PERSP_LIN;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.indirect.header.active_indirect_stage_count = 1;
    mutated.indirect.header.active_order_mask = 1;
    if (!expect_rejection(&mutated, output)) return 0;
    mutated = *base;
    mutated.dynamic.header.present_image_mask = 1;
    mutated.dynamic.header.required_image_mask = 1;
    mutated.dynamic.header.present_resource_count = 1;
    if (!expect_rejection(&mutated, output)) return 0;
    return 1;
}

int main(void) {
    AcgcAppleCanonicalPlan base;
    AcgcAppleCanonicalPlan mutated;
    AcgcAppleCanonicalPlan copy;
    AcgcMetalPacketConsumerOutput output;
    AcgcMetalPacketConsumerOutput before;
    AcgcGxSemanticPacket semantic;

    CHECK(make_base_plan(&base));
    memset(&output, 0xA5, sizeof(output));
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(&base, &output) ==
          ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output.source_kind ==
          ACGC_METAL_PACKET_CONSUMER_SOURCE_CANONICAL_PLAN);
    CHECK(output.semantic_version == 0);
    CHECK(output.alpha_write_enabled == 1);
    CHECK(output.state.viewport.width == bits_from_float(64.0f));
    CHECK(output.state.viewport.height == bits_from_float(64.0f));
    CHECK(output.state.depth.compare_function == ACGC_METAL_DEPTH_LESS_EQUAL);
    CHECK(output.state.blend.enabled == 1);
    CHECK(output.state.blend.source_rgb_factor ==
          ACGC_METAL_BLEND_SOURCE_ALPHA);
    CHECK(output.state.blend.destination_rgb_factor ==
          ACGC_METAL_BLEND_ONE_MINUS_SOURCE_ALPHA);
    CHECK(output.state.raster.cull_mode == ACGC_METAL_CULL_BACK);
    CHECK(output.state.raster.front_facing_winding ==
          ACGC_METAL_WINDING_COUNTER_CLOCKWISE);
    CHECK(output.state.raster.triangle_fill_mode == ACGC_METAL_TRIANGLE_FILL);
    CHECK(output.state.transform.matrix[0] == bits_from_float(1.0f));
    CHECK(output.state.transform.matrix[5] == bits_from_float(1.0f));
    CHECK(output.state.transform.matrix[10] == bits_from_float(1.0f));
    CHECK(output.state.transform.matrix[15] == bits_from_float(1.0f));
    CHECK(output.geometry.vertices[0].color_rgba8 == UINT32_C(0x11223344));
    CHECK(output.geometry.vertices[1].color_rgba8 == UINT32_C(0x55667788));
    CHECK(acgc_metal_state_fixture_validate(&output.state));
    CHECK(acgc_renderer_geometry_validate(&output.geometry));
    before = output;
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(NULL, &output) ==
          ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT);
    CHECK(memcmp(&before, &output, sizeof(output)) == 0);

    copy = base;
    before = output;
    base.geometry.vertices[0].position[0] = bits_from_float(99.0f);
    base.geometry.vertices[0].color_rgba8[0] = UINT32_C(0x01020304);
    CHECK(memcmp(&before, &output, sizeof(output)) == 0);
    base = copy;
    copy.geometry.vertices[0].position[0] = bits_from_float(99.0f);
    copy.geometry.vertices[0].color_rgba8[0] = UINT32_C(0x01020304);
    CHECK(memcmp(&before, &output, sizeof(output)) == 0);
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(&copy, &output) ==
          ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output.geometry.vertices[0].position_x == bits_from_float(99.0f));
    CHECK(output.geometry.vertices[0].color_rgba8 == UINT32_C(0x04030201));

    mutated = base;
    mutated.geometry.vertices[3].position[0] = bits_from_float(1.0f);
    CHECK(expect_rejection(&mutated, &output));

    CHECK(run_rejection_matrix(&base, &output));

    /* A present PNMTXIDX is accepted only as a direct normalized selector. */
    mutated = base;
    mutated.geometry.present_mask |=
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_PNMTXIDX;
    mutated.geometry.vertices[0].present_mask = mutated.geometry.present_mask;
    mutated.geometry.vertices[1].present_mask = mutated.geometry.present_mask;
    mutated.geometry.vertices[2].present_mask = mutated.geometry.present_mask;
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
              &mutated, &output) == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output.geometry.vertices[0].position_x == bits_from_float(0.0f));

    /* Exercise the exact sparse perspective projection and row-major 3x4 M. */
    mutated = base;
    mutated.transform.projection_type =
        ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE;
    mutated.transform.projection[0] = bits_from_float(2.0f);
    mutated.transform.projection[1] = bits_from_float(0.25f);
    mutated.transform.projection[2] = bits_from_float(3.0f);
    mutated.transform.projection[3] = bits_from_float(-0.5f);
    mutated.transform.projection[4] = bits_from_float(-1.0f);
    mutated.transform.projection[5] = bits_from_float(2.0f);
    mutated.transform.position[0][3] = bits_from_float(0.125f);
    mutated.transform.position[0][7] = bits_from_float(-0.25f);
    mutated.transform.position[0][11] = bits_from_float(0.5f);
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
              &mutated, &output) == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output.state.transform.matrix[0] == bits_from_float(2.0f));
    CHECK(output.state.transform.matrix[1] == bits_from_float(0.0f));
    CHECK(output.state.transform.matrix[5] == bits_from_float(3.0f));
    CHECK(output.state.transform.matrix[6] == bits_from_float(0.0f));
    CHECK(output.state.transform.matrix[9] == bits_from_float(-0.5f));
    CHECK(output.state.transform.matrix[10] == bits_from_float(-1.0f));
    CHECK(output.state.transform.matrix[11] == bits_from_float(-1.0f));
    CHECK(output.state.transform.matrix[12] == bits_from_float(0.375f));
    CHECK(output.state.transform.matrix[13] == bits_from_float(-1.0f));
    CHECK(output.state.transform.matrix[14] == bits_from_float(1.5f));
    CHECK(output.state.transform.matrix[15] == bits_from_float(-0.5f));

    /* Input/output aliasing is rejected before reading either value. */
    memset(&output, 0x5A, sizeof(output));
    before = output;
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
              (const AcgcAppleCanonicalPlan*)&output, &output) ==
          ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT);
    CHECK(memcmp(&before, &output, sizeof(output)) == 0);

    CHECK(make_semantic_packet(&semantic));
    memset(&output, 0xA5, sizeof(output));
    CHECK(acgc_metal_packet_consumer_prepare(&semantic, NULL, &output) ==
          ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output.source_kind == ACGC_METAL_PACKET_CONSUMER_SOURCE_SEMANTIC);
    CHECK(output.semantic_version == ACGC_GX_SEMANTIC_PACKET_VERSION);

    /* PASS is deliberately emitted only after every mutation gate succeeds. */
    puts("Apple canonical plan consumer fixture: PASS");
    return 0;
}
