#include "acgc/metal_packet_consumer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static AcgcMetalPacketConsumerCanonicalResourceStage s_default_resource_stage;

static AcgcMetalPacketConsumerStatus prepare_default_plan(
    const AcgcAppleCanonicalPlan* plan,
    AcgcMetalPacketConsumerOutput* output
) {
    return acgc_metal_packet_consumer_prepare_canonical_plan(
        plan,
        &s_default_resource_stage,
        output
    );
}

static uint32_t bits_from_float(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float float_from_bits(uint32_t bits) {
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void set_decomp_dynamic_raster(AcgcGxCanonicalRasterState* raster) {
    raster->viewport_bits[0] = bits_from_float(0.0f);
    raster->viewport_bits[1] = bits_from_float(0.0f);
    raster->viewport_bits[2] = bits_from_float(640.0f);
    raster->viewport_bits[3] = bits_from_float(480.0f);
    raster->viewport_bits[4] = bits_from_float(0.0f);
    raster->viewport_bits[5] = bits_from_float(1.0f);
    raster->scissor[0] = 0;
    raster->scissor[1] = 0;
    raster->scissor[2] = 640;
    raster->scissor[3] = 480;
    raster->scissor_offset[0] = 0;
    raster->scissor_offset[1] = 0;
    raster->clip_mode = ACGC_GX_CANONICAL_RASTER_CLIP_MODE_ENABLE;
    raster->cull_mode = ACGC_GX_CANONICAL_RASTER_CULL_MODE_BACK;
    raster->co_planar_enable = 0;
    /* GXSetLineWidth(line_width - 1, ...) follows the 6-pixel init. */
    raster->line_width = 5;
    raster->point_size = 6;
    raster->dither = 1;
    raster->field_mode = 0;
    raster->half_aspect_ratio = 0;
    raster->field_odd_mask = 1;
    raster->field_even_mask = 1;
    raster->dst_alpha_enable = 0;
    raster->dst_alpha = 0;
}

static int expect_staged_raster(
    const AcgcAppleCanonicalPlan* plan,
    AcgcMetalPacketConsumerOutput* output
) {
    AcgcAppleCanonicalPlan before_plan;

    if (plan == NULL || output == NULL) {
        return 0;
    }
    before_plan = *plan;
    if (prepare_default_plan(plan, output) !=
            ACGC_METAL_PACKET_CONSUMER_OK ||
        output->canonical_raster_disposition !=
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_DISPOSITION_STAGED_UNRENDERED ||
        memcmp(&output->canonical_raster, &plan->raster,
               sizeof(plan->raster)) != 0 ||
        output->state.viewport.origin_x != ACGC_METAL_FLOAT_ZERO ||
        output->state.viewport.origin_y != ACGC_METAL_FLOAT_ZERO ||
        output->state.viewport.width != ACGC_METAL_FLOAT_SIXTY_FOUR ||
        output->state.viewport.height != ACGC_METAL_FLOAT_SIXTY_FOUR ||
        output->state.viewport.znear != ACGC_METAL_FLOAT_ZERO ||
        output->state.viewport.zfar != ACGC_METAL_FLOAT_ONE ||
        output->state.raster.cull_mode != ACGC_METAL_CULL_NONE ||
        !acgc_metal_state_fixture_validate(&output->state) ||
        memcmp(&before_plan, plan, sizeof(before_plan)) != 0) {
        return 0;
    }
    return 1;
}

static int expect_rejection(
    const AcgcAppleCanonicalPlan* plan,
    AcgcMetalPacketConsumerOutput* output
) {
    AcgcAppleCanonicalPlan before_plan;
    AcgcMetalPacketConsumerOutput before;
    AcgcMetalPacketConsumerStatus status;

    if (plan == NULL || output == NULL) {
        return 0;
    }
    before_plan = *plan;
    before = *output;
    status = prepare_default_plan(plan, output);
    return status != ACGC_METAL_PACKET_CONSUMER_OK &&
        memcmp(&before, output, sizeof(before)) == 0 &&
        memcmp(&before_plan, plan, sizeof(before_plan)) == 0;
}

static int expect_rejection_status(
    const AcgcAppleCanonicalPlan* plan,
    AcgcMetalPacketConsumerOutput* output,
    AcgcMetalPacketConsumerStatus expected_status
) {
    AcgcAppleCanonicalPlan before_plan;
    AcgcMetalPacketConsumerOutput before;
    AcgcMetalPacketConsumerStatus status;

    if (plan == NULL || output == NULL ||
        expected_status == ACGC_METAL_PACKET_CONSUMER_OK) {
        return 0;
    }
    before_plan = *plan;
    before = *output;
    status = prepare_default_plan(plan, output);
    if (status != expected_status) {
        fprintf(
            stderr,
            "expected consumer status %s, got %s\n",
            acgc_metal_packet_consumer_status_string(expected_status),
            acgc_metal_packet_consumer_status_string(status)
        );
    }
    return status == expected_status &&
        memcmp(&before, output, sizeof(before)) == 0 &&
        memcmp(&before_plan, plan, sizeof(before_plan)) == 0;
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
    plan->channels.records[0].color.material_source =
        ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX;
    plan->channels.records[0].color.attenuation_function =
        ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_NONE;
    plan->channels.records[0].alpha.material_source =
        ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX;
    plan->channels.records[0].alpha.attenuation_function =
        ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_NONE;

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
    plan->alpha.z_comp_loc_before_tex = 1;

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

static int make_geometry_plan(
    AcgcAppleCanonicalPlan* plan,
    uint32_t primitive,
    uint32_t vertex_count
) {
    uint32_t vertex;

    if (plan == NULL || vertex_count == 0 ||
        vertex_count > ACGC_APPLE_CANONICAL_PLAN_MAX_VERTEX_COUNT ||
        !make_base_plan(plan)) {
        return 0;
    }
    plan->geometry.primitive = primitive;
    plan->geometry.vertex_count = vertex_count;
    for (vertex = 0; vertex < vertex_count; vertex++) {
        AcgcAppleCanonicalPlanVertex* destination =
            &plan->geometry.vertices[vertex];

        destination->present_mask = plan->geometry.present_mask;
        destination->component_mask = plan->geometry.component_mask;
        destination->position_matrix_id = 0;
        destination->position[0] = bits_from_float((float)vertex);
        destination->position[1] = bits_from_float((float)(vertex % 4));
        destination->position[2] = bits_from_float(0.0f);
        destination->color_rgba8[0] = UINT32_C(0x01020300) | vertex;
    }
    return 1;
}

static int make_source_geometry_plan(AcgcAppleCanonicalPlan* plan) {
    const uint32_t normal_mask =
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM;
    const uint32_t texcoord0_mask =
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0;
    uint32_t vertex;

    if (!make_geometry_plan(
            plan, ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES, 3)) {
        return 0;
    }
    plan->geometry.present_mask |= normal_mask | texcoord0_mask;
    plan->geometry.component_mask |=
        ACGC_APPLE_CANONICAL_PLAN_COMPONENT_NORMAL |
        ACGC_APPLE_CANONICAL_PLAN_COMPONENT_TEXCOORD0;
    if (plan->geometry.present_mask != UINT32_C(0x2E00) ||
        plan->geometry.component_mask != UINT32_C(0x00000053)) {
        return 0;
    }
    for (vertex = 0; vertex < plan->geometry.vertex_count; vertex++) {
        AcgcAppleCanonicalPlanVertex* destination =
            &plan->geometry.vertices[vertex];

        destination->present_mask = plan->geometry.present_mask;
        destination->component_mask = plan->geometry.component_mask;
        destination->normal[0] = bits_from_float(1.0f);
        destination->normal[1] = bits_from_float(0.0f);
        destination->normal[2] = bits_from_float(0.0f);
        destination->texcoord[0][0] = bits_from_float((float)vertex);
        destination->texcoord[0][1] = bits_from_float((float)(vertex + 1));
    }
    return 1;
}

static int make_active_texgen_plan(AcgcAppleCanonicalPlan* plan) {
    static const uint32_t ordinary30_words[12] = {
        UINT32_C(0x39800000), UINT32_C(0x00000000), UINT32_C(0x00000000),
        UINT32_C(0x00000000), UINT32_C(0x00000000), UINT32_C(0x3A800000),
        UINT32_C(0x00000000), UINT32_C(0x00000000), UINT32_C(0x00000000),
        UINT32_C(0x00000000), UINT32_C(0x00000000), UINT32_C(0x00000000)
    };
    static const uint32_t identity_words[12] = {
        UINT32_C(0x3F800000), UINT32_C(0x00000000), UINT32_C(0x00000000),
        UINT32_C(0x00000000), UINT32_C(0x00000000), UINT32_C(0x3F800000),
        UINT32_C(0x00000000), UINT32_C(0x00000000), UINT32_C(0x00000000),
        UINT32_C(0x00000000), UINT32_C(0x3F800000), UINT32_C(0x00000000)
    };
    AcgcGxCanonicalTexgenState* texgens;
    uint32_t vertex;
    uint32_t word;

    if (plan == NULL || !make_source_geometry_plan(plan)) {
        return 0;
    }
    texgens = &plan->texgens;
    texgens->header.active_texgen_count = 2;
    texgens->header.known_texgen_count = 8;
    texgens->header.texgen_known_mask = UINT32_C(0x000000FF);
    texgens->header.ordinary_matrix_count = 2;
    texgens->header.ordinary_matrix_known_mask = UINT32_C(0x00000401);
    texgens->header.post_matrix_count = 1;
    texgens->header.post_matrix_known_mask = UINT32_C(0x00100000);
    texgens->header.su_count = 0;
    texgens->header.su_known_mask = 0;
    texgens->header.component_known_summary =
        ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_TEXGEN;

    texgens->texgen[0].function =
        ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX2X4;
    texgens->texgen[0].source = ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX0;
    texgens->texgen[0].ordinary_matrix_id = 30;
    texgens->texgen[0].post_matrix_id = 125;
    texgens->texgen[0].component_known =
        ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL;
    texgens->texgen[1].function =
        ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX2X4;
    texgens->texgen[1].source = ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX0;
    texgens->texgen[1].ordinary_matrix_id = 60;
    texgens->texgen[1].post_matrix_id = 125;
    texgens->texgen[1].component_known =
        ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL;
    /* The live known mask is 0xff, so canonical validation also requires
     * complete retained records 2..7. These are the emu64 initial setters;
     * the consumer predicate intentionally does not constrain their values. */
    for (word = 2; word < ACGC_GX_CANONICAL_TEXGEN_COUNT; word++) {
        texgens->texgen[word].function =
            ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX2X4;
        texgens->texgen[word].source =
            ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX0 + word;
        texgens->texgen[word].ordinary_matrix_id = 60;
        texgens->texgen[word].post_matrix_id = 125;
        texgens->texgen[word].component_known =
            ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL;
    }

    texgens->ordinary_matrix[0].last_load_type =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX2X4;
    texgens->ordinary_matrix[0].last_written_word_count =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_2X4;
    texgens->ordinary_matrix[0].known_word_mask =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_2X4;
    memcpy(texgens->ordinary_matrix[0].words,
           ordinary30_words, sizeof(ordinary30_words));
    texgens->ordinary_matrix[10].last_load_type =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX3X4;
    texgens->ordinary_matrix[10].last_written_word_count =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_3X4;
    texgens->ordinary_matrix[10].known_word_mask =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4;
    memcpy(texgens->ordinary_matrix[10].words,
           identity_words, sizeof(identity_words));
    texgens->post_matrix[20].last_load_type =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX3X4;
    texgens->post_matrix[20].last_written_word_count =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_3X4;
    texgens->post_matrix[20].known_word_mask =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4;
    memcpy(texgens->post_matrix[20].words,
           identity_words, sizeof(identity_words));

    for (vertex = 0; vertex < plan->geometry.vertex_count; vertex++) {
        plan->geometry.vertices[vertex].texture_matrix_id[0] = 30;
        plan->geometry.vertices[vertex].texture_matrix_id[1] = 60;
        for (word = 2; word < ACGC_GX_CANONICAL_TEXGEN_COUNT; word++) {
            plan->geometry.vertices[vertex].texture_matrix_id[word] = 0;
        }
    }
    return acgc_gx_canonical_texgen_state_validate(texgens);
}

static void make_lighting_record(
    AcgcGxCanonicalLightingRecord* record,
    uint32_t color,
    float position_x,
    float position_y,
    float position_z
) {
    uint32_t component;

    memset(record, 0, sizeof(*record));
    record->color_rgba8 = color;
    record->angular_attenuation[0] = bits_from_float(1.0f);
    record->angular_attenuation[1] = bits_from_float(2.0f);
    record->angular_attenuation[2] = bits_from_float(3.0f);
    record->distance_attenuation[0] = bits_from_float(4.0f);
    record->distance_attenuation[1] = bits_from_float(5.0f);
    record->distance_attenuation[2] = bits_from_float(6.0f);
    record->position[0] = bits_from_float(position_x);
    record->position[1] = bits_from_float(position_y);
    record->position[2] = bits_from_float(position_z);
    for (component = 0; component < 3; component++) {
        record->direction[component] = bits_from_float(-0.0f);
    }
}

static int make_af_none_lighting_plan(AcgcAppleCanonicalPlan* plan) {
    uint32_t vertex;

    if (plan == NULL || !make_source_geometry_plan(plan)) {
        return 0;
    }

    /* The source emu64 path uses one COLOR0A0 record, with COLOR0 enabled
     * REG/REG, mask 7, CLAMP/NONE and disabled vertex-sourced ALPHA0. The
     * captured light register shape is retained below; axis positions make
     * the CPU expected colors deterministic without using trace artifacts. */
    plan->transform.known_mask |=
        ACGC_GX_CANONICAL_TRANSFORM_NORMAL_KNOWN_MASK(0);
    plan->transform.normal[0][0] = bits_from_float(1.0f);
    plan->transform.normal[0][4] = bits_from_float(1.0f);
    plan->transform.normal[0][8] = bits_from_float(1.0f);

    plan->channels.records[0].color.enable =
        ACGC_GX_CANONICAL_CHANNEL_BOOLEAN_TRUE;
    plan->channels.records[0].color.ambient_source =
        ACGC_GX_CANONICAL_CHANNEL_SOURCE_REG;
    plan->channels.records[0].color.material_source =
        ACGC_GX_CANONICAL_CHANNEL_SOURCE_REG;
    plan->channels.records[0].color.light_mask = 7;
    plan->channels.records[0].color.diffuse_function =
        ACGC_GX_CANONICAL_CHANNEL_DIFFUSE_CLAMP;
    plan->channels.records[0].color.attenuation_function =
        ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_NONE;
    plan->channels.records[0].ambient_rgba8 = UINT32_C(0x00965050);
    plan->channels.records[0].material_rgba8 = UINT32_C(0xFFFFFFFF);

    plan->lighting.loaded_mask = 7;
    make_lighting_record(
        &plan->lighting.records[0], UINT32_C(0x00000000), 1.0f, 0.0f, 0.0f);
    make_lighting_record(
        &plan->lighting.records[1], UINT32_C(0x00070002), 0.0f, 1.0f, 0.0f);
    make_lighting_record(
        &plan->lighting.records[2], UINT32_C(0x00F0F0C8), 0.0f, 0.0f, 1.0f);

    plan->geometry.vertices[0].normal[0] = bits_from_float(1.0f);
    plan->geometry.vertices[0].normal[1] = bits_from_float(0.0f);
    plan->geometry.vertices[0].normal[2] = bits_from_float(0.0f);
    plan->geometry.vertices[1].normal[0] = bits_from_float(0.0f);
    plan->geometry.vertices[1].normal[1] = bits_from_float(1.0f);
    plan->geometry.vertices[1].normal[2] = bits_from_float(0.0f);
    plan->geometry.vertices[2].normal[0] = bits_from_float(0.0f);
    plan->geometry.vertices[2].normal[1] = bits_from_float(0.0f);
    plan->geometry.vertices[2].normal[2] = bits_from_float(1.0f);
    plan->geometry.vertices[0].color_rgba8[0] = UINT32_C(0x44332211);
    plan->geometry.vertices[1].color_rgba8[0] = UINT32_C(0x88776655);
    plan->geometry.vertices[2].color_rgba8[0] = UINT32_C(0xCCBBAA99);
    for (vertex = 0; vertex < plan->geometry.vertex_count; vertex++) {
        if (plan->geometry.vertices[vertex].normal[0] == 0 &&
            plan->geometry.vertices[vertex].normal[1] == 0 &&
            plan->geometry.vertices[vertex].normal[2] == 0) {
            return 0;
        }
    }
    return acgc_gx_canonical_transform_state_validate(&plan->transform) &&
        acgc_gx_canonical_channel_state_validate(&plan->channels) &&
        acgc_gx_canonical_lighting_state_validate(&plan->lighting);
}

static int test_af_none_lighting(
    AcgcMetalPacketConsumerOutput* output
) {
    static const uint32_t expected_colors[3] = {
        UINT32_C(0x50509644),
        UINT32_C(0x52509D88),
        UINT32_C(0xFFFFFFCC)
    };
    AcgcAppleCanonicalPlan plan;
    AcgcAppleCanonicalPlan before_plan;
    AcgcAppleCanonicalPlan mutated;
    AcgcMetalPacketConsumerOutput before_output;
    uint32_t vertex;

#define EXPECT_LIGHTING_REJECTION(expected_status) do { \
    if (!expect_rejection_status(&mutated, output, (expected_status))) { \
        return 0; \
    } \
} while (0)

    if (output == NULL || !make_af_none_lighting_plan(&plan)) {
        return 0;
    }
    before_plan = plan;
    memset(output, 0xA5, sizeof(*output));
    {
        AcgcMetalPacketConsumerStatus status =
            prepare_default_plan(&plan, output);

        if (status != ACGC_METAL_PACKET_CONSUMER_OK ||
            memcmp(&before_plan, &plan, sizeof(before_plan)) != 0) {
            return 0;
        }
    }
    for (vertex = 0; vertex < 3; vertex++) {
        const AcgcAppleCanonicalPlanVertex* source =
            &plan.geometry.vertices[vertex];
        const AcgcRendererVertex* destination =
            &output->geometry.vertices[vertex];

        if (destination->position_x != source->position[0] ||
            destination->position_y != source->position[1] ||
            destination->position_z != source->position[2] ||
            destination->color_rgba8 != expected_colors[vertex]) {
            return 0;
        }
    }

    before_output = *output;
    mutated = plan;
    mutated.channels.records[0].color.material_source =
        ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX;
    EXPECT_LIGHTING_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_CHANNELS_UNSUPPORTED);
    mutated = plan;
    mutated.channels.records[0].color.ambient_source =
        ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX;
    EXPECT_LIGHTING_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_CHANNELS_UNSUPPORTED);
    mutated = plan;
    mutated.channels.records[0].color.attenuation_function =
        ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_SPOT;
    EXPECT_LIGHTING_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_CHANNELS_UNSUPPORTED);
    mutated = plan;
    mutated.channels.records[0].color.attenuation_function =
        ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_SPEC;
    EXPECT_LIGHTING_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_CHANNELS_UNSUPPORTED);
    mutated = plan;
    mutated.channels.records[0].alpha.enable =
        ACGC_GX_CANONICAL_CHANNEL_BOOLEAN_TRUE;
    EXPECT_LIGHTING_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_CHANNELS_UNSUPPORTED);
    mutated = plan;
    mutated.channels.records[0].color.light_mask = 0;
    EXPECT_LIGHTING_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_CHANNELS_UNSUPPORTED);

    mutated = plan;
    mutated.lighting.loaded_mask = 3;
    EXPECT_LIGHTING_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_LIGHTING_UNSUPPORTED);
    mutated = plan;
    memset(mutated.lighting.records[1].position, 0,
           sizeof(mutated.lighting.records[1].position));
    EXPECT_LIGHTING_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_LIGHTING_UNSUPPORTED);
    mutated = plan;
    mutated.lighting.records[1].position[0] = UINT32_C(0x7FC00000);
    EXPECT_LIGHTING_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_LIGHTING_UNSUPPORTED);
    mutated = plan;
    mutated.geometry.present_mask &= ~(
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM);
    mutated.geometry.component_mask &=
        ~ACGC_APPLE_CANONICAL_PLAN_COMPONENT_NORMAL;
    for (vertex = 0; vertex < mutated.geometry.vertex_count; vertex++) {
        mutated.geometry.vertices[vertex].present_mask =
            mutated.geometry.present_mask;
        mutated.geometry.vertices[vertex].component_mask =
            mutated.geometry.component_mask;
        memset(mutated.geometry.vertices[vertex].normal, 0,
               sizeof(mutated.geometry.vertices[vertex].normal));
    }
    EXPECT_LIGHTING_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_LIGHTING_UNSUPPORTED);
    mutated = plan;
    mutated.transform.known_mask &=
        ~ACGC_GX_CANONICAL_TRANSFORM_NORMAL_KNOWN_MASK(0);
    memset(mutated.transform.normal[0], 0,
           sizeof(mutated.transform.normal[0]));
    EXPECT_LIGHTING_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_LIGHTING_UNSUPPORTED);
    mutated = plan;
    memset(mutated.transform.normal[0], 0,
           sizeof(mutated.transform.normal[0]));
    EXPECT_LIGHTING_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_LIGHTING_UNSUPPORTED);
    mutated = plan;
    mutated.geometry.vertices[0].normal[0] = UINT32_C(0x7FC00000);
    EXPECT_LIGHTING_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_GEOMETRY_UNSUPPORTED);
    mutated = plan;
    mutated.channels.records[1].channel_index = 1;
    EXPECT_LIGHTING_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_CHANNELS_UNSUPPORTED);

    if (memcmp(&before_output, output, sizeof(before_output)) != 0) {
        return 0;
    }
