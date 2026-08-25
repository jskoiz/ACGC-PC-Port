#import <Foundation/Foundation.h>

#include "acgc/gx_semantic_packet.h"
#include "acgc/metal_packet_consumer.h"
#include "acgc/metal_sink.h"
#include "acgc/renderer_geometry.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

#define SKIP_NO_METAL 77

static int make_packet_output(AcgcMetalPacketConsumerOutput* output) {
    AcgcGxSemanticPacket packet;
    AcgcRendererGeometryPacket geometry;
    uint32_t vertex_index;

    if (output == NULL || !acgc_renderer_geometry_make_triangle(&geometry) ||
        !acgc_gx_semantic_packet_init(&packet)) {
        return 0;
    }

    packet.vertex_count = ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES;
    packet.material.flags = ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR;
    for (vertex_index = 0;
         vertex_index < ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES;
         vertex_index++) {
        memcpy(
            packet.vertices[vertex_index].position,
            &geometry.vertices[vertex_index].position_x,
            sizeof(packet.vertices[vertex_index].position)
        );
        packet.vertices[vertex_index].color_rgba8 =
            geometry.vertices[vertex_index].color_rgba8;
    }
    return acgc_metal_packet_consumer_prepare(&packet, NULL, output) ==
        ACGC_METAL_PACKET_CONSUMER_OK;
}

