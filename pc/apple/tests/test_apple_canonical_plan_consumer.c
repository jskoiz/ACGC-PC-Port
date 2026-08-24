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
    AcgcAppleCanonicalPlan before_plan;
    AcgcMetalPacketConsumerOutput before;
    AcgcMetalPacketConsumerStatus status;

    if (plan == NULL || output == NULL) {
        return 0;
    }
    before_plan = *plan;
    before = *output;
    status = acgc_metal_packet_consumer_prepare_canonical_plan(plan, output);
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
    status = acgc_metal_packet_consumer_prepare_canonical_plan(plan, output);
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
            acgc_metal_packet_consumer_prepare_canonical_plan(&plan, output);

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
    mutated.blend.mode = ACGC_GX_SEMANTIC_V3_BLEND_MODE_LOGIC;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_UNSUPPORTED);
    mutated = *base;
    mutated.blend.source_factor =
        ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_SOURCE_COLOR;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_UNSUPPORTED);
    mutated = *base;
    mutated.alpha.comp0 = 0;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_UNSUPPORTED);
    mutated = *base;
    mutated.alpha.color_update_enable = 0;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_UNSUPPORTED);
    mutated = *base;
    mutated.depth.z_compare_enable = 0;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_DEPTH_UNSUPPORTED);
    mutated = *base;
    mutated.raster.scissor[2] = 63;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_UNSUPPORTED);
    mutated = *base;
    mutated.raster.viewport_bits[4] = bits_from_float(0.000001f);
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_UNSUPPORTED);
    mutated = *base;
    mutated.raster.viewport_bits[5] = bits_from_float(0.999999f);
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_UNSUPPORTED);
    mutated = *base;
    mutated.raster.clip_mode = ACGC_GX_CANONICAL_RASTER_CLIP_MODE_DISABLE;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_UNSUPPORTED);
    mutated = *base;
    mutated.raster.cull_mode = ACGC_GX_CANONICAL_RASTER_CULL_MODE_ALL;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_UNSUPPORTED);
    mutated = *base;
    mutated.raster.dither = 1;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_UNSUPPORTED);
    mutated = *base;
    mutated.raster.line_width = 1;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_UNSUPPORTED);
    mutated = *base;
    mutated.raster.point_size = 1;
    EXPECT_CANONICAL_REJECTION(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_UNSUPPORTED);
    mutated = *base;
    mutated.fog.fog_type = ACGC_GX_CANONICAL_FOG_TYPE_PERSP_LIN;
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
    if (acgc_metal_packet_consumer_prepare_canonical_plan(&plan, output) !=
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
    if (acgc_metal_packet_consumer_prepare_canonical_plan(&plan, output) !=
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
        CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
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
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
              &plan, output) == ACGC_METAL_PACKET_CONSUMER_OK);
    output->geometry.vertices[1].position_x ^= UINT32_C(1);
    CHECK(!check_output_vertex(&plan, output, 1, 1));
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
    CHECK(memcmp(&copy, &base, sizeof(copy)) == 0);
    CHECK(test_multi_vertex_geometry(&output) == 0);
    CHECK(test_source_geometry_attributes(&output));
    CHECK(test_active_texgen_admission(&output));
    CHECK(test_af_none_lighting(&output));
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
    mutated.raster.scissor[2] = 63;
    CHECK(expect_rejection_status(
        &mutated,
        &output,
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_UNSUPPORTED
    ));
    CHECK(strcmp(acgc_metal_packet_consumer_status_string(
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEXGENS_UNSUPPORTED
    ), "unsupported canonical Texgen section") == 0);

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

    /* Finite max inputs overflow during transform multiplication without
     * changing the previously published output or the input plan. */
    mutated = base;
    mutated.transform.projection[0] = UINT32_C(0x7F7FFFFF);
    mutated.transform.position[0][0] = UINT32_C(0x7F7FFFFF);
    copy = mutated;
    before = output;
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
              &mutated, &output) ==
          ACGC_METAL_PACKET_CONSUMER_CANONICAL_TRANSFORM_UNSUPPORTED);
    CHECK(memcmp(&before, &output, sizeof(output)) == 0);
    CHECK(memcmp(&copy, &mutated, sizeof(copy)) == 0);

    /* Input/output aliasing is rejected before reading either value. */
    memset(&output, 0x5A, sizeof(output));
    before = output;
    CHECK(acgc_metal_packet_consumer_prepare_canonical_plan(
              (const AcgcAppleCanonicalPlan*)&output, &output) ==
          ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT);
    CHECK(memcmp(&before, &output, sizeof(output)) == 0);

    CHECK(make_semantic_packet(&semantic));
    semantic_before = semantic;
    memset(&output, 0xA5, sizeof(output));
    CHECK(acgc_metal_packet_consumer_prepare(&semantic, NULL, &output) ==
          ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output.source_kind == ACGC_METAL_PACKET_CONSUMER_SOURCE_SEMANTIC);
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