#undef EXPECT_LIGHTING_REJECTION
    return 1;
}

static uint32_t expected_renderer_color(uint32_t canonical_rgba8) {
    return ((canonical_rgba8 & UINT32_C(0x000000FF)) << 24) |
        ((canonical_rgba8 & UINT32_C(0x0000FF00)) << 8) |
        ((canonical_rgba8 & UINT32_C(0x00FF0000)) >> 8) |
        ((canonical_rgba8 & UINT32_C(0xFF000000)) >> 24);
}

static int check_output_vertex(
    const AcgcAppleCanonicalPlan* plan,
    const AcgcMetalPacketConsumerOutput* output,
    uint32_t output_vertex,
    uint32_t source_vertex
) {
    const AcgcAppleCanonicalPlanVertex* source =
        &plan->geometry.vertices[source_vertex];
    const AcgcRendererVertex* destination =
        &output->geometry.vertices[output_vertex];

    return destination->position_x == source->position[0] &&
        destination->position_y == source->position[1] &&
        destination->position_z == source->position[2] &&
        destination->color_rgba8 == expected_renderer_color(
            source->color_rgba8[0]);
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

static int make_semantic_packet_v2(AcgcGxSemanticPacketV2* packet) {
    AcgcGxSemanticV2TevStage* stage;
    uint32_t input;

    if (packet == NULL ||
        !acgc_gx_semantic_packet_v2_init(packet) ||
        !make_semantic_packet(&packet->base)) {
        return 0;
    }
    packet->base.version = ACGC_GX_SEMANTIC_PACKET_V2_VERSION;
    packet->base.byte_size = ACGC_GX_SEMANTIC_PACKET_V2_SIZE;
    packet->base.material.flags = ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR;
    packet->state_mask = ACGC_GX_SEMANTIC_V2_STATE_SUPPORTED;
    packet->projection_type = ACGC_GX_SEMANTIC_V2_PROJECTION_ORTHOGRAPHIC;
    packet->channel_count = 1;
    packet->texture_generator_count = 1;
    packet->tev_stage_count = 1;
    packet->channels[0].ambient_source =
        ACGC_GX_SEMANTIC_V2_CHANNEL_SOURCE_REGISTER;
    packet->channels[0].material_source =
        ACGC_GX_SEMANTIC_V2_CHANNEL_SOURCE_REGISTER;
    packet->channels[0].diffuse_function =
        ACGC_GX_SEMANTIC_V2_CHANNEL_DIFFUSE_NONE;
    packet->channels[0].attenuation_function =
        ACGC_GX_SEMANTIC_V2_CHANNEL_ATTENUATION_NONE;
    packet->texture_generators[0].enabled = 1;
    packet->texture_generators[0].coordinate_index = 0;
    packet->texture_generators[0].function =
        ACGC_GX_SEMANTIC_V2_TEXGEN_FUNCTION_MTX2X4;
    packet->texture_generators[0].source =
        ACGC_GX_SEMANTIC_V2_TEXGEN_SOURCE_TEX0;
    packet->texture_generators[0].matrix =
        ACGC_GX_SEMANTIC_V2_TEXGEN_MATRIX_IDENTITY;
    packet->texture_generators[0].texture_key = 42;
    packet->texture_generators[0].sampler_key = 42;
    packet->texture_generators[0].width = 8;
    packet->texture_generators[0].height = 8;
    packet->texture_generators[0].format =
        ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_I4;

    stage = &packet->tev_stages[0];
    for (input = 0; input < 4; input++) {
        stage->color_input[input] =
            ACGC_GX_SEMANTIC_V2_COLOR_INPUT_ZERO;
        stage->alpha_input[input] =
            ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_ZERO;
    }
    stage->color_operation = ACGC_GX_SEMANTIC_V2_TEV_OP_ADD;
    stage->alpha_operation = ACGC_GX_SEMANTIC_V2_TEV_OP_ADD;
    stage->color_bias = ACGC_GX_SEMANTIC_V2_TEV_BIAS_ZERO;
    stage->alpha_bias = ACGC_GX_SEMANTIC_V2_TEV_BIAS_ZERO;
    stage->color_scale = ACGC_GX_SEMANTIC_V2_TEV_SCALE_ONE;
    stage->alpha_scale = ACGC_GX_SEMANTIC_V2_TEV_SCALE_ONE;
    stage->color_clamp = 1;
    stage->alpha_clamp = 1;
    stage->color_output = ACGC_GX_SEMANTIC_V2_TEV_OUTPUT_PREVIOUS;
    stage->alpha_output = ACGC_GX_SEMANTIC_V2_TEV_OUTPUT_PREVIOUS;
    stage->texture_coordinate_index = 0;
    stage->texture_index = 0;
    stage->raster_channel_index = 0;
    stage->constant_color_selector =
        ACGC_GX_SEMANTIC_V2_TEV_KCOLOR_ONE;
    stage->constant_alpha_selector =
        ACGC_GX_SEMANTIC_V2_TEV_KALPHA_ONE;
    return acgc_gx_semantic_packet_v2_validate(packet);
}

static int run_rejection_matrix(const AcgcAppleCanonicalPlan* base,
                                AcgcMetalPacketConsumerOutput* output) {
    AcgcAppleCanonicalPlan mutated;

#define EXPECT_CANONICAL_REJECTION(expected_status) do { \
    if (!expect_rejection_status(&mutated, output, (expected_status))) { \
        return 0; \
    } \
} while (0)

    mutated = *base;
    mutated.geometry.vertex_count = 0;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_GEOMETRY_UNSUPPORTED);
    mutated = *base;
    mutated.geometry.vertex_count =
        ACGC_APPLE_CANONICAL_PLAN_MAX_VERTEX_COUNT + 1;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_GEOMETRY_UNSUPPORTED);
    mutated = *base;
    mutated.geometry.vertex_count = 4;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_GEOMETRY_UNSUPPORTED);
    mutated = *base;
    mutated.geometry.primitive = ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS;
    mutated.geometry.vertex_count = 0;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_GEOMETRY_UNSUPPORTED);
    mutated = *base;
    mutated.geometry.primitive = ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_GEOMETRY_UNSUPPORTED);
    mutated = *base;
    mutated.geometry.primitive = ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS;
    mutated.geometry.vertex_count =
        ACGC_APPLE_CANONICAL_PLAN_MAX_VERTEX_COUNT + 1;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_GEOMETRY_UNSUPPORTED);
    mutated = *base;
    mutated.geometry.vertex_count = 5;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_GEOMETRY_UNSUPPORTED);
    mutated = *base;
    mutated.geometry.vertex_count = 128;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_GEOMETRY_UNSUPPORTED);
    mutated = *base;
    mutated.geometry.primitive = ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS;
    mutated.geometry.vertex_count = 6;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_GEOMETRY_UNSUPPORTED);
    mutated = *base;
    mutated.geometry.primitive = 0;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_GEOMETRY_UNSUPPORTED);
    mutated = *base;
    mutated.geometry.component_mask |=
        ACGC_APPLE_CANONICAL_PLAN_COMPONENT_NORMAL;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_GEOMETRY_UNSUPPORTED);
    mutated = *base;
    mutated.geometry.vertices[0].position[0] = UINT32_C(0x7F800000);
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_GEOMETRY_UNSUPPORTED);
    mutated = *base;
    mutated.geometry.vertices[1].position_matrix_id = 3;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_GEOMETRY_UNSUPPORTED);
    mutated = *base;
    mutated.transform.known_mask &=
        ~ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(0);
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TRANSFORM_UNSUPPORTED);
    mutated = *base;
    mutated.transform.projection[0] = UINT32_C(0x7FC00000);
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TRANSFORM_UNSUPPORTED);
    mutated = *base;
    mutated.channels.records[0].color.enable = 1;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_CHANNELS_UNSUPPORTED);
    mutated = *base;
    mutated.texgens.header.active_texgen_count = 1;
    mutated.texgens.header.known_texgen_count = 1;
    mutated.texgens.header.texgen_known_mask = 1;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEXGENS_UNSUPPORTED);
    mutated = *base;
    mutated.texgens.header.active_texgen_count = 1;
    mutated.texgens.header.known_texgen_count = 1;
    mutated.texgens.header.texgen_known_mask = 1;
    mutated.texgens.texgen[0].function =
        ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP0;
    mutated.texgens.texgen[0].component_known =
        ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEXGENS_UNSUPPORTED);
    mutated = *base;
    mutated.texture.header.known_map_mask = 1;
    mutated.texture.header.known_map_count = 1;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEXTURE_UNSUPPORTED);
    mutated = *base;
    mutated.texture.header.tlut_present_map_mask = 1;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEXTURE_UNSUPPORTED);
    mutated = *base;
    mutated.tev.stages[0].tex_map = 0;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_UNSUPPORTED);
    mutated = *base;
    mutated.lighting.loaded_mask = 1;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_LIGHTING_UNSUPPORTED);
    mutated = *base;
    mutated.alpha.comp0 = ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX + 1;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_UNSUPPORTED);
    mutated = *base;
    mutated.alpha.color_update_enable = ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX + 1;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_UNSUPPORTED);
    mutated = *base;
    mutated.depth.z_compare_enable = 0;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_DEPTH_UNSUPPORTED);
    mutated = *base;
    mutated.raster.reserved[0] = 1;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_UNSUPPORTED);
    mutated = *base;
    mutated.raster.viewport_bits[0] = UINT32_C(0x7FC00000);
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_UNSUPPORTED);
    mutated = *base;
    mutated.raster.scissor[0] = ACGC_GX_CANONICAL_RASTER_SCISSOR_LIMIT;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_UNSUPPORTED);
    mutated = *base;
    mutated.raster.cull_mode = ACGC_GX_CANONICAL_RASTER_CULL_MODE_ALL + 1;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_UNSUPPORTED);
    mutated = *base;
    mutated.fog.reserved[0] = 1;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_FOG_UNSUPPORTED);
    mutated = *base;
    mutated.fog.fog_type = ACGC_GX_CANONICAL_FOG_TYPE_PERSP_LIN;
    mutated.fog.start_bits = UINT32_C(0x7FC00000);
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_FOG_UNSUPPORTED);
    mutated = *base;
    mutated.indirect.header.active_indirect_stage_count = 1;
    mutated.indirect.header.active_order_mask = 1;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_INDIRECT_UNSUPPORTED);
    mutated = *base;
    mutated.dynamic.header.present_image_mask = 1;
    mutated.dynamic.header.required_image_mask = 1;
    mutated.dynamic.header.present_resource_count = 1;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_DYNAMIC_UNSUPPORTED);