static uint32_t bits_from_float(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void set_mapped_canonical_raster(
    AcgcMetalPacketConsumerOutput* output
) {
    output->canonical_raster_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_DISPOSITION_MAPPED;
    memset(&output->canonical_raster, 0, sizeof(output->canonical_raster));
    output->canonical_raster.viewport_bits[0] = output->state.viewport.origin_x;
    output->canonical_raster.viewport_bits[1] = output->state.viewport.origin_y;
    output->canonical_raster.viewport_bits[2] = output->state.viewport.width;
    output->canonical_raster.viewport_bits[3] = output->state.viewport.height;
    output->canonical_raster.viewport_bits[4] = output->state.viewport.znear;
    output->canonical_raster.viewport_bits[5] = output->state.viewport.zfar;
    output->canonical_raster.scissor[2] = 64;
    output->canonical_raster.scissor[3] = 64;
    output->canonical_raster.clip_mode =
        ACGC_GX_CANONICAL_RASTER_CLIP_MODE_ENABLE;
    output->canonical_raster.cull_mode = output->state.raster.cull_mode;
}

static void set_decomp_dynamic_canonical_raster(
    AcgcMetalPacketConsumerOutput* output
) {
    output->state.viewport.width = bits_from_float(640.0f);
    output->state.viewport.height = bits_from_float(480.0f);
    set_mapped_canonical_raster(output);
    output->canonical_raster.scissor[2] = 640;
    output->canonical_raster.scissor[3] = 480;
    output->canonical_raster.line_width = 5;
    output->canonical_raster.point_size = 6;
    output->canonical_raster.dither = 1;
    output->canonical_raster.field_mode = 0;
    output->canonical_raster.half_aspect_ratio = 0;
    output->canonical_raster.field_odd_mask = 1;
    output->canonical_raster.field_even_mask = 1;
}

static int expect_invalid_mapped_raster_field(
    AcgcMetalPacketConsumerOutput* output,
    uint32_t* field,
    uint32_t value
) {
    AcgcMetalSinkSnapshot before;
    AcgcMetalSinkSnapshot after;
    uint32_t original;
    AcgcMetalSinkStatus status;

    if (output == NULL || field == NULL) {
        return 0;
    }
    original = *field;
    *field = value;
    acgc_metal_sink_get_snapshot(&before);
    status = acgc_metal_sink_submit(output);
    acgc_metal_sink_get_snapshot(&after);
    *field = original;
    return status == ACGC_METAL_SINK_INVALID_OUTPUT &&
        after.submit_count == before.submit_count + 1 &&
        after.completed_count == before.completed_count &&
        after.readback_count == before.readback_count;
}

static int expect_invalid_source_kind(
    const AcgcMetalPacketConsumerOutput* output,
    uint32_t source_kind
) {
    AcgcMetalPacketConsumerOutput candidate;
    AcgcMetalSinkSnapshot before;
    AcgcMetalSinkSnapshot after;

    if (output == NULL) {
        return 0;
    }
    candidate = *output;
    candidate.source_kind = source_kind;
    acgc_metal_sink_get_snapshot(&before);
    if (acgc_metal_sink_submit(&candidate) !=
            ACGC_METAL_SINK_INVALID_OUTPUT) {
        return 0;
    }
    acgc_metal_sink_get_snapshot(&after);
    return after.submit_count == before.submit_count + 1 &&
        after.completed_count == before.completed_count &&
        after.readback_count == before.readback_count;
}

static void set_passthrough_canonical_alpha(
    AcgcMetalPacketConsumerOutput* output
) {
    output->canonical_alpha_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_DISPOSITION_PASSTHROUGH;
    output->canonical_alpha.comp0 = ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX;
    output->canonical_alpha.ref0 = 17;
    output->canonical_alpha.op = ACGC_GX_CANONICAL_ALPHA_OPERATOR_MIN;
    output->canonical_alpha.comp1 = ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX;
    output->canonical_alpha.ref1 = 29;
    output->canonical_alpha.color_update_enable =
        ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX;
    output->canonical_alpha.alpha_update_enable =
        output->alpha_write_enabled;
    output->canonical_alpha.z_comp_loc_before_tex = 1;
}

static void set_inactive_canonical_fog(
    AcgcMetalPacketConsumerOutput* output
) {
    output->canonical_fog_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_FOG_DISPOSITION_INACTIVE;
    memset(&output->canonical_fog, 0, sizeof(output->canonical_fog));
}

static void set_texture_replace_canonical_tev(
    AcgcMetalPacketConsumerOutput* output
) {
    AcgcGxCanonicalTevStage* stage;

    memset(&output->canonical_tev, 0, sizeof(output->canonical_tev));
    output->canonical_tev.header.version = ACGC_GX_CANONICAL_TEV_STATE_VERSION;
    output->canonical_tev.header.section_id = ACGC_GX_CANONICAL_TEV_SECTION_ID;
    output->canonical_tev.header.section_mask = ACGC_GX_CANONICAL_TEV_SECTION_MASK;
    output->canonical_tev.header.byte_size = ACGC_GX_CANONICAL_TEV_STATE_SIZE;
    output->canonical_tev.header.active_stage_count = 1;
    output->canonical_tev.header.stage_capacity =
        ACGC_GX_CANONICAL_TEV_STAGE_CAPACITY;
    output->canonical_tev.header.component_valid_mask =
        ACGC_GX_CANONICAL_TEV_COMPONENT_VALID_MASK;
    output->canonical_tev.header.stage_offset = ACGC_GX_CANONICAL_TEV_STAGE_OFFSET;
    output->canonical_tev.header.stage_record_size =
        ACGC_GX_CANONICAL_TEV_STAGE_RECORD_SIZE;
    output->canonical_tev.header.register_offset =
        ACGC_GX_CANONICAL_TEV_REGISTER_OFFSET;
    output->canonical_tev.header.register_record_size =
        ACGC_GX_CANONICAL_TEV_REGISTER_RECORD_SIZE;
    output->canonical_tev.header.konst_offset = ACGC_GX_CANONICAL_TEV_KONST_OFFSET;
    output->canonical_tev.header.konst_record_size =
        ACGC_GX_CANONICAL_TEV_KONST_RECORD_SIZE;
    output->canonical_tev.header.swap_table_offset =
        ACGC_GX_CANONICAL_TEV_SWAP_TABLE_OFFSET;
    output->canonical_tev.header.swap_table_record_size =
        ACGC_GX_CANONICAL_TEV_SWAP_TABLE_RECORD_SIZE;
    stage = &output->canonical_tev.stages[0];
    stage->color_a = ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX;
    stage->color_b = ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX;
    stage->color_c = ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX;
    stage->color_d = 8;
    stage->alpha_a = ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX;
    stage->alpha_b = ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX;
    stage->alpha_c = ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX;
    stage->alpha_d = 4;
    stage->color_op = ACGC_GX_CANONICAL_TEV_OPERATION_ADD;
    stage->color_bias = ACGC_GX_CANONICAL_TEV_BIAS_MIN;
    stage->color_scale = ACGC_GX_CANONICAL_TEV_SCALE_MIN;
    stage->color_clamp = ACGC_GX_CANONICAL_TEV_BOOLEAN_MAX;
    stage->color_out = ACGC_GX_CANONICAL_TEV_REGISTER_INDEX_MIN;
    stage->alpha_op = ACGC_GX_CANONICAL_TEV_OPERATION_ADD;
    stage->alpha_bias = ACGC_GX_CANONICAL_TEV_BIAS_MIN;
    stage->alpha_scale = ACGC_GX_CANONICAL_TEV_SCALE_MIN;
    stage->alpha_clamp = ACGC_GX_CANONICAL_TEV_BOOLEAN_MAX;
    stage->alpha_out = ACGC_GX_CANONICAL_TEV_REGISTER_INDEX_MIN;
    stage->tex_coord = 0;
    stage->tex_map = 0;
    stage->color_chan = 0;
    output->canonical_tev.swap_tables[0] =
        (AcgcGxCanonicalTevSwapTable){0, 1, 2, 3};
    output->canonical_tev.swap_tables[1] =
        (AcgcGxCanonicalTevSwapTable){0, 0, 0, 3};
    output->canonical_tev.swap_tables[2] =
        (AcgcGxCanonicalTevSwapTable){1, 1, 1, 3};
    output->canonical_tev.swap_tables[3] =
        (AcgcGxCanonicalTevSwapTable){2, 2, 2, 3};
}

static int make_texture_replace_output(
    AcgcMetalPacketConsumerOutput* output
) {
    AcgcMetalPacketConsumerCanonicalResourceStage* resource_stage;
    uint32_t x;
    uint32_t y;

    if (output == NULL || !make_packet_output(output)) {
        return 0;
    }
    output->source_kind =
        ACGC_METAL_PACKET_CONSUMER_SOURCE_CANONICAL_PLAN;
    output->canonical_tev_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_TEXTURE_REPLACE;
    set_texture_replace_canonical_tev(output);

    output->canonical_blend_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_DISPOSITION_MAPPED;
    output->canonical_blend = (AcgcGxCanonicalBlendState){
        ACGC_GX_SEMANTIC_V3_BLEND_MODE_NONE,
        ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_ZERO,
        ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_ONE,
        ACGC_GX_SEMANTIC_V3_LOGIC_CLEAR
    };
    output->state.blend.enabled = 0;
    output->state.blend.source_rgb_factor = ACGC_METAL_BLEND_ZERO;
    output->state.blend.destination_rgb_factor = ACGC_METAL_BLEND_ONE;
    output->state.blend.source_alpha_factor = ACGC_METAL_BLEND_ZERO;
    output->state.blend.destination_alpha_factor = ACGC_METAL_BLEND_ONE;
    output->state.blend.rgb_operation = ACGC_METAL_BLEND_ADD;
    output->state.blend.alpha_operation = ACGC_METAL_BLEND_ADD;
    set_passthrough_canonical_alpha(output);
    set_mapped_canonical_raster(output);
    set_inactive_canonical_fog(output);

    resource_stage = &output->canonical_resource_stage;
    memset(resource_stage, 0, sizeof(*resource_stage));
    resource_stage->attempt_id = 1;
    resource_stage->valid = 1;
    resource_stage->image_mask = 1;
    resource_stage->decoded_image_mask = 1;
    resource_stage->image_byte_sizes[0] = 4 * 4 * 4;
    resource_stage->decoded_rgba_byte_sizes[0] = 4 * 4 * 4;
    resource_stage->descriptions[0].version = ACGC_RENDERER_FIXTURE_VERSION;
    resource_stage->descriptions[0].width = 4;
    resource_stage->descriptions[0].height = 4;
    resource_stage->descriptions[0].format = ACGC_RENDERER_FIXTURE_TF_RGBA8;
    resource_stage->descriptions[0].data_byte_order =
        ACGC_RENDERER_FIXTURE_LITTLE_ENDIAN;
    resource_stage->descriptions[0].data_size = 4 * 4 * 4;
    resource_stage->samplers[0].version = ACGC_RENDERER_FIXTURE_VERSION;
    resource_stage->samplers[0].wrap_s = ACGC_RENDERER_FIXTURE_WRAP_REPEAT;
    resource_stage->samplers[0].wrap_t = ACGC_RENDERER_FIXTURE_WRAP_REPEAT;
    resource_stage->samplers[0].min_filter =
        ACGC_RENDERER_FIXTURE_FILTER_NEAREST;
    resource_stage->samplers[0].mag_filter =
        ACGC_RENDERER_FIXTURE_FILTER_NEAREST;
    resource_stage->samplers[0].filtering_enabled = 1;
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            const size_t byte_offset = ((size_t)y * 4 + x) * 4;
            resource_stage->decoded_rgba[0][byte_offset] =
                (uint8_t)(16 + x);
            resource_stage->decoded_rgba[0][byte_offset + 1] =
                (uint8_t)(32 + y * 5);
            resource_stage->decoded_rgba[0][byte_offset + 2] =
                (uint8_t)(48 + y * 4 + x);
            resource_stage->decoded_rgba[0][byte_offset + 3] = 255;
        }
    }
    output->canonical_texture_binding.selected_map = 0;
    output->canonical_texture_binding.selected_texcoord = 0;
    output->canonical_texture_binding.vertex_count = 3;
    for (x = 0; x < 3; x++) {
        output->canonical_texture_binding.texcoord_words[x][0] =
            bits_from_float(1.375f);
        output->canonical_texture_binding.texcoord_words[x][1] =
            bits_from_float(-0.375f);
    }
    return acgc_gx_canonical_tev_state_validate(&output->canonical_tev) &&
        acgc_metal_state_fixture_validate(&output->state) &&
        acgc_renderer_geometry_validate(&output->geometry);
}