#undef EXPECT_CANONICAL_REJECTION
    return 1;
}

static int test_canonical_alpha_disposition(
    const AcgcAppleCanonicalPlan* base,
    AcgcMetalPacketConsumerOutput* output
) {
    typedef struct AlphaTautologyCase {
        uint32_t comp0;
        uint32_t ref0;
        uint32_t op;
        uint32_t comp1;
        uint32_t ref1;
    } AlphaTautologyCase;
    static const AlphaTautologyCase tautologies[] = {
        {
            ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX, 17,
            ACGC_GX_CANONICAL_ALPHA_OPERATOR_MIN,
            ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX, 29
        },
        {
            ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN, 73,
            ACGC_GX_CANONICAL_ALPHA_OPERATOR_MIN + 1,
            ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX, 211
        },
        {
            ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN + 1, 127,
            ACGC_GX_CANONICAL_ALPHA_OPERATOR_MIN + 2,
            ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN + 6, 127
        },
        {
            ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN + 1, 91,
            ACGC_GX_CANONICAL_ALPHA_OPERATOR_MAX,
            ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN + 1, 91
        }
    };
    AcgcAppleCanonicalPlan mutated;
    AcgcAppleCanonicalPlan before_plan;
    AcgcMetalPacketConsumerOutput before_output;
    AcgcGxCanonicalAlphaState published;
    size_t index;

    if (base == NULL || output == NULL) {
        return 0;
    }

    /* Every GX operator is evaluated over the complete 8-bit fragment domain. */
    for (index = 0;
         index < sizeof(tautologies) / sizeof(tautologies[0]);
         index++) {
        mutated = *base;
        mutated.alpha.comp0 = tautologies[index].comp0;
        mutated.alpha.ref0 = tautologies[index].ref0;
        mutated.alpha.op = tautologies[index].op;
        mutated.alpha.comp1 = tautologies[index].comp1;
        mutated.alpha.ref1 = tautologies[index].ref1;
        mutated.alpha.color_update_enable =
            ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX;
        mutated.alpha.alpha_update_enable = (uint32_t)(index & 1);
        mutated.alpha.z_comp_loc_before_tex = 1;
        before_plan = mutated;
        if (prepare_default_plan(&mutated, output) !=
                ACGC_METAL_PACKET_CONSUMER_OK ||
            output->canonical_alpha_disposition !=
                ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_DISPOSITION_PASSTHROUGH ||
            memcmp(&output->canonical_alpha, &mutated.alpha,
                   sizeof(mutated.alpha)) != 0 ||
            output->alpha_write_enabled != mutated.alpha.alpha_update_enable ||
            memcmp(&before_plan, &mutated, sizeof(before_plan)) != 0) {
            return 0;
        }
        published = output->canonical_alpha;
        mutated.alpha.ref0 =
            (mutated.alpha.ref0 + 1) &
            ACGC_GX_CANONICAL_ALPHA_REFERENCE_MAX;
        if (memcmp(&published, &output->canonical_alpha,
                   sizeof(published)) != 0) {
            return 0;
        }
    }

    /* A valid active comparison is staged instead of being falsely rendered. */
    mutated = *base;
    mutated.alpha.comp0 = ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN + 1;
    mutated.alpha.ref0 = 127;
    mutated.alpha.op = ACGC_GX_CANONICAL_ALPHA_OPERATOR_MIN;
    mutated.alpha.comp1 = ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX;
    mutated.alpha.ref1 = 251;
    mutated.alpha.color_update_enable =
        ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX;
    mutated.alpha.alpha_update_enable =
        ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MIN;
    mutated.alpha.z_comp_loc_before_tex = 1;
    before_plan = mutated;
    if (prepare_default_plan(&mutated, output) !=
            ACGC_METAL_PACKET_CONSUMER_OK ||
        output->canonical_alpha_disposition !=
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_DISPOSITION_STAGED_UNRENDERED ||
        memcmp(&output->canonical_alpha, &mutated.alpha,
               sizeof(mutated.alpha)) != 0 ||
        output->alpha_write_enabled != ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MIN ||
        memcmp(&before_plan, &mutated, sizeof(before_plan)) != 0) {
        return 0;
    }
    published = output->canonical_alpha;
    mutated.alpha.comp0 = ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX;
    if (memcmp(&published, &output->canonical_alpha, sizeof(published)) != 0) {
        return 0;
    }

    /* RGB writes disabled remain staged even when the predicate is tautological. */
    mutated = *base;
    mutated.alpha.color_update_enable =
        ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MIN;
    mutated.alpha.alpha_update_enable =
        ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX;
    mutated.alpha.z_comp_loc_before_tex = 1;
    if (prepare_default_plan(&mutated, output) !=
            ACGC_METAL_PACKET_CONSUMER_OK ||
        output->canonical_alpha_disposition !=
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_DISPOSITION_STAGED_UNRENDERED ||
        memcmp(&output->canonical_alpha, &mutated.alpha,
               sizeof(mutated.alpha)) != 0 ||
        output->alpha_write_enabled != ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX) {
        return 0;
    }

    /* Structural Alpha failures preserve a prefilled output byte-for-byte. */
#define EXPECT_INVALID_ALPHA(field, value) do { \
    mutated = *base; \
    mutated.alpha.field = (value); \
    before_plan = mutated; \
    memset(output, 0xA5, sizeof(*output)); \
    before_output = *output; \
    if (prepare_default_plan(&mutated, output) != \
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_UNSUPPORTED || \
        memcmp(&before_output, output, sizeof(before_output)) != 0 || \
        memcmp(&before_plan, &mutated, sizeof(before_plan)) != 0) { \
        return 0; \
    } \
} while (0)
    EXPECT_INVALID_ALPHA(
        comp0, ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX + 1);
    EXPECT_INVALID_ALPHA(
        ref0, ACGC_GX_CANONICAL_ALPHA_REFERENCE_MAX + 1);
    EXPECT_INVALID_ALPHA(
        op, ACGC_GX_CANONICAL_ALPHA_OPERATOR_MAX + 1);
    EXPECT_INVALID_ALPHA(
        comp1, ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX + 1);
    EXPECT_INVALID_ALPHA(
        ref1, ACGC_GX_CANONICAL_ALPHA_REFERENCE_MAX + 1);
    EXPECT_INVALID_ALPHA(
        color_update_enable, ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX + 1);
    EXPECT_INVALID_ALPHA(
        alpha_update_enable, ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX + 1);
    EXPECT_INVALID_ALPHA(
        z_comp_loc_before_tex, ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX + 1);