static int make_live_texture_replace_output(
    AcgcMetalPacketConsumerOutput* output
) {
    AcgcMetalPacketConsumerCanonicalResourceStage* resource_stage;
    uint32_t map;

    if (output == NULL || !make_texture_replace_output(output)) {
        return 0;
    }
    /* Match the emu64 initialization raster while keeping the texture seam
     * focused on the selected map and its value-owned staged resources. */
    set_decomp_dynamic_canonical_raster(output);

    resource_stage = &output->canonical_resource_stage;
    memset(resource_stage, 0, sizeof(*resource_stage));
    resource_stage->attempt_id = 1;
    resource_stage->valid = 1;
    resource_stage->image_mask = UINT32_C(0xFF);
    resource_stage->decoded_image_mask = UINT32_C(0xFF);
    resource_stage->tlut_mask = UINT32_C(1) << 15;
    resource_stage->tlut_byte_sizes[15] = 32;

    for (map = 0; map < PC_GX_TEXTURE_RAW_MAP_COUNT; map++) {
        const uint32_t width = map == 0 ? 128 : 8;
        const uint32_t height = map == 0 ? 32 : 4;
        const uint32_t format = map == 0
            ? ACGC_RENDERER_FIXTURE_TF_C4
            : ACGC_RENDERER_FIXTURE_TF_I8;
        const uint32_t image_size = map == 0 ? 2048 : 32;
        const uint32_t decoded_size = width * height * 4;
        AcgcRendererFixtureTextureDescription* description =
            &resource_stage->descriptions[map];
        AcgcRendererFixtureSamplerDescription* sampler =
            &resource_stage->samplers[map];

        resource_stage->image_byte_sizes[map] = image_size;
        resource_stage->decoded_rgba_byte_sizes[map] = decoded_size;
        description->version = ACGC_RENDERER_FIXTURE_VERSION;
        description->width = width;
        description->height = height;
        description->format = format;
        description->data_byte_order = ACGC_RENDERER_FIXTURE_BIG_ENDIAN;
        description->data_size = image_size;
        if (map == 0) {
            description->tlut_format = ACGC_RENDERER_FIXTURE_TL_RGB5A3;
            description->tlut_entries = 16;
            description->tlut_data_size = 32;
            description->tlut_byte_order = ACGC_RENDERER_FIXTURE_BIG_ENDIAN;
        }

        sampler->version = ACGC_RENDERER_FIXTURE_VERSION;
        sampler->wrap_s = ACGC_RENDERER_FIXTURE_WRAP_MIRROR;
        sampler->wrap_t = ACGC_RENDERER_FIXTURE_WRAP_MIRROR;
        sampler->min_filter = ACGC_RENDERER_FIXTURE_FILTER_LINEAR;
        sampler->mag_filter = ACGC_RENDERER_FIXTURE_FILTER_LINEAR;
        sampler->filtering_enabled = 1;
        if (!acgc_renderer_fixture_decode_texture(
                description,
                resource_stage->image_bytes[map],
                map == 0 ? resource_stage->tlut_bytes[15] : NULL,
                resource_stage->decoded_rgba[map],
                ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DECODED_RGBA_BYTES
            )) {
            return 0;
        }
    }

    output->canonical_texture_binding.selected_map = 0;
    output->canonical_texture_binding.selected_texcoord = 0;
    output->canonical_texture_binding.vertex_count = 3;
    return acgc_gx_canonical_tev_state_validate(&output->canonical_tev) &&
        acgc_metal_state_fixture_validate(&output->state) &&
        acgc_renderer_geometry_validate(&output->geometry) &&
        resource_stage->descriptions[0].width * 4 !=
            resource_stage->descriptions[7].width * 4 &&
        resource_stage->decoded_rgba_byte_sizes[0] == 16384 &&
        resource_stage->decoded_rgba_byte_sizes[7] == 128;
}