#undef EXPECT_INVALID_ALPHA
    return 1;
}

static int test_canonical_raster_disposition(
    const AcgcAppleCanonicalPlan* base,
    AcgcMetalPacketConsumerOutput* output
) {
    AcgcAppleCanonicalPlan mutated;
    AcgcAppleCanonicalPlan before_plan;
    AcgcGxCanonicalRasterState published;

    if (base == NULL || output == NULL) {
        return 0;
    }

    mutated = *base;
    before_plan = mutated;
    if (prepare_default_plan(&mutated, output) !=
            ACGC_METAL_PACKET_CONSUMER_OK ||
        output->canonical_raster_disposition !=
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_DISPOSITION_MAPPED ||
        memcmp(&output->canonical_raster, &mutated.raster,
               sizeof(mutated.raster)) != 0 ||
        output->state.viewport.origin_x != ACGC_METAL_FLOAT_ZERO ||
        output->state.viewport.origin_y != ACGC_METAL_FLOAT_ZERO ||
        output->state.viewport.width != ACGC_METAL_FLOAT_SIXTY_FOUR ||
        output->state.viewport.height != ACGC_METAL_FLOAT_SIXTY_FOUR ||
        output->state.viewport.znear != ACGC_METAL_FLOAT_ZERO ||
        output->state.viewport.zfar != ACGC_METAL_FLOAT_ONE ||
        output->state.raster.cull_mode != ACGC_METAL_CULL_BACK ||
        memcmp(&before_plan, &mutated, sizeof(before_plan)) != 0) {
        return 0;
    }
    published = output->canonical_raster;
    mutated.raster.cull_mode = ACGC_GX_CANONICAL_RASTER_CULL_MODE_FRONT;
    if (memcmp(&published, &output->canonical_raster, sizeof(published)) != 0) {
        return 0;
    }

    /* The decomp GXNtsc480IntDf initialization shape is admitted as the
     * bounded full-frame triangle subset. Every Raster word remains exact. */
    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    before_plan = mutated;
    if (prepare_default_plan(&mutated, output) !=
            ACGC_METAL_PACKET_CONSUMER_OK ||
        output->canonical_raster_disposition !=
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_DISPOSITION_MAPPED ||
        memcmp(&output->canonical_raster, &mutated.raster,
               sizeof(mutated.raster)) != 0 ||
        output->state.viewport.origin_x != ACGC_METAL_FLOAT_ZERO ||
        output->state.viewport.origin_y != ACGC_METAL_FLOAT_ZERO ||
        output->state.viewport.width != bits_from_float(640.0f) ||
        output->state.viewport.height != bits_from_float(480.0f) ||
        output->state.viewport.znear != ACGC_METAL_FLOAT_ZERO ||
        output->state.viewport.zfar != ACGC_METAL_FLOAT_ONE ||
        output->state.raster.cull_mode != ACGC_METAL_CULL_BACK ||
        output->state.raster.front_facing_winding !=
            ACGC_METAL_WINDING_COUNTER_CLOCKWISE ||
        output->state.raster.triangle_fill_mode != ACGC_METAL_TRIANGLE_FILL ||
        !acgc_metal_state_fixture_validate(&output->state) ||
        memcmp(&before_plan, &mutated, sizeof(before_plan)) != 0) {
        return 0;
    }
    published = output->canonical_raster;
    mutated.raster.scissor[2] = 641;
    if (memcmp(&published, &output->canonical_raster, sizeof(published)) != 0) {
        return 0;
    }

    /* Each structurally valid line/point word outside the exact decomp shape
     * remains staged while the complete canonical value is preserved. */
    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.line_width = 6;
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.line_tex_offsets = 1;
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.point_size = 7;
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.point_tex_offsets = 1;
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.line_texcoord_mask = 1;
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.point_texcoord_mask = 1;
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    /* Valid canonical Raster outside the narrow triangle subset remains
     * value-preserved but staged, including dimensions and active features. */
    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.viewport_bits[2] = bits_from_float(0.0f);
    mutated.raster.scissor[2] = 0;
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.viewport_bits[2] = bits_from_float(-1.0f);
    mutated.raster.scissor[2] = 1;
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.viewport_bits[2] = bits_from_float(640.5f);
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.viewport_bits[2] = bits_from_float(1706.0f);
    mutated.raster.scissor[2] = 1705;
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.scissor[2] = 639;
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.scissor[0] = 1;
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.scissor_offset[0] = 1;
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.clip_mode = ACGC_GX_CANONICAL_RASTER_CLIP_MODE_DISABLE;
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.cull_mode = ACGC_GX_CANONICAL_RASTER_CULL_MODE_ALL;
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.co_planar_enable = 1;
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.dst_alpha_enable = 1;
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.dither = 0;
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.field_mode = 1;
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.half_aspect_ratio = 1;
    if (!expect_staged_raster(&mutated, output)) {
        return 0;
    }

    mutated = *base;
    set_decomp_dynamic_raster(&mutated.raster);
    mutated.raster.viewport_bits[2] = UINT32_C(0x7FC00000);
    if (!expect_rejection_status(
            &mutated,
            output,
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_UNSUPPORTED)) {
        return 0;
    }
    return 1;
}

static int test_canonical_fog_disposition(
    const AcgcAppleCanonicalPlan* base,
    AcgcMetalPacketConsumerOutput* output
) {
    AcgcAppleCanonicalPlan mutated;
    AcgcAppleCanonicalPlan before_plan;
    AcgcGxCanonicalFogState published;
    uint32_t index;

    if (base == NULL || output == NULL) {
        return 0;
    }

    /* The existing all-zero plan is a valid inactive Fog value. */
    mutated = *base;
    before_plan = mutated;
    if (prepare_default_plan(&mutated, output) !=
            ACGC_METAL_PACKET_CONSUMER_OK ||
        output->canonical_fog_disposition !=
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_FOG_DISPOSITION_INACTIVE ||
        memcmp(&output->canonical_fog, &mutated.fog,
               sizeof(mutated.fog)) != 0 ||
        memcmp(&before_plan, &mutated, sizeof(before_plan)) != 0) {
        return 0;
    }
    published = output->canonical_fog;
    mutated.fog.color_rgba8 = UINT32_C(0x01020304);
    if (memcmp(&published, &output->canonical_fog, sizeof(published)) != 0) {
        return 0;
    }

    /* Match the decomp's inactive parameter shape while retaining valid
     * nonzero words that a disabled range setter must not erase. */
    mutated = *base;
    mutated.fog.fog_type = ACGC_GX_CANONICAL_FOG_TYPE_NONE;
    mutated.fog.start_bits = bits_from_float(0.0f);
    mutated.fog.end_bits = bits_from_float(1.0f);
    mutated.fog.near_bits = bits_from_float(0.1f);
    mutated.fog.far_bits = bits_from_float(1.0f);
    mutated.fog.color_rgba8 = UINT32_C(0x44332211);
    mutated.fog.range_adjust_enable = 0;
    mutated.fog.range_center = 42;
    for (index = 0; index < ACGC_GX_CANONICAL_FOG_RANGE_COUNT; index++) {
        mutated.fog.range_adjust[index] = UINT32_C(0x0100) + index;
    }
    before_plan = mutated;
    if (prepare_default_plan(&mutated, output) !=
            ACGC_METAL_PACKET_CONSUMER_OK ||
        output->canonical_fog_disposition !=
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_FOG_DISPOSITION_INACTIVE ||
        memcmp(&output->canonical_fog, &mutated.fog,
               sizeof(mutated.fog)) != 0 ||
        memcmp(&before_plan, &mutated, sizeof(before_plan)) != 0) {
        return 0;
    }
    published = output->canonical_fog;
    mutated.fog.range_adjust[0]++;
    if (memcmp(&published, &output->canonical_fog, sizeof(published)) != 0) {
        return 0;
    }

    /* Valid active Fog advances typed parsing but remains unrendered. */
    mutated = *base;
    mutated.fog.fog_type = ACGC_GX_CANONICAL_FOG_TYPE_PERSP_LIN;
    mutated.fog.start_bits = bits_from_float(0.25f);
    mutated.fog.end_bits = bits_from_float(0.75f);
    mutated.fog.near_bits = bits_from_float(0.1f);
    mutated.fog.far_bits = bits_from_float(1.0f);
    mutated.fog.color_rgba8 = UINT32_C(0x88776655);
    before_plan = mutated;
    if (prepare_default_plan(&mutated, output) !=
            ACGC_METAL_PACKET_CONSUMER_OK ||
        output->canonical_fog_disposition !=
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_FOG_DISPOSITION_STAGED_UNRENDERED ||
        memcmp(&output->canonical_fog, &mutated.fog,
               sizeof(mutated.fog)) != 0 ||
        memcmp(&before_plan, &mutated, sizeof(before_plan)) != 0) {
        return 0;
    }
    published = output->canonical_fog;
    mutated.fog.far_bits = bits_from_float(2.0f);
    if (memcmp(&published, &output->canonical_fog, sizeof(published)) != 0) {
        return 0;
    }

    /* Range adjustment alone is also staged even when Fog math is disabled. */
    mutated = *base;
    mutated.fog.range_adjust_enable = 1;
    mutated.fog.range_center = 7;
    for (index = 0; index < ACGC_GX_CANONICAL_FOG_RANGE_COUNT; index++) {
        mutated.fog.range_adjust[index] = UINT32_C(0x0200) + index;
    }
    before_plan = mutated;
    if (prepare_default_plan(&mutated, output) !=
            ACGC_METAL_PACKET_CONSUMER_OK ||
        output->canonical_fog_disposition !=
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_FOG_DISPOSITION_STAGED_UNRENDERED ||
        memcmp(&output->canonical_fog, &mutated.fog,
               sizeof(mutated.fog)) != 0 ||
        memcmp(&before_plan, &mutated, sizeof(before_plan)) != 0) {
        return 0;
    }

    mutated = *base;
    mutated.fog.reserved[0] = 1;
    if (!expect_rejection_status(
            &mutated,
            output,
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_FOG_UNSUPPORTED)) {
        return 0;
    }
    mutated = *base;
    mutated.fog.fog_type = ACGC_GX_CANONICAL_FOG_TYPE_PERSP_LIN;
    mutated.fog.start_bits = UINT32_C(0x7FC00000);
    mutated.fog.end_bits = bits_from_float(1.0f);
    mutated.fog.near_bits = bits_from_float(0.1f);
    mutated.fog.far_bits = bits_from_float(1.0f);
    if (!expect_rejection_status(
            &mutated,
            output,
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_FOG_UNSUPPORTED)) {
        return 0;
    }
    mutated = *base;
    mutated.fog.range_center = ACGC_GX_CANONICAL_FOG_CENTER_SOURCE_MAX + 1;
    return expect_rejection_status(
        &mutated,
        output,
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_FOG_UNSUPPORTED
    );
}

static int test_source_geometry_attributes(
    AcgcMetalPacketConsumerOutput* output
) {
    const uint32_t normal_mask =
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM;
    const uint32_t texcoord0_mask =
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0;
    AcgcAppleCanonicalPlan plan;
    AcgcAppleCanonicalPlan before_plan;
    AcgcAppleCanonicalPlan mutated;
    uint32_t vertex;

#define EXPECT_SOURCE_GEOMETRY_REJECTION() do { \
    if (!expect_rejection_status( \
            &mutated, output, \
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_GEOMETRY_UNSUPPORTED)) { \
        return 0; \
    } \
} while (0)

    if (output == NULL || !make_source_geometry_plan(&plan)) {
        return 0;
    }
    before_plan = plan;
    memset(output, 0xA5, sizeof(*output));
    if (prepare_default_plan(&plan, output) !=
            ACGC_METAL_PACKET_CONSUMER_OK) {
        return 0;
    }
    if (output->geometry.vertex_count != 3 ||
        output->geometry.draw_count != 1 ||
        !acgc_renderer_geometry_validate(&output->geometry) ||
        memcmp(&before_plan, &plan, sizeof(before_plan)) != 0) {
        return 0;
    }
    for (vertex = 0; vertex < 3; vertex++) {
        if (!check_output_vertex(&plan, output, vertex, vertex)) {
            return 0;
        }
    }

    mutated = plan;
    mutated.geometry.vertices[0].normal[0] = UINT32_C(0x7FC00000);
    EXPECT_SOURCE_GEOMETRY_REJECTION();
    mutated = plan;
    mutated.geometry.vertices[0].normal[0] = UINT32_C(0x7F800000);
    EXPECT_SOURCE_GEOMETRY_REJECTION();
    mutated = plan;
    mutated.geometry.vertices[0].texcoord[0][0] = UINT32_C(0x7FC00000);
    EXPECT_SOURCE_GEOMETRY_REJECTION();
    mutated = plan;
    mutated.geometry.vertices[0].texcoord[0][0] = UINT32_C(0x7F800000);
    EXPECT_SOURCE_GEOMETRY_REJECTION();

    /* The normalized component/presence contract is repeated per vertex. */
    mutated = plan;
    mutated.geometry.component_mask &=
        ~ACGC_APPLE_CANONICAL_PLAN_COMPONENT_NORMAL;
    EXPECT_SOURCE_GEOMETRY_REJECTION();
    mutated = plan;
    mutated.geometry.vertices[1].present_mask &= ~texcoord0_mask;
    EXPECT_SOURCE_GEOMETRY_REJECTION();
    mutated = plan;
    mutated.geometry.present_mask &= ~normal_mask;
    mutated.geometry.component_mask &=
        ~ACGC_APPLE_CANONICAL_PLAN_COMPONENT_NORMAL;
    for (vertex = 0; vertex < mutated.geometry.vertex_count; vertex++) {
        mutated.geometry.vertices[vertex].present_mask =
            mutated.geometry.present_mask;
        mutated.geometry.vertices[vertex].component_mask =
            mutated.geometry.component_mask;
    }
    EXPECT_SOURCE_GEOMETRY_REJECTION();
    mutated = plan;
    mutated.geometry.present_mask &= ~texcoord0_mask;
    mutated.geometry.component_mask &=
        ~ACGC_APPLE_CANONICAL_PLAN_COMPONENT_TEXCOORD0;
    for (vertex = 0; vertex < mutated.geometry.vertex_count; vertex++) {
        mutated.geometry.vertices[vertex].present_mask =
            mutated.geometry.present_mask;
        mutated.geometry.vertices[vertex].component_mask =
            mutated.geometry.component_mask;
    }
    EXPECT_SOURCE_GEOMETRY_REJECTION();

    /* NBT, COLOR1, TEX1, and a texture-matrix selector are outside this
     * consumer's bounded Geometry set; their source words remain untouched. */
    mutated = plan;
    mutated.geometry.vertices[0].binormal[0] = bits_from_float(1.0f);
    EXPECT_SOURCE_GEOMETRY_REJECTION();
    mutated = plan;
    mutated.geometry.vertices[0].tangent[0] = bits_from_float(1.0f);
    EXPECT_SOURCE_GEOMETRY_REJECTION();
    mutated = plan;
    mutated.geometry.vertices[0].color_rgba8[1] = UINT32_C(0x01020304);
    EXPECT_SOURCE_GEOMETRY_REJECTION();
    mutated = plan;
    mutated.geometry.vertices[0].texcoord[1][0] = bits_from_float(1.0f);
    EXPECT_SOURCE_GEOMETRY_REJECTION();
    mutated = plan;
    mutated.geometry.vertices[0].texture_matrix_id[1] = UINT32_C(1);
    EXPECT_SOURCE_GEOMETRY_REJECTION();

    mutated = plan;
    mutated.geometry.present_mask |=
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NBT;
    for (vertex = 0; vertex < mutated.geometry.vertex_count; vertex++) {
        mutated.geometry.vertices[vertex].present_mask =
            mutated.geometry.present_mask;
    }
    EXPECT_SOURCE_GEOMETRY_REJECTION();
    mutated = plan;
    mutated.geometry.present_mask |=
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR1;
    for (vertex = 0; vertex < mutated.geometry.vertex_count; vertex++) {
        mutated.geometry.vertices[vertex].present_mask =
            mutated.geometry.present_mask;
    }
    EXPECT_SOURCE_GEOMETRY_REJECTION();
    mutated = plan;
    mutated.geometry.present_mask |=
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX1;
    for (vertex = 0; vertex < mutated.geometry.vertex_count; vertex++) {
        mutated.geometry.vertices[vertex].present_mask =
            mutated.geometry.present_mask;
    }
    EXPECT_SOURCE_GEOMETRY_REJECTION();
    mutated = plan;
    mutated.geometry.present_mask |=
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0MTXIDX;
    for (vertex = 0; vertex < mutated.geometry.vertex_count; vertex++) {
        mutated.geometry.vertices[vertex].present_mask =
            mutated.geometry.present_mask;
    }
    EXPECT_SOURCE_GEOMETRY_REJECTION();

#undef EXPECT_SOURCE_GEOMETRY_REJECTION
    return 1;
}

static int test_active_texgen_admission(
    AcgcMetalPacketConsumerOutput* output
) {
    const uint32_t tex1_mask =
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX1;
    AcgcAppleCanonicalPlan plan;
    AcgcAppleCanonicalPlan before_plan;
    AcgcAppleCanonicalPlan mutated;
    uint32_t vertex;

#define EXPECT_TEXGEN_REJECTION() do { \
    if (!expect_rejection_status( \
            &mutated, output, \
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEXGENS_UNSUPPORTED)) { \
        return 0; \
    } \
} while (0)
#define EXPECT_GEOMETRY_REJECTION() do { \
    if (!expect_rejection_status( \
            &mutated, output, \
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_GEOMETRY_UNSUPPORTED)) { \
        return 0; \
    } \
} while (0)

    if (output == NULL || !make_active_texgen_plan(&plan)) {
        return 0;
    }
    before_plan = plan;
    memset(output, 0xA5, sizeof(*output));
    if (prepare_default_plan(&plan, output) !=
            ACGC_METAL_PACKET_CONSUMER_OK ||
        memcmp(&before_plan, &plan, sizeof(before_plan)) != 0 ||
        output->geometry.vertex_count != 3 ||
        !acgc_renderer_geometry_validate(&output->geometry)) {
        return 0;
    }
    /* Texgen admission is deliberately bounded CPU state validation. The
     * renderer-facing value remains only position plus materialized color. */
    for (vertex = 0; vertex < plan.geometry.vertex_count; vertex++) {
        if (!check_output_vertex(&plan, output, vertex, vertex)) {
            return 0;
        }
    }

    mutated = plan;
    mutated.texgens.header.active_texgen_count = 1;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.texgen[0].function =
        ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX3X4;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.texgen[0].source =
        ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX1;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.texgen[0].normalize = 1;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.texgen[0].ordinary_matrix_id = 60;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.texgen[1].function =
        ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX3X4;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.texgen[1].source =
        ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX1;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.texgen[1].ordinary_matrix_id = 30;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.texgen[1].normalize = 1;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.texgen[1].post_matrix_id = 64;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.texgen[0].function =
        ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP0;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.texgen[0].function =
        ACGC_GX_CANONICAL_TEXGEN_FUNCTION_SRTG;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.header.ordinary_matrix_known_mask =
        UINT32_C(0x00000402);
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.header.post_matrix_known_mask = UINT32_C(0x00080000);
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.ordinary_matrix[0].last_load_type =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX3X4;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.ordinary_matrix[0].last_written_word_count =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_3X4;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.ordinary_matrix[0].known_word_mask =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.ordinary_matrix[0].words[5] = 0;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.ordinary_matrix[0].words[5] = UINT32_C(0x7FC00000);
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.ordinary_matrix[10].words[0] = 0;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.post_matrix[20].words[10] = 0;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.header.su_count = 1;
    mutated.texgens.header.su_known_mask = 1;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.su[0].manual_enable = 1;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.header.reserved[0] = 1;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.texgens.texgen[1].reserved[1] = 1;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.geometry.vertices[0].texture_matrix_id[0] = 0;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.geometry.vertices[1].texture_matrix_id[1] = 30;
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.geometry.present_mask &= ~(
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0);
    mutated.geometry.component_mask &=
        ~ACGC_APPLE_CANONICAL_PLAN_COMPONENT_TEXCOORD0;
    for (vertex = 0; vertex < mutated.geometry.vertex_count; vertex++) {
        mutated.geometry.vertices[vertex].present_mask =
            mutated.geometry.present_mask;
        mutated.geometry.vertices[vertex].component_mask =
            mutated.geometry.component_mask;
        memset(mutated.geometry.vertices[vertex].texcoord[0], 0,
               sizeof(mutated.geometry.vertices[vertex].texcoord[0]));
    }
    EXPECT_TEXGEN_REJECTION();
    mutated = plan;
    mutated.geometry.present_mask |= tex1_mask;
    for (vertex = 0; vertex < mutated.geometry.vertex_count; vertex++) {
        mutated.geometry.vertices[vertex].present_mask =
            mutated.geometry.present_mask;
    }
    EXPECT_GEOMETRY_REJECTION();

#undef EXPECT_GEOMETRY_REJECTION
#undef EXPECT_TEXGEN_REJECTION
    return 1;
}

static int test_multi_vertex_geometry(
    AcgcMetalPacketConsumerOutput* output
) {
    static const struct {
        uint32_t primitive;
        uint32_t input_vertex_count;
        uint32_t output_vertex_count;
    } cases[] = {
        {
            ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES,
            3,
            3
        },
        {
            ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES,
            6,
            6
        },
        {
            ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES,
            126,
            126
        },
        {
            ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS,
            4,
            6
        },
        {
            ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS,
            128,
            192
        }
    };
    static const uint32_t quad_triangle_order[6] = {0, 1, 2, 0, 2, 3};
    AcgcAppleCanonicalPlan plan;
    AcgcAppleCanonicalPlan before_plan;
    size_t case_index;

    if (output == NULL) {
        return 1;
    }
    for (case_index = 0;
         case_index < sizeof(cases) / sizeof(cases[0]);
         case_index++) {
        uint32_t output_vertex;

        CHECK(make_geometry_plan(
            &plan,
            cases[case_index].primitive,
            cases[case_index].input_vertex_count));
        before_plan = plan;
        memset(output, 0xA5, sizeof(*output));
        CHECK(prepare_default_plan(
                  &plan, output) == ACGC_METAL_PACKET_CONSUMER_OK);
        CHECK(output->geometry.vertex_count ==
              cases[case_index].output_vertex_count);
        CHECK(output->geometry.draw_count == 1);
        CHECK(output->geometry.draws[0].primitive ==
              ACGC_RENDERER_PRIMITIVE_TRIANGLES);
        CHECK(output->geometry.draws[0].first_vertex == 0);
        CHECK(output->geometry.draws[0].vertex_count ==
              cases[case_index].output_vertex_count);
        CHECK(acgc_renderer_geometry_validate(&output->geometry));

        for (output_vertex = 0;
             output_vertex < cases[case_index].output_vertex_count;
             output_vertex++) {
            uint32_t source_vertex = output_vertex;

            if (cases[case_index].primitive ==
                ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS) {
                source_vertex = (output_vertex / 6) * 4 +
                    quad_triangle_order[output_vertex % 6];
            }
            CHECK(check_output_vertex(
                &plan, output, output_vertex, source_vertex));
        }
        for (output_vertex = cases[case_index].output_vertex_count;
             output_vertex < ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
             output_vertex++) {
            CHECK(output->geometry.vertices[output_vertex].position_x == 0);
            CHECK(output->geometry.vertices[output_vertex].position_y == 0);
            CHECK(output->geometry.vertices[output_vertex].position_z == 0);
            CHECK(output->geometry.vertices[output_vertex].color_rgba8 == 0);
        }
        CHECK(memcmp(&before_plan, &plan, sizeof(before_plan)) == 0);
    }

    /* Negative control: corrupt one prepared vertex and require the same
     * per-vertex verifier used above to propagate the mismatch. */
    CHECK(make_geometry_plan(
        &plan, ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES, 3));
    CHECK(prepare_default_plan(
              &plan, output) == ACGC_METAL_PACKET_CONSUMER_OK);
    output->geometry.vertices[1].position_x ^= UINT32_C(1);
    CHECK(!check_output_vertex(&plan, output, 1, 1));
    return 0;
}