static int expect_invalid_texture_output(
    const AcgcMetalPacketConsumerOutput* output
) {
    AcgcMetalSinkSnapshot before;
    AcgcMetalSinkSnapshot after;

    if (output == NULL) {
        return 0;
    }
    acgc_metal_sink_get_snapshot(&before);
    if (acgc_metal_sink_submit(output) != ACGC_METAL_SINK_INVALID_OUTPUT) {
        return 0;
    }
    acgc_metal_sink_get_snapshot(&after);
    return after.submit_count == before.submit_count + 1 &&
        after.completed_count == before.completed_count &&
        after.readback_count == before.readback_count &&
        after.last_status == ACGC_METAL_SINK_INVALID_OUTPUT;
}

static int test_texture_replace_contract(
    const AcgcMetalSinkStatus init_status
) {
    AcgcMetalPacketConsumerOutput valid;
    AcgcMetalPacketConsumerOutput candidate;
    AcgcMetalPacketConsumerOutput before;
    AcgcMetalSinkSnapshot before_submit;
    AcgcMetalSinkSnapshot after_submit;
    const uint32_t expected_pixel = UINT32_C(0x112A39FF);

    CHECK(make_texture_replace_output(&valid));
    before = valid;

    candidate = valid;
    candidate.canonical_tev_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_STAGED_UNRENDERED;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = valid;
    candidate.canonical_texture_binding.selected_map =
        PC_GX_TEXTURE_RAW_MAP_COUNT;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = valid;
    candidate.canonical_tev.stages[0].tex_map = 1;
    candidate.canonical_texture_binding.selected_map = 1;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = valid;
    candidate.canonical_texture_binding.selected_texcoord = 1;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = valid;
    candidate.canonical_texture_binding.vertex_count = 2;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = valid;
    candidate.canonical_texture_binding.texcoord_words[0][0] =
        UINT32_C(0x7FC00000);
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = valid;
    candidate.canonical_resource_stage.valid = 0;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = valid;
    candidate.canonical_resource_stage.attempt_id = 0;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = valid;
    candidate.canonical_resource_stage.image_mask = 0;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = valid;
    candidate.canonical_resource_stage.decoded_image_mask = 0;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = valid;
    candidate.canonical_resource_stage.decoded_rgba_byte_sizes[0]--;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = valid;
    candidate.canonical_resource_stage.descriptions[0].width = 0;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = valid;
    candidate.canonical_resource_stage.descriptions[0].data_size--;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = valid;
    candidate.canonical_resource_stage.samplers[0].wrap_s = 99;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = valid;
    candidate.canonical_resource_stage.samplers[0].min_filter =
        ACGC_RENDERER_FIXTURE_FILTER_NEAR_MIP_NEAR;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = valid;
    candidate.canonical_tev.stages[0].color_d = 10;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = valid;
    candidate.canonical_tev.stages[0].color_op =
        ACGC_GX_CANONICAL_TEV_OPERATION_SUB;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = valid;
    candidate.canonical_tev.stages[0].k_color_sel = 1;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = valid;
    candidate.canonical_tev.stages[0].ind_stage = 1;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = valid;
    candidate.canonical_tev.swap_tables[0].g = 0;
    CHECK(expect_invalid_texture_output(&candidate));

    acgc_metal_sink_get_snapshot(&before_submit);
    CHECK(acgc_metal_sink_submit(&valid) ==
          (init_status == ACGC_METAL_SINK_OK
              ? ACGC_METAL_SINK_OK
              : ACGC_METAL_SINK_NO_DEVICE));
    acgc_metal_sink_get_snapshot(&after_submit);
    CHECK(after_submit.submit_count == before_submit.submit_count + 1);
    CHECK(memcmp(&before, &valid, sizeof(valid)) == 0);
    if (init_status == ACGC_METAL_SINK_OK) {
        CHECK(after_submit.completed_count == before_submit.completed_count + 1);
        CHECK(after_submit.readback_count == before_submit.readback_count + 1);
        if (after_submit.last_pixel_rgba8 != expected_pixel) {
            fprintf(stderr, "texture pixel 0x%08X (expected 0x%08X)\n",
                    after_submit.last_pixel_rgba8, expected_pixel);
        }
        CHECK(after_submit.last_pixel_rgba8 == expected_pixel);
        CHECK(after_submit.last_checksum != 0);
    } else {
        CHECK(after_submit.completed_count == before_submit.completed_count);
        CHECK(after_submit.readback_count == before_submit.readback_count);
    }
    return 0;
}