static int make_active_texture_plan_with_stage(
    AcgcAppleCanonicalPlan* plan,
    AcgcMetalPacketConsumerCanonicalResourceStage* resource_stage
) {
    AcgcGxCanonicalTextureRecord* texture_record;
    AcgcGxCanonicalDynamicRecord* image_record;
    AcgcGxCanonicalDynamicRecord* tlut_record;
    uint32_t image_byte_size;

    if (plan == NULL || resource_stage == NULL || !make_base_plan(plan)) {
        return 0;
    }
    plan->tev.swap_tables[0].r = 0;
    plan->tev.swap_tables[0].g = 1;
    plan->tev.swap_tables[0].b = 2;
    plan->tev.swap_tables[0].a = 3;
    plan->tev.swap_tables[1].r = 0;
    plan->tev.swap_tables[1].g = 0;
    plan->tev.swap_tables[1].b = 0;
    plan->tev.swap_tables[1].a = 3;
    plan->tev.swap_tables[2].r = 1;
    plan->tev.swap_tables[2].g = 1;
    plan->tev.swap_tables[2].b = 1;
    plan->tev.swap_tables[2].a = 3;
    plan->tev.swap_tables[3].r = 2;
    plan->tev.swap_tables[3].g = 2;
    plan->tev.swap_tables[3].b = 2;
    plan->tev.swap_tables[3].a = 3;
    image_byte_size = acgc_renderer_fixture_texture_bytes(
        8, 8, ACGC_RENDERER_FIXTURE_TF_C4);
    if (image_byte_size == 0) {
        return 0;
    }

    plan->texture.header.known_map_mask = 1;
    plan->texture.header.known_map_count = 1;
    plan->texture.header.indexed_map_mask = 1;
    plan->texture.header.tlut_present_map_mask = 1;
    plan->texture.header.required_map_mask = 1;
    texture_record = &plan->texture.records[0];
    texture_record->flags = ACGC_GX_CANONICAL_TEXTURE_FLAG_INDEXED |
        ACGC_GX_CANONICAL_TEXTURE_FLAG_TLUT_PRESENT |
        ACGC_GX_CANONICAL_TEXTURE_FLAG_RESOURCE_REQUIRED;
    texture_record->image_resource_id =
        ACGC_GX_CANONICAL_TEXTURE_IMAGE_RESOURCE_ID_BASE;
    texture_record->image_owner_epoch = 1;
    texture_record->image_generation_lo = 1;
    texture_record->width = 8;
    texture_record->height = 8;
    texture_record->image_format = ACGC_GX_CANONICAL_TEXTURE_FORMAT_C4;
    texture_record->wrap_s = ACGC_GX_CANONICAL_TEXTURE_WRAP_CLAMP;
    texture_record->wrap_t = ACGC_GX_CANONICAL_TEXTURE_WRAP_CLAMP;
    texture_record->min_filter = ACGC_RENDERER_FIXTURE_FILTER_LINEAR;
    texture_record->mag_filter = ACGC_RENDERER_FIXTURE_FILTER_LINEAR;
    texture_record->mip_level_count = 1;
    texture_record->image_byte_size = image_byte_size;
    texture_record->image_byte_order =
        ACGC_GX_CANONICAL_TEXTURE_BYTE_ORDER_GX_BE;
    texture_record->image_source_kind =
        ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_RAW_GUEST;
    texture_record->tlut_resource_id =
        ACGC_GX_CANONICAL_TEXTURE_TLUT_RESOURCE_ID_BASE;
    texture_record->tlut_owner_epoch = 1;
    texture_record->tlut_generation_lo = 2;
    texture_record->tlut_name = 0;
    texture_record->tlut_format = ACGC_GX_CANONICAL_TEXTURE_TLUT_FORMAT_IA8;
    texture_record->tlut_entry_count = 16;
    texture_record->tlut_byte_size = 32;
    texture_record->tlut_byte_order =
        ACGC_GX_CANONICAL_TEXTURE_BYTE_ORDER_GX_BE;
    texture_record->tlut_source_kind =
        ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_RAW_GUEST;

    plan->dynamic.header.present_image_mask = 1;
    plan->dynamic.header.present_tlut_mask = 1;
    plan->dynamic.header.required_image_mask = 1;
    plan->dynamic.header.required_tlut_mask = 1;
    plan->dynamic.header.present_resource_count = 2;
    image_record = &plan->dynamic.records[0];
    image_record->resource_id = ACGC_GX_CANONICAL_DYNAMIC_IMAGE_RESOURCE_ID_BASE;
    image_record->kind = ACGC_GX_CANONICAL_DYNAMIC_KIND_IMAGE;
    image_record->owner_epoch = 1;
    image_record->generation_lo = 1;
    image_record->owner_slot = 0;
    image_record->byte_flags =
        ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE |
        ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED;
    image_record->byte_size = image_byte_size;
    image_record->byte_order = ACGC_GX_CANONICAL_DYNAMIC_BYTE_ORDER_GX_BE;
    image_record->alignment = ACGC_GX_CANONICAL_DYNAMIC_ALIGNMENT_BYTES;
    image_record->source_kind =
        ACGC_GX_CANONICAL_DYNAMIC_SOURCE_KIND_RAW_GUEST;
    image_record->format = ACGC_GX_CANONICAL_DYNAMIC_FORMAT_C4;
    tlut_record = &plan->dynamic.records[
        ACGC_GX_CANONICAL_DYNAMIC_IMAGE_MAP_COUNT];
    tlut_record->resource_id = ACGC_GX_CANONICAL_DYNAMIC_TLUT_RESOURCE_ID_BASE;
    tlut_record->kind = ACGC_GX_CANONICAL_DYNAMIC_KIND_TLUT;
    tlut_record->owner_epoch = 1;
    tlut_record->generation_lo = 2;
    tlut_record->owner_slot = 0;
    tlut_record->byte_flags =
        ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE |
        ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED;
    tlut_record->byte_size = 32;
    tlut_record->byte_order = ACGC_GX_CANONICAL_DYNAMIC_BYTE_ORDER_GX_BE;
    tlut_record->alignment = ACGC_GX_CANONICAL_DYNAMIC_ALIGNMENT_BYTES;
    tlut_record->source_kind =
        ACGC_GX_CANONICAL_DYNAMIC_SOURCE_KIND_RAW_GUEST;
    tlut_record->format = ACGC_GX_CANONICAL_DYNAMIC_TLUT_FORMAT_IA8;
    tlut_record->element_count = 16;

    memset(resource_stage, 0, sizeof(*resource_stage));
    resource_stage->attempt_id = 7;
    resource_stage->valid = 1;
    resource_stage->image_mask = 1;
    resource_stage->tlut_mask = 1;
    resource_stage->decoded_image_mask = 1;
    resource_stage->image_byte_sizes[0] = image_byte_size;
    resource_stage->tlut_byte_sizes[0] = 32;
    resource_stage->decoded_rgba_byte_sizes[0] = 8 * 8 * 4;
    resource_stage->descriptions[0].version = ACGC_RENDERER_FIXTURE_VERSION;
    resource_stage->descriptions[0].width = 8;
    resource_stage->descriptions[0].height = 8;
    resource_stage->descriptions[0].format = ACGC_RENDERER_FIXTURE_TF_C4;
    resource_stage->descriptions[0].data_byte_order =
        ACGC_RENDERER_FIXTURE_BIG_ENDIAN;
    resource_stage->descriptions[0].data_size = image_byte_size;
    resource_stage->descriptions[0].tlut_format =
        ACGC_RENDERER_FIXTURE_TL_IA8;
    resource_stage->descriptions[0].tlut_entries = 16;
    resource_stage->descriptions[0].tlut_data_size = 32;
    resource_stage->descriptions[0].tlut_byte_order =
        ACGC_RENDERER_FIXTURE_BIG_ENDIAN;
    resource_stage->samplers[0].version = ACGC_RENDERER_FIXTURE_VERSION;
    resource_stage->samplers[0].wrap_s = ACGC_RENDERER_FIXTURE_WRAP_CLAMP;
    resource_stage->samplers[0].wrap_t = ACGC_RENDERER_FIXTURE_WRAP_CLAMP;
    resource_stage->samplers[0].min_filter =
        ACGC_RENDERER_FIXTURE_FILTER_LINEAR;
    resource_stage->samplers[0].mag_filter =
        ACGC_RENDERER_FIXTURE_FILTER_LINEAR;
    resource_stage->samplers[0].filtering_enabled = 1;
    return acgc_gx_canonical_texture_state_validate(&plan->texture) &&
        acgc_gx_canonical_dynamic_state_validate(&plan->dynamic) &&
        acgc_gx_canonical_texture_dynamic_validate(
            &plan->texture, &plan->dynamic) &&
        acgc_renderer_fixture_decode_texture(
            &resource_stage->descriptions[0],
            resource_stage->image_bytes[0],
            resource_stage->tlut_bytes[0],
            resource_stage->decoded_rgba[0],
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DECODED_RGBA_BYTES
        );
}