static int test_live_texture_replace_stage_shape(
    const AcgcMetalSinkStatus init_status
) {
    AcgcMetalPacketConsumerOutput live;
    AcgcMetalPacketConsumerOutput candidate;
    AcgcMetalSinkSnapshot before;
    AcgcMetalSinkSnapshot after;
    const AcgcMetalSinkStatus expected_status =
        init_status == ACGC_METAL_SINK_OK
            ? ACGC_METAL_SINK_OK
            : ACGC_METAL_SINK_NO_DEVICE;

    CHECK(make_live_texture_replace_output(&live));
    CHECK(live.canonical_resource_stage.image_mask == UINT32_C(0xFF));
    CHECK(live.canonical_resource_stage.decoded_image_mask == UINT32_C(0xFF));
    CHECK(live.canonical_resource_stage.tlut_mask == (UINT32_C(1) << 15));
    CHECK(live.canonical_resource_stage.image_byte_sizes[0] == 2048);
    CHECK(live.canonical_resource_stage.image_byte_sizes[7] == 32);
    CHECK(live.canonical_resource_stage.descriptions[0].format ==
          ACGC_RENDERER_FIXTURE_TF_C4);
    CHECK(live.canonical_resource_stage.descriptions[7].format ==
          ACGC_RENDERER_FIXTURE_TF_I8);
    CHECK(live.canonical_resource_stage.descriptions[0].tlut_entries == 16);
    CHECK(live.canonical_resource_stage.descriptions[0].tlut_data_size == 32);
    CHECK(live.canonical_resource_stage.samplers[0].wrap_s ==
          ACGC_RENDERER_FIXTURE_WRAP_MIRROR);
    CHECK(live.canonical_resource_stage.samplers[0].min_filter ==
          ACGC_RENDERER_FIXTURE_FILTER_LINEAR);

    /* The map-7 control has the last loop iteration's dimensions, so it
     * cannot expose a selected-map-0 sizing mistake. */
    candidate = live;
    candidate.canonical_tev.stages[0].tex_map = 7;
    candidate.canonical_texture_binding.selected_map = 7;
    acgc_metal_sink_get_snapshot(&before);
    CHECK(acgc_metal_sink_submit(&candidate) == expected_status);
    acgc_metal_sink_get_snapshot(&after);
    CHECK(after.submit_count == before.submit_count + 1);
    if (init_status == ACGC_METAL_SINK_OK) {
        CHECK(after.completed_count == before.completed_count + 1);
        CHECK(after.readback_count == before.readback_count + 1);
    } else {
        CHECK(after.completed_count == before.completed_count);
        CHECK(after.readback_count == before.readback_count);
    }

    /* The live-equivalent active map is map 0, not the final staged map. */
    acgc_metal_sink_get_snapshot(&before);
    CHECK(acgc_metal_sink_submit(&live) == expected_status);
    acgc_metal_sink_get_snapshot(&after);
    CHECK(after.submit_count == before.submit_count + 1);
    if (init_status == ACGC_METAL_SINK_OK) {
        CHECK(after.completed_count == before.completed_count + 1);
        CHECK(after.readback_count == before.readback_count + 1);
        CHECK(after.last_checksum != 0);
    } else {
        CHECK(after.completed_count == before.completed_count);
        CHECK(after.readback_count == before.readback_count);
    }

    /* Full-stage safety remains strict even though map 7 is not selected. */
    candidate = live;
    candidate.canonical_resource_stage.descriptions[7].width = 0;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = live;
    candidate.canonical_resource_stage.samplers[7].wrap_s = 99;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = live;
    candidate.canonical_resource_stage.decoded_rgba_byte_sizes[7]--;
    CHECK(expect_invalid_texture_output(&candidate));
    candidate = live;
    candidate.canonical_resource_stage.tlut_mask = 0;
    CHECK(expect_invalid_texture_output(&candidate));
    return 0;
}

static int test_cpu_contract(
    AcgcMetalPacketConsumerOutput* output,
    AcgcMetalSinkStatus* init_status
) {
    AcgcMetalPacketConsumerOutput invalid_output;
    AcgcMetalPacketConsumerOutput multi_output;
    AcgcMetalSinkSnapshot before_staged;
    AcgcMetalSinkSnapshot after_staged;
    AcgcMetalSinkSnapshot snapshot;
    AcgcMetalSinkSnapshot before_dynamic;
    AcgcMetalSinkSnapshot after_dynamic;
    AcgcMetalPacketConsumerOutput dynamic_output;
    uint32_t vertex_index;

    acgc_metal_sink_get_snapshot(&snapshot);
    CHECK(snapshot.initialized == 0);
    CHECK(acgc_metal_sink_submit(NULL) == ACGC_METAL_SINK_NOT_INITIALIZED);
    CHECK(make_packet_output(output));
    CHECK(output->geometry.vertex_count ==
          ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES);
    CHECK(output->geometry.draw_count == ACGC_RENDERER_GEOMETRY_MAX_DRAWS);
    CHECK(output->geometry.draws[0].primitive ==
          ACGC_RENDERER_PRIMITIVE_TRIANGLES);
    CHECK(output->geometry.draws[0].first_vertex == 0);
    CHECK(output->geometry.draws[0].vertex_count ==
          ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES);

    *init_status = acgc_metal_sink_init();
    CHECK(*init_status == ACGC_METAL_SINK_OK ||
          *init_status == ACGC_METAL_SINK_NO_DEVICE);
    CHECK(acgc_metal_sink_init() == *init_status);
    acgc_metal_sink_get_snapshot(&snapshot);
    CHECK(snapshot.initialized == 1);
    CHECK(snapshot.available == (*init_status == ACGC_METAL_SINK_OK));

    invalid_output = *output;
    invalid_output.geometry.reserved = 1;
    CHECK(acgc_metal_sink_submit(&invalid_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&snapshot);
    CHECK(snapshot.submit_count == 1);
    CHECK(snapshot.completed_count == 0);
    CHECK(snapshot.readback_count == 0);
    CHECK(snapshot.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* Only the two typed sink sources are admissible; malformed source kinds
     * must stop before allocation even when the semantic payload is valid. */
    CHECK(expect_invalid_source_kind(
        output,
        ACGC_METAL_PACKET_CONSUMER_SOURCE_NONE));
    CHECK(expect_invalid_source_kind(output, 99));

    multi_output = *output;
    multi_output.source_kind =
        ACGC_METAL_PACKET_CONSUMER_SOURCE_CANONICAL_PLAN;
    multi_output.semantic_version = 0;
    multi_output.v2_extension_rendering_status = 0;
    multi_output.v3_extension_rendering_status = 0;
    multi_output.v4_extension_rendering_status = 0;
    multi_output.canonical_tev_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_STAGED_UNRENDERED;
    multi_output.canonical_blend_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_DISPOSITION_STAGED_UNRENDERED;
    multi_output.canonical_blend.mode =
        ACGC_GX_SEMANTIC_V3_BLEND_MODE_LOGIC;
    multi_output.canonical_blend.source_factor =
        ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_SOURCE_ALPHA;
    multi_output.canonical_blend.destination_factor =
        ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_SOURCE_ALPHA;
    multi_output.canonical_blend.logic_op = ACGC_GX_SEMANTIC_V3_LOGIC_XOR;
    set_passthrough_canonical_alpha(&multi_output);
    set_mapped_canonical_raster(&multi_output);
    set_inactive_canonical_fog(&multi_output);
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* Blend staging is rejected independently of the TEV disposition. */
    multi_output.canonical_tev_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_VERTEX_COLOR_PASSTHROUGH;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* The exact mapped disposition carries position-correct GX factors. */
    multi_output.canonical_blend_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_DISPOSITION_MAPPED;
    multi_output.canonical_blend.mode =
        ACGC_GX_SEMANTIC_V3_BLEND_MODE_BLEND;
    multi_output.canonical_blend.source_factor =
        ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_SOURCE_COLOR;
    multi_output.canonical_blend.destination_factor =
        ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_SOURCE_COLOR;
    multi_output.canonical_blend.logic_op = ACGC_GX_SEMANTIC_V3_LOGIC_CLEAR;
    multi_output.state.blend.enabled = 1;
    multi_output.state.blend.source_rgb_factor =
        ACGC_METAL_BLEND_DESTINATION_COLOR;
    multi_output.state.blend.destination_rgb_factor =
        ACGC_METAL_BLEND_ONE_MINUS_SOURCE_COLOR;
    multi_output.state.blend.source_alpha_factor =
        ACGC_METAL_BLEND_DESTINATION_COLOR;
    multi_output.state.blend.destination_alpha_factor =
        ACGC_METAL_BLEND_ONE_MINUS_SOURCE_COLOR;
    multi_output.state.blend.rgb_operation = ACGC_METAL_BLEND_ADD;
    multi_output.state.blend.alpha_operation = ACGC_METAL_BLEND_ADD;

    /* Alpha staging is rejected before any Metal allocation or encode. */
    multi_output.canonical_alpha_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_DISPOSITION_STAGED_UNRENDERED;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* A passthrough disposition with a non-tautological predicate is malformed. */
    set_passthrough_canonical_alpha(&multi_output);
    multi_output.canonical_alpha.comp0 =
        ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN + 1;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* The sink also checks the exact alpha-write relationship independently. */
    set_passthrough_canonical_alpha(&multi_output);
    multi_output.canonical_alpha.alpha_update_enable =
        multi_output.alpha_write_enabled ==
            ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX
        ? ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MIN
        : ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* A valid live-shaped Raster remains staged and is rejected before
     * submit/completion/readback can reach the Metal command path. */
    multi_output.canonical_tev_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_VERTEX_COLOR_PASSTHROUGH;
    multi_output.canonical_blend_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_DISPOSITION_MAPPED;
    multi_output.canonical_alpha_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_DISPOSITION_PASSTHROUGH;
    set_mapped_canonical_raster(&multi_output);
    multi_output.canonical_raster.viewport_bits[2] = bits_from_float(640.0f);
    multi_output.canonical_raster.viewport_bits[3] = bits_from_float(480.0f);
    multi_output.canonical_raster.scissor[2] = 640;
    multi_output.canonical_raster.scissor[3] = 480;
    multi_output.canonical_raster_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_DISPOSITION_STAGED_UNRENDERED;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* A mapped disposition cannot disguise a canonical/state value mismatch. */
    set_mapped_canonical_raster(&multi_output);
    multi_output.canonical_raster.viewport_bits[2] = bits_from_float(63.0f);
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* Invalid canonical words remain fail-closed even with MAPPED selected. */
    set_mapped_canonical_raster(&multi_output);
    multi_output.canonical_raster.reserved[0] = 1;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* The copied canonical cull value must correlate with materialized state. */
    set_mapped_canonical_raster(&multi_output);
    multi_output.state.raster.cull_mode = ACGC_METAL_CULL_FRONT;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);
    multi_output.state.raster.cull_mode = output->state.raster.cull_mode;
    set_mapped_canonical_raster(&multi_output);

    /* Fog staging is rejected before any Metal allocation or encoding. */
    multi_output.canonical_fog_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_FOG_DISPOSITION_STAGED_UNRENDERED;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* An active Fog value cannot be mislabeled as inactive. */
    set_inactive_canonical_fog(&multi_output);
    multi_output.canonical_fog.fog_type = ACGC_GX_CANONICAL_FOG_TYPE_PERSP_LIN;
    multi_output.canonical_fog.start_bits = bits_from_float(0.25f);
    multi_output.canonical_fog.end_bits = bits_from_float(0.75f);
    multi_output.canonical_fog.near_bits = bits_from_float(0.1f);
    multi_output.canonical_fog.far_bits = bits_from_float(1.0f);
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* A copied Fog value with invalid reserved data remains fail-closed. */
    set_inactive_canonical_fog(&multi_output);
    multi_output.canonical_fog.reserved[0] = 1;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* Enabling range adjustment also makes the Fog value unrendered. */
    set_inactive_canonical_fog(&multi_output);
    multi_output.canonical_fog.range_adjust_enable = 1;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* The exact passthrough disposition retains the existing sink contract. */
    set_inactive_canonical_fog(&multi_output);
    set_passthrough_canonical_alpha(&multi_output);
    multi_output.canonical_tev_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_VERTEX_COLOR_PASSTHROUGH;

    /* The exact decomp-shaped 640x480 Raster maps independently of the old
     * 64x64 fixture path and remains bounded through submit/readback. */
    dynamic_output = multi_output;
    set_decomp_dynamic_canonical_raster(&dynamic_output);
    CHECK(dynamic_output.canonical_raster_disposition ==
          ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_DISPOSITION_MAPPED);
    CHECK(dynamic_output.state.viewport.width == bits_from_float(640.0f));
    CHECK(dynamic_output.state.viewport.height == bits_from_float(480.0f));
    CHECK(dynamic_output.canonical_raster.scissor[2] == 640);
    CHECK(dynamic_output.canonical_raster.scissor[3] == 480);
    CHECK(expect_invalid_mapped_raster_field(
        &dynamic_output,
        &dynamic_output.canonical_raster.line_width,
        6));
    CHECK(expect_invalid_mapped_raster_field(
        &dynamic_output,
        &dynamic_output.canonical_raster.line_tex_offsets,
        1));
    CHECK(expect_invalid_mapped_raster_field(
        &dynamic_output,
        &dynamic_output.canonical_raster.point_size,
        7));
    CHECK(expect_invalid_mapped_raster_field(
        &dynamic_output,
        &dynamic_output.canonical_raster.point_tex_offsets,
        1));
    CHECK(expect_invalid_mapped_raster_field(
        &dynamic_output,
        &dynamic_output.canonical_raster.line_texcoord_mask,
        1));
    CHECK(expect_invalid_mapped_raster_field(
        &dynamic_output,
        &dynamic_output.canonical_raster.point_texcoord_mask,
        1));
    CHECK(expect_invalid_mapped_raster_field(
        &dynamic_output,
        &dynamic_output.canonical_raster.field_mode,
        1));
    dynamic_output.state.viewport.width = bits_from_float(1706.0f);
    dynamic_output.canonical_raster.viewport_bits[2] =
        bits_from_float(1706.0f);
    dynamic_output.canonical_raster.scissor[2] = 1705;
    acgc_metal_sink_get_snapshot(&before_dynamic);
    CHECK(acgc_metal_sink_submit(&dynamic_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_dynamic);
    CHECK(after_dynamic.submit_count == before_dynamic.submit_count + 1);
    CHECK(after_dynamic.completed_count == before_dynamic.completed_count);
    CHECK(after_dynamic.readback_count == before_dynamic.readback_count);
    set_decomp_dynamic_canonical_raster(&dynamic_output);
    dynamic_output.geometry.draws[0].primitive = 0;
    acgc_metal_sink_get_snapshot(&before_dynamic);
    CHECK(acgc_metal_sink_submit(&dynamic_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_dynamic);
    CHECK(after_dynamic.submit_count == before_dynamic.submit_count + 1);
    CHECK(after_dynamic.completed_count == before_dynamic.completed_count);
    CHECK(after_dynamic.readback_count == before_dynamic.readback_count);
    dynamic_output.geometry.draws[0].primitive =
        multi_output.geometry.draws[0].primitive;
    set_decomp_dynamic_canonical_raster(&dynamic_output);
    dynamic_output.canonical_raster.scissor[2] = 639;
    acgc_metal_sink_get_snapshot(&before_dynamic);
    CHECK(acgc_metal_sink_submit(&dynamic_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_dynamic);
    CHECK(after_dynamic.submit_count == before_dynamic.submit_count + 1);
    CHECK(after_dynamic.completed_count == before_dynamic.completed_count);
    CHECK(after_dynamic.readback_count == before_dynamic.readback_count);
    set_decomp_dynamic_canonical_raster(&dynamic_output);
    dynamic_output.canonical_raster.clip_mode =
        ACGC_GX_CANONICAL_RASTER_CLIP_MODE_DISABLE;
    acgc_metal_sink_get_snapshot(&before_dynamic);
    CHECK(acgc_metal_sink_submit(&dynamic_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_dynamic);
    CHECK(after_dynamic.submit_count == before_dynamic.submit_count + 1);
    CHECK(after_dynamic.completed_count == before_dynamic.completed_count);
    CHECK(after_dynamic.readback_count == before_dynamic.readback_count);
    set_decomp_dynamic_canonical_raster(&dynamic_output);
    acgc_metal_sink_get_snapshot(&before_dynamic);
    CHECK(acgc_metal_sink_submit(&dynamic_output) ==
          (*init_status == ACGC_METAL_SINK_OK
              ? ACGC_METAL_SINK_OK
              : ACGC_METAL_SINK_NO_DEVICE));
    acgc_metal_sink_get_snapshot(&after_dynamic);
    CHECK(after_dynamic.submit_count == before_dynamic.submit_count + 1);
    if (*init_status == ACGC_METAL_SINK_OK) {
        CHECK(after_dynamic.completed_count ==
              before_dynamic.completed_count + 1);
        CHECK(after_dynamic.readback_count ==
              before_dynamic.readback_count + 1);
        CHECK(after_dynamic.last_checksum != 0);
        CHECK((after_dynamic.last_pixel_rgba8 & UINT32_C(0xFF)) ==
              UINT32_C(0xFF));
    } else {
        CHECK(after_dynamic.completed_count == before_dynamic.completed_count);
        CHECK(after_dynamic.readback_count == before_dynamic.readback_count);
    }

    for (vertex_index = 0;
         vertex_index < ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
         vertex_index++) {
        multi_output.geometry.vertices[vertex_index] =
            output->geometry.vertices[vertex_index %
                ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES];
    }
    multi_output.geometry.vertex_count = ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
    multi_output.geometry.draws[0].vertex_count =
        ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
    CHECK(acgc_renderer_geometry_validate(&multi_output.geometry));
    CHECK(multi_output.geometry.draw_count == ACGC_RENDERER_GEOMETRY_MAX_DRAWS);
    CHECK(multi_output.geometry.draws[0].vertex_count ==
          ACGC_RENDERER_GEOMETRY_MAX_VERTICES);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          (*init_status == ACGC_METAL_SINK_OK
              ? ACGC_METAL_SINK_OK
              : ACGC_METAL_SINK_NO_DEVICE));
    return 0;
}

int main(void) {
    AcgcMetalPacketConsumerOutput output;
    AcgcMetalSinkSnapshot first;
    AcgcMetalSinkSnapshot second;
    AcgcMetalSinkSnapshot after_shutdown;
    AcgcMetalSinkStatus init_status;

    @autoreleasepool {
        CHECK(test_cpu_contract(&output, &init_status) == 0);
        CHECK(test_texture_replace_contract(init_status) == 0);
        CHECK(test_live_texture_replace_stage_shape(init_status) == 0);
        if (init_status == ACGC_METAL_SINK_NO_DEVICE) {
            CHECK(acgc_metal_sink_submit(&output) ==
                  ACGC_METAL_SINK_NO_DEVICE);
            acgc_metal_sink_get_snapshot(&first);
            CHECK(first.available == 0);
            CHECK(first.completed_count == 0);
            CHECK(first.readback_count == 0);
            CHECK(first.last_status == ACGC_METAL_SINK_NO_DEVICE);
            acgc_metal_sink_shutdown();
            acgc_metal_sink_shutdown();
            puts("Metal sink: CPU contract PASS; SKIP (no macOS Metal device available)");
            return SKIP_NO_METAL;
        }

        CHECK(acgc_metal_sink_submit(&output) == ACGC_METAL_SINK_OK);
        acgc_metal_sink_get_snapshot(&first);
        CHECK(first.submit_count > 0);
        CHECK(first.completed_count == first.readback_count);
        CHECK(first.completed_count > 0);
        CHECK(first.last_status == ACGC_METAL_SINK_OK);
        CHECK(first.last_pixel_rgba8 != UINT32_C(0x000000FF));
        CHECK((first.last_pixel_rgba8 & UINT32_C(0xFF)) == UINT32_C(0xFF));
        CHECK(first.last_checksum != 0);

        /* A second synchronous pass must produce the same bounded readback. */
        CHECK(acgc_metal_sink_submit(&output) == ACGC_METAL_SINK_OK);
        acgc_metal_sink_get_snapshot(&second);
        CHECK(second.submit_count == first.submit_count + 1);
        CHECK(second.completed_count == first.completed_count + 1);
        CHECK(second.readback_count == first.readback_count + 1);
        CHECK(second.last_status == ACGC_METAL_SINK_OK);
        CHECK(second.last_pixel_rgba8 == first.last_pixel_rgba8);
        CHECK(second.last_checksum == first.last_checksum);

        acgc_metal_sink_shutdown();
        acgc_metal_sink_shutdown();
        acgc_metal_sink_get_snapshot(&after_shutdown);
        CHECK(after_shutdown.initialized == 0);
        CHECK(after_shutdown.available == 0);
        CHECK(acgc_metal_sink_submit(&output) ==
              ACGC_METAL_SINK_NOT_INITIALIZED);
    }

    puts("Metal sink: PASS (synchronous offscreen completion/readback; no live-frame claim)");
    return 0;
}