static int test_active_texture_resource_admission(
    AcgcMetalPacketConsumerOutput* output
) {
    AcgcAppleCanonicalPlan plan;
    AcgcAppleCanonicalPlan texgen_plan;
    AcgcAppleCanonicalPlan valid_plan;
    AcgcMetalPacketConsumerCanonicalResourceStage resource_stage;
    AcgcMetalPacketConsumerCanonicalResourceStage rejected_stage;
    AcgcMetalPacketConsumerOutput before;
    AcgcAppleCanonicalPlan quad_plan;
    AcgcAppleCanonicalPlan quad_geometry_plan;
    AcgcAppleCanonicalPlan quad_before_plan;
    AcgcMetalPacketConsumerCanonicalResourceStage quad_before_stage;
    AcgcMetalPacketConsumerOutput quad_output;
    uint32_t vertex;

    if (output == NULL || !make_active_texture_plan_with_stage(
            &plan, &resource_stage) || !make_active_texgen_plan(&texgen_plan)) {
        return 0;
    }
    plan.geometry = texgen_plan.geometry;
    plan.texgens = texgen_plan.texgens;
    /* GX_CC_TEXC/GX_CA_TEXA are the source-faithful non-pass-through inputs
     * recorded by the decomp oracle; the canonical values are 8 and 4. */
    plan.tev.stages[0].color_a =
        ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX;
    plan.tev.stages[0].color_b =
        ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX;
    plan.tev.stages[0].color_c =
        ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX;
    plan.tev.stages[0].color_d = UINT32_C(8);
    plan.tev.stages[0].alpha_a =
        ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX;
    plan.tev.stages[0].alpha_b =
        ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX;
    plan.tev.stages[0].alpha_c =
        ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX;
    plan.tev.stages[0].alpha_d = UINT32_C(4);
    plan.tev.stages[0].tex_coord = 0;
    plan.tev.stages[0].tex_map = 0;
    plan.tev.stages[0].color_chan = 0;
    if (!acgc_gx_canonical_tev_state_validate(&plan.tev)) {
        return 0;
    }
    memset(output, 0xA5, sizeof(*output));
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
        &plan, &resource_stage, output) == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output->canonical_tev_disposition ==
          ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_TEXTURE_REPLACE);
    CHECK(output->canonical_texture_binding.selected_map == 0);
    CHECK(output->canonical_texture_binding.selected_texcoord == 0);
    CHECK(output->canonical_texture_binding.vertex_count == 3);
    for (vertex = 0; vertex < 3; vertex++) {
        CHECK(output->canonical_texture_binding.texcoord_words[vertex][0] ==
              plan.geometry.vertices[vertex].texcoord[0][0]);
        CHECK(output->canonical_texture_binding.texcoord_words[vertex][1] ==
              plan.geometry.vertices[vertex].texcoord[0][1]);
    }
    for (vertex = 3; vertex < ACGC_RENDERER_GEOMETRY_MAX_VERTICES; vertex++) {
        CHECK(output->canonical_texture_binding.texcoord_words[vertex][0] == 0);
        CHECK(output->canonical_texture_binding.texcoord_words[vertex][1] == 0);
    }
    CHECK(memcmp(
        &output->canonical_tev,
        &plan.tev,
        sizeof(plan.tev)) == 0);
    CHECK(output->canonical_resource_stage.attempt_id == UINT64_C(7));
    CHECK(output->canonical_resource_stage.image_mask == 1);
    CHECK(output->canonical_resource_stage.tlut_mask == 1);
    CHECK(output->canonical_resource_stage.decoded_image_mask == 1);
    CHECK(memcmp(
        &output->canonical_resource_stage,
        &resource_stage,
        sizeof(resource_stage)) == 0);

    valid_plan = plan;
    before = *output;
    resource_stage.image_bytes[0][0] ^= UINT8_C(0xFF);
    CHECK(memcmp(&before, output, sizeof(before)) == 0);
    resource_stage = output->canonical_resource_stage;

    rejected_stage = resource_stage;
    rejected_stage.valid = 0;
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
        &plan, &rejected_stage, output) ==
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DEPENDENCY_UNSUPPORTED);
    CHECK(memcmp(&before, output, sizeof(before)) == 0);
    rejected_stage = resource_stage;
    rejected_stage.attempt_id = 0;
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
        &plan, &rejected_stage, output) ==
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DEPENDENCY_UNSUPPORTED);
    CHECK(memcmp(&before, output, sizeof(before)) == 0);
    rejected_stage = resource_stage;
    rejected_stage.image_mask = 0;
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
        &plan, &rejected_stage, output) ==
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DEPENDENCY_UNSUPPORTED);
    CHECK(memcmp(&before, output, sizeof(before)) == 0);
    rejected_stage = resource_stage;
    rejected_stage.tlut_mask = 0;
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
        &plan, &rejected_stage, output) ==
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DEPENDENCY_UNSUPPORTED);
    CHECK(memcmp(&before, output, sizeof(before)) == 0);
    rejected_stage = resource_stage;
    rejected_stage.decoded_image_mask = 0;
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
        &plan, &rejected_stage, output) ==
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DEPENDENCY_UNSUPPORTED);
    CHECK(memcmp(&before, output, sizeof(before)) == 0);
    rejected_stage = resource_stage;
    rejected_stage.descriptions[0].width++;
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
        &plan, &rejected_stage, output) ==
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DEPENDENCY_UNSUPPORTED);
    CHECK(memcmp(&before, output, sizeof(before)) == 0);
    rejected_stage = resource_stage;
    rejected_stage.samplers[0].wrap_s = ACGC_RENDERER_FIXTURE_WRAP_MIRROR;
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
        &plan, &rejected_stage, output) ==
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DEPENDENCY_UNSUPPORTED);
    CHECK(memcmp(&before, output, sizeof(before)) == 0);
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
        &plan, NULL, output) ==
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DEPENDENCY_UNSUPPORTED);
    CHECK(memcmp(&before, output, sizeof(before)) == 0);

    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
        &plan,
        (const AcgcMetalPacketConsumerCanonicalResourceStage*)output,
        output) == ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT);
    CHECK(memcmp(&before, output, sizeof(before)) == 0);

    rejected_stage = resource_stage;
    plan = valid_plan;
    plan.tev.stages[0].color_a =
        ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX + UINT32_C(1);
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
        &plan, &rejected_stage, output) ==
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_UNSUPPORTED);
    CHECK(memcmp(&before, output, sizeof(before)) == 0);

    plan = valid_plan;
    plan.tev.stages[0].tex_map = 1;
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
        &plan, &rejected_stage, output) ==
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_UNSUPPORTED);
    CHECK(memcmp(&before, output, sizeof(before)) == 0);

    plan = valid_plan;
    plan.tev.stages[0].color_chan = 1;
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
        &plan, &rejected_stage, output) ==
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_UNSUPPORTED);
    CHECK(memcmp(&before, output, sizeof(before)) == 0);

    /* Legal but non-REPLACE TEV shapes remain typed and staged. */
    plan = valid_plan;
    plan.tev.stages[0].color_d = UINT32_C(10);
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
        &plan, &rejected_stage, output) ==
        ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output->canonical_tev_disposition ==
          ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_STAGED_UNRENDERED);
    CHECK(output->canonical_texture_binding.selected_map == 0);
    CHECK(output->canonical_texture_binding.selected_texcoord == 0);
    CHECK(output->canonical_texture_binding.vertex_count == 0);

    plan = valid_plan;
    plan.tev.stages[0].color_op = ACGC_GX_CANONICAL_TEV_OPERATION_SUB;
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
        &plan, &rejected_stage, output) ==
        ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output->canonical_tev_disposition ==
          ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_STAGED_UNRENDERED);
    plan = valid_plan;
    plan.tev.stages[0].k_color_sel = 1;
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
        &plan, &rejected_stage, output) ==
        ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output->canonical_tev_disposition ==
          ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_STAGED_UNRENDERED);
    plan = valid_plan;
    plan.tev.swap_tables[0].g = 0;
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
        &plan, &rejected_stage, output) ==
        ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output->canonical_tev_disposition ==
          ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_STAGED_UNRENDERED);

    /* The same selected TEXCOORD0 words follow the source order through
     * quad-to-triangle expansion. */
    CHECK(make_source_geometry_plan(&quad_geometry_plan));
    quad_geometry_plan.geometry.primitive =
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS;
    quad_geometry_plan.geometry.vertex_count = 4;
    for (vertex = 0; vertex < 4; vertex++) {
        quad_geometry_plan.geometry.vertices[vertex].present_mask =
            quad_geometry_plan.geometry.present_mask;
        quad_geometry_plan.geometry.vertices[vertex].component_mask =
            quad_geometry_plan.geometry.component_mask;
        quad_geometry_plan.geometry.vertices[vertex].position[0] =
            bits_from_float((float)vertex);
        quad_geometry_plan.geometry.vertices[vertex].position[1] =
            bits_from_float((float)(vertex + 1));
        quad_geometry_plan.geometry.vertices[vertex].position[2] =
            bits_from_float(0.0f);
        quad_geometry_plan.geometry.vertices[vertex].color_rgba8[0] =
            UINT32_C(0x10000000) | vertex;
        quad_geometry_plan.geometry.vertices[vertex].normal[0] =
            bits_from_float(1.0f);
        quad_geometry_plan.geometry.vertices[vertex].normal[1] =
            bits_from_float(0.0f);
        quad_geometry_plan.geometry.vertices[vertex].normal[2] =
            bits_from_float(0.0f);
        quad_geometry_plan.geometry.vertices[vertex].texcoord[0][0] =
            bits_from_float(0.25f + (float)vertex);
        quad_geometry_plan.geometry.vertices[vertex].texcoord[0][1] =
            bits_from_float(0.5f + (float)vertex);
        quad_geometry_plan.geometry.vertices[vertex].texture_matrix_id[0] = 30;
        quad_geometry_plan.geometry.vertices[vertex].texture_matrix_id[1] = 60;
    }
    quad_plan = valid_plan;
    quad_plan.geometry = quad_geometry_plan.geometry;
    quad_plan.texgens = texgen_plan.texgens;
    quad_before_plan = quad_plan;
    quad_before_stage = resource_stage;
    memset(&quad_output, 0xA5, sizeof(quad_output));
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
        &quad_plan, &resource_stage, &quad_output) ==
        ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(quad_output.canonical_tev_disposition ==
          ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_TEXTURE_REPLACE);
    CHECK(quad_output.canonical_texture_binding.vertex_count == 6);
    {
        static const uint32_t quad_order[6] = {0, 1, 2, 0, 2, 3};
        for (vertex = 0; vertex < 6; vertex++) {
            const uint32_t source_vertex = quad_order[vertex];
            CHECK(quad_output.canonical_texture_binding.texcoord_words[vertex][0] ==
                  quad_plan.geometry.vertices[source_vertex].texcoord[0][0]);
            CHECK(quad_output.canonical_texture_binding.texcoord_words[vertex][1] ==
                  quad_plan.geometry.vertices[source_vertex].texcoord[0][1]);
        }
    }
    CHECK(memcmp(&quad_before_plan, &quad_plan, sizeof(quad_plan)) == 0);
    CHECK(memcmp(&quad_before_stage, &resource_stage,
                 sizeof(resource_stage)) == 0);
    {
        const uint32_t published_texcoord =
            quad_output.canonical_texture_binding.texcoord_words[0][0];
        quad_plan.geometry.vertices[0].texcoord[0][0] ^= UINT32_C(1);
        resource_stage.decoded_rgba[0][0] ^= UINT8_C(0xFF);
        CHECK(quad_output.canonical_texture_binding.texcoord_words[0][0] ==
              published_texcoord);
        CHECK(quad_output.canonical_resource_stage.decoded_rgba[0][0] !=
              resource_stage.decoded_rgba[0][0]);
    }
    return 0;
}

int main(void) {
    AcgcAppleCanonicalPlan base;
    AcgcAppleCanonicalPlan mutated;
    AcgcAppleCanonicalPlan copy;
    AcgcMetalPacketConsumerOutput output;
    AcgcMetalPacketConsumerOutput before;
    AcgcGxSemanticPacket semantic;
    AcgcGxSemanticPacket semantic_before;
    AcgcGxSemanticPacket semantic_overflow;
    AcgcGxSemanticPacket semantic_overflow_before;
    AcgcGxSemanticPacketV2 semantic_v2;
    AcgcGxSemanticPacketV2 semantic_v2_before;

    CHECK(make_base_plan(&base));
    copy = base;
    memset(&output, 0xA5, sizeof(output));
    memset(&s_default_resource_stage, 0, sizeof(s_default_resource_stage));
    s_default_resource_stage.attempt_id = 1;
    s_default_resource_stage.valid = 1;
    CHECK(prepare_default_plan(&base, &output) ==
          ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output.source_kind ==
          ACGC_METAL_PACKET_CONSUMER_SOURCE_CANONICAL_PLAN);
    CHECK(output.canonical_tev_disposition ==
          ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_VERTEX_COLOR_PASSTHROUGH);
    CHECK(output.canonical_blend_disposition ==
          ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_DISPOSITION_MAPPED);
    CHECK(memcmp(
        &output.canonical_blend,
        &base.blend,
        sizeof(base.blend)) == 0);
    CHECK(memcmp(
        &output.canonical_tev,
        &base.tev,
        sizeof(base.tev)) == 0);
    CHECK(output.semantic_version == 0);
    CHECK(output.alpha_write_enabled == 1);
    CHECK(output.canonical_alpha_disposition ==
          ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_DISPOSITION_PASSTHROUGH);
    CHECK(memcmp(&output.canonical_alpha, &base.alpha,
                 sizeof(base.alpha)) == 0);
    CHECK(output.canonical_raster_disposition ==
          ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_DISPOSITION_MAPPED);
    CHECK(memcmp(&output.canonical_raster, &base.raster,
                 sizeof(base.raster)) == 0);
    CHECK(output.canonical_fog_disposition ==
          ACGC_METAL_PACKET_CONSUMER_CANONICAL_FOG_DISPOSITION_INACTIVE);
    CHECK(memcmp(&output.canonical_fog, &base.fog,
                 sizeof(base.fog)) == 0);
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
    CHECK(memcmp(&copy, &base, sizeof(copy)) == 0);
    CHECK(test_canonical_alpha_disposition(&base, &output));
    CHECK(test_canonical_raster_disposition(&base, &output));
    CHECK(test_canonical_fog_disposition(&base, &output));
    CHECK(test_multi_vertex_geometry(&output) == 0);
    CHECK(test_source_geometry_attributes(&output));
    CHECK(test_active_texgen_admission(&output));
    CHECK(test_af_none_lighting(&output));
    CHECK(test_active_texture_resource_admission(&output) == 0);
    before = output;
    CHECK(prepare_default_plan(NULL, &output) ==
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
    CHECK(prepare_default_plan(&copy, &output) ==
          ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output.geometry.vertices[0].position_x == bits_from_float(99.0f));
    CHECK(output.geometry.vertices[0].color_rgba8 == UINT32_C(0x04030201));

    mutated = base;
    mutated.geometry.vertices[3].position[0] = bits_from_float(1.0f);
    CHECK(expect_rejection(&mutated, &output));

    mutated = base;
    mutated.geometry.vertex_count = 0;
    CHECK(expect_rejection_status(
        &mutated,
        &output,
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_GEOMETRY_UNSUPPORTED
    ));
    mutated = base;
    mutated.transform.known_mask &=
        ~ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_KNOWN_MASK;
    CHECK(expect_rejection_status(
        &mutated,
        &output,
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TRANSFORM_UNSUPPORTED
    ));
    mutated = base;
    mutated.channels.records[0].color.enable = 1;
    CHECK(expect_rejection_status(
        &mutated,
        &output,
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_CHANNELS_UNSUPPORTED
    ));
    mutated = base;
    mutated.texgens.header.active_texgen_count = 1;
    CHECK(expect_rejection_status(
        &mutated,
        &output,
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEXGENS_UNSUPPORTED
    ));
    mutated = base;
    mutated.tev.stages[0].tex_map = 0;
    CHECK(expect_rejection_status(
        &mutated,
        &output,
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_UNSUPPORTED
    ));
    mutated = base;
    /* Reserved Raster words are structurally invalid and retain status 23. */
    mutated.raster.reserved[0] = 1;
    CHECK(expect_rejection_status(
        &mutated,
        &output,
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_UNSUPPORTED
    ));
    CHECK(strcmp(acgc_metal_packet_consumer_status_string(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEXGENS_UNSUPPORTED
    ), "unsupported canonical Texgen section") == 0);

    CHECK(run_rejection_matrix(&base, &output));

    /* Structurally valid logic mode is carried by value but stays staged. */
    mutated = base;
    mutated.blend.mode = ACGC_GX_SEMANTIC_V3_BLEND_MODE_LOGIC;
    mutated.blend.logic_op = ACGC_GX_SEMANTIC_V3_LOGIC_XOR;
    copy = mutated;
    CHECK(prepare_default_plan(&mutated, &output) ==
          ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output.canonical_blend_disposition ==
          ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_DISPOSITION_STAGED_UNRENDERED);
    CHECK(memcmp(
        &output.canonical_blend,
        &copy.blend,
        sizeof(copy.blend)) == 0);
    CHECK(output.state.blend.enabled == 0);
    CHECK(acgc_metal_state_fixture_validate(&output.state));
    mutated.blend.mode = ACGC_GX_SEMANTIC_V3_BLEND_MODE_NONE;
    mutated.blend.logic_op = ACGC_GX_SEMANTIC_V3_LOGIC_CLEAR;
    CHECK(memcmp(
        &output.canonical_blend,
        &copy.blend,
        sizeof(copy.blend)) == 0);

    /* Subtract and the GX position-dependent color aliases are mapped exactly. */
    mutated = base;
    mutated.blend.mode = ACGC_GX_SEMANTIC_V3_BLEND_MODE_SUBTRACT;
    mutated.blend.source_factor =
        ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_SOURCE_COLOR;
    mutated.blend.destination_factor =
        ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_SOURCE_COLOR;
    copy = mutated;
    CHECK(prepare_default_plan(&mutated, &output) ==
          ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output.canonical_blend_disposition ==
          ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_DISPOSITION_MAPPED);
    CHECK(memcmp(
        &output.canonical_blend,
        &copy.blend,
        sizeof(copy.blend)) == 0);
    CHECK(output.state.blend.enabled == 1);
    CHECK(output.state.blend.source_rgb_factor ==
          ACGC_METAL_BLEND_DESTINATION_COLOR);
    CHECK(output.state.blend.destination_rgb_factor ==
          ACGC_METAL_BLEND_ONE_MINUS_SOURCE_COLOR);
    CHECK(output.state.blend.source_alpha_factor ==
          ACGC_METAL_BLEND_DESTINATION_COLOR);
    CHECK(output.state.blend.destination_alpha_factor ==
          ACGC_METAL_BLEND_ONE_MINUS_SOURCE_COLOR);
    CHECK(output.state.blend.rgb_operation ==
          ACGC_METAL_BLEND_REVERSE_SUBTRACT);
    CHECK(output.state.blend.alpha_operation ==
          ACGC_METAL_BLEND_REVERSE_SUBTRACT);
    CHECK(acgc_metal_state_fixture_validate(&output.state));
    mutated.blend.mode = ACGC_GX_SEMANTIC_V3_BLEND_MODE_NONE;
    mutated.blend.source_factor = ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_ZERO;
    mutated.blend.destination_factor = ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_ONE;
    CHECK(memcmp(
        &output.canonical_blend,
        &copy.blend,
        sizeof(copy.blend)) == 0);

    /* Out-of-range canonical words remain status 20 and do not publish. */
    mutated = base;
    mutated.blend.mode = ACGC_GX_CANONICAL_BLEND_MODE_MAX + 1;
    CHECK(expect_rejection_status(
        &mutated,
        &output,
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_UNSUPPORTED));
    mutated = base;
    mutated.blend.source_factor = ACGC_GX_CANONICAL_BLEND_FACTOR_MAX + 1;
    CHECK(expect_rejection_status(
        &mutated,
        &output,
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_UNSUPPORTED));

    /* A present PNMTXIDX is accepted only as a direct normalized selector. */
    mutated = base;
    mutated.geometry.present_mask |=
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_PNMTXIDX;
    mutated.geometry.vertices[0].present_mask = mutated.geometry.present_mask;
    mutated.geometry.vertices[1].present_mask = mutated.geometry.present_mask;
    mutated.geometry.vertices[2].present_mask = mutated.geometry.present_mask;
    CHECK(prepare_default_plan(
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
    CHECK(prepare_default_plan(
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

    /* Exercise the audited nonidentity orthographic projection and row-major
     * 3x4 M, including every published column-major word. */
    mutated = base;
    mutated.transform.projection_type =
        ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_ORTHOGRAPHIC;
    mutated.transform.projection[0] = bits_from_float(0.25f);
    mutated.transform.projection[1] = bits_from_float(-0.5f);
    mutated.transform.projection[2] = bits_from_float(0.5f);
    mutated.transform.projection[3] = bits_from_float(0.5f);
    mutated.transform.projection[4] = bits_from_float(-0.125f);
    mutated.transform.projection[5] = bits_from_float(-0.875f);
    mutated.transform.position[0][0] = bits_from_float(2.0f);
    mutated.transform.position[0][1] = bits_from_float(0.0f);
    mutated.transform.position[0][2] = bits_from_float(0.0f);
    mutated.transform.position[0][3] = bits_from_float(1.0f);
    mutated.transform.position[0][4] = bits_from_float(0.0f);
    mutated.transform.position[0][5] = bits_from_float(3.0f);
    mutated.transform.position[0][6] = bits_from_float(0.0f);
    mutated.transform.position[0][7] = bits_from_float(-2.0f);
    mutated.transform.position[0][8] = bits_from_float(0.0f);
    mutated.transform.position[0][9] = bits_from_float(0.0f);
    mutated.transform.position[0][10] = bits_from_float(4.0f);
    mutated.transform.position[0][11] = bits_from_float(0.5f);
    copy = mutated;
    CHECK(prepare_default_plan(
              &mutated, &output) == ACGC_METAL_PACKET_CONSUMER_OK);
    {
        static const uint32_t expected_matrix[16] = {
            UINT32_C(0x3F000000), UINT32_C(0x00000000),
            UINT32_C(0x00000000), UINT32_C(0x00000000),
            UINT32_C(0x00000000), UINT32_C(0x3FC00000),
            UINT32_C(0x00000000), UINT32_C(0x00000000),
            UINT32_C(0x00000000), UINT32_C(0x00000000),
            UINT32_C(0xBF000000), UINT32_C(0x00000000),
            UINT32_C(0xBE800000), UINT32_C(0xBF000000),
            UINT32_C(0xBF700000), UINT32_C(0x3F800000)
        };
        uint32_t word;

        for (word = 0; word < 16; word++) {
            CHECK(isfinite(float_from_bits(
                output.state.transform.matrix[word])));
            CHECK(output.state.transform.matrix[word] == expected_matrix[word]);
        }
    }
    CHECK(memcmp(&copy, &mutated, sizeof(copy)) == 0);

    /* Finite max inputs overflow during transform multiplication without
     * changing the previously published output or the input plan. */
    mutated = base;
    mutated.transform.projection[0] = UINT32_C(0x7F7FFFFF);
    mutated.transform.position[0][0] = UINT32_C(0x7F7FFFFF);
    copy = mutated;
    before = output;
    CHECK(prepare_default_plan(
              &mutated, &output) ==
          ACGC_METAL_PACKET_CONSUMER_CANONICAL_TRANSFORM_UNSUPPORTED);
    CHECK(memcmp(&before, &output, sizeof(output)) == 0);
    CHECK(memcmp(&copy, &mutated, sizeof(copy)) == 0);

    /* Input/output aliasing is rejected before reading either value. */
    memset(&output, 0x5A, sizeof(output));
    before = output;
    CHECK(prepare_default_plan(
              (const AcgcAppleCanonicalPlan*)&output, &output) ==
          ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT);
    CHECK(memcmp(&before, &output, sizeof(output)) == 0);

    CHECK(make_semantic_packet(&semantic));
    semantic_before = semantic;
    memset(&output, 0xA5, sizeof(output));
    CHECK(acgc_metal_packet_consumer_prepare(&semantic, NULL, &output) ==
          ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output.source_kind == ACGC_METAL_PACKET_CONSUMER_SOURCE_SEMANTIC);
    CHECK(output.canonical_tev_disposition ==
          ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_NONE);
    CHECK(output.semantic_version == ACGC_GX_SEMANTIC_PACKET_VERSION);
    CHECK(output.geometry.vertex_count ==
          ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES);
    CHECK(output.geometry.draw_count == ACGC_RENDERER_GEOMETRY_MAX_DRAWS);
    CHECK(output.geometry.draws[0].primitive ==
          ACGC_RENDERER_PRIMITIVE_TRIANGLES);
    CHECK(output.geometry.draws[0].first_vertex == 0);
    CHECK(output.geometry.draws[0].vertex_count ==
          ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES);
    CHECK(acgc_renderer_geometry_validate(&output.geometry));
    CHECK(memcmp(&semantic_before, &semantic, sizeof(semantic)) == 0);

    /* V1 transform overflow preserves the caller's prior output. */
    semantic_overflow = semantic;
    semantic_overflow.transform.projection[0] = UINT32_C(0x7F7FFFFF);
    semantic_overflow.transform.modelview[0] = UINT32_C(0x7F7FFFFF);
    semantic_overflow_before = semantic_overflow;
    before = output;
    CHECK(acgc_metal_packet_consumer_prepare(
              &semantic_overflow, NULL, &output) ==
          ACGC_METAL_PACKET_CONSUMER_TRANSFORM_OVERFLOW);
    CHECK(memcmp(&before, &output, sizeof(output)) == 0);
    CHECK(memcmp(
              &semantic_overflow_before,
              &semantic_overflow,
              sizeof(semantic_overflow)
          ) == 0);

    CHECK(make_semantic_packet_v2(&semantic_v2));
    semantic_v2_before = semantic_v2;
    memset(&output, 0xA5, sizeof(output));
    CHECK(acgc_metal_packet_consumer_prepare_v2(
              &semantic_v2, NULL, &output) ==
          ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output.source_kind == ACGC_METAL_PACKET_CONSUMER_SOURCE_SEMANTIC);
    CHECK(output.semantic_version == ACGC_GX_SEMANTIC_PACKET_V2_VERSION);
    CHECK(output.canonical_tev_disposition ==
          ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_NONE);
    CHECK(output.v2_extension_rendering_status ==
          ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_RENDERED);
    CHECK(output.geometry.vertex_count ==
          ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES);
    CHECK(output.geometry.draw_count == ACGC_RENDERER_GEOMETRY_MAX_DRAWS);
    CHECK(output.geometry.draws[0].primitive ==
          ACGC_RENDERER_PRIMITIVE_TRIANGLES);
    CHECK(output.geometry.draws[0].first_vertex == 0);
    CHECK(output.geometry.draws[0].vertex_count ==
          ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES);
    CHECK(acgc_renderer_geometry_validate(&output.geometry));
    CHECK(memcmp(&semantic_v2_before, &semantic_v2, sizeof(semantic_v2)) == 0);

    /* PASS is deliberately emitted only after every mutation gate succeeds. */
    puts("Apple canonical plan consumer fixture: PASS");
    puts("proof boundary: bounded CPU canonical-plan conversion accepts source-faithful disabled vertex-color channels, the exact active COLOR0 REG/REG nonzero-mask DF_CLAMP/AF_NONE mode with validated normal/light inputs pre-materialized to vertex RGB, and the exact two-active Texgen canonical state as admission-only provenance while still emitting only position+color; this does not claim general lighting, Texgen coordinate transformation, live gather/callback, Metal encode/present, pixels, device, assets, or playability");
    return 0;
}
