#include "acgc/gx_semantic_packet.h"

#include <stdio.h>

/*
 * This fixture crosswalks the decomp GX calls that feed this state:
 * GXSetNumTexGens/GXSetTexCoordGen2, GXInitTexObj[CI],
 * GXInitTlutObj/GXLoadTlut, and the GXSetTev* family.  The PC packet is
 * deliberately synthetic: it exercises only value validation, not a live GX
 * callback, texture upload, Metal consumer, or rendered frame.
 */

static void set_valid_texture_generator(
    AcgcGxSemanticV2TextureGenerator* generator,
    uint32_t index,
    uint32_t texture_key,
    uint32_t format,
    uint32_t tlut_key
) {
    generator->enabled = 1;
    generator->coordinate_index = index;
    generator->function = ACGC_GX_SEMANTIC_V2_TEXGEN_FUNCTION_MTX2X4;
    generator->source = ACGC_GX_SEMANTIC_V2_TEXGEN_SOURCE_TEX0;
    generator->matrix = ACGC_GX_SEMANTIC_V2_TEXGEN_MATRIX_IDENTITY;
    generator->texture_key = texture_key;
    generator->tlut_key = tlut_key;
    generator->sampler_key = texture_key;
    generator->width = 4;
    generator->height = 4;
    generator->format = format;
}

static void set_valid_tev_stage(
    AcgcGxSemanticV2TevStage* stage,
    uint32_t texture_coordinate_index,
    uint32_t texture_index
) {
    stage->color_input[0] = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_PREVIOUS;
    stage->color_input[1] = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_TEXTURE;
    stage->color_input[2] = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_RASTER;
    stage->color_input[3] = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_CONSTANT;
    stage->alpha_input[0] = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_PREVIOUS;
    stage->alpha_input[1] = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_TEXTURE;
    stage->alpha_input[2] = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_RASTER;
    stage->alpha_input[3] = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_CONSTANT;
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
    stage->texture_coordinate_index = texture_coordinate_index;
    stage->texture_index = texture_index;
    stage->raster_channel_index = 0;
    stage->constant_color_selector = ACGC_GX_SEMANTIC_V2_TEV_KCOLOR_ONE;
    stage->constant_alpha_selector = ACGC_GX_SEMANTIC_V2_TEV_KALPHA_ONE;
    stage->raster_swap = 0;
    stage->texture_swap = 0;
    stage->reserved = 0;
}

static void make_valid_packet(AcgcGxSemanticPacketV2* packet) {
    uint32_t index;

    (void)acgc_gx_semantic_packet_v2_init(packet);
    packet->base.vertex_count = 3;
    packet->state_mask = ACGC_GX_SEMANTIC_V2_STATE_SUPPORTED;
    packet->projection_type = ACGC_GX_SEMANTIC_V2_PROJECTION_PERSPECTIVE;
    packet->channel_count = 1;
    packet->texture_generator_count = 2;
    packet->tev_stage_count = 2;

    packet->channels[0].ambient_source =
        ACGC_GX_SEMANTIC_V2_CHANNEL_SOURCE_REGISTER;
    packet->channels[0].material_source =
        ACGC_GX_SEMANTIC_V2_CHANNEL_SOURCE_REGISTER;
    packet->channels[0].diffuse_function =
        ACGC_GX_SEMANTIC_V2_CHANNEL_DIFFUSE_NONE;
    packet->channels[0].attenuation_function =
        ACGC_GX_SEMANTIC_V2_CHANNEL_ATTENUATION_NONE;

    set_valid_texture_generator(
        &packet->texture_generators[0], 0, UINT32_C(0x100),
        ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_RGBA8, 0);
    set_valid_texture_generator(
        &packet->texture_generators[1], 1, UINT32_C(0x200),
        ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_C8, UINT32_C(0x200));

    set_valid_tev_stage(&packet->tev_stages[0], 0, 0);
    set_valid_tev_stage(&packet->tev_stages[1], 1, 1);
    for (index = 0; index < 16; index++) {
        packet->tev_swap_tables[index / 4][index % 4] = index % 4;
    }
}

static int expect_valid(
    const char* label,
    const AcgcGxSemanticPacketV2* packet
) {
    if (!acgc_gx_semantic_packet_v2_validate(packet)) {
        fprintf(stderr, "expected valid packet: %s\n", label);
        return 0;
    }
    return 1;
}

static int expect_invalid(
    const char* label,
    const AcgcGxSemanticPacketV2* packet
) {
    if (acgc_gx_semantic_packet_v2_validate(packet)) {
        fprintf(stderr, "expected rejected packet: %s\n", label);
        return 0;
    }
    return 1;
}

#define EXPECT_INVALID(label, mutation) \
    do { \
        make_valid_packet(&packet); \
        mutation; \
        if (!expect_invalid((label), &packet)) { \
            return 1; \
        } \
    } while (0)

int main(void) {
    AcgcGxSemanticPacketV2 packet;

    make_valid_packet(&packet);
    if (!expect_valid("direct two-generator/two-stage baseline", &packet)) {
        return 1;
    }

    /* TLUT-bearing formats mirror GXInitTexObjCI/GXInitTlutObj/GXLoadTlut. */
    packet.texture_generators[1].format =
        ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_C4;
    if (!expect_valid("C4 texture with matching TLUT", &packet)) {
        return 1;
    }
    packet.texture_generators[1].format =
        ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_C14X2;
    if (!expect_valid("C14X2 texture with matching TLUT", &packet)) {
        return 1;
    }
    packet.texture_generators[1].format =
        ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_RGBA8;
    packet.texture_generators[1].tlut_key = 0;
    if (!expect_valid("non-indexed texture without TLUT", &packet)) {
        return 1;
    }

    /* Texgen and texture identity/shape/format must fail closed. */
    EXPECT_INVALID("disabled texgen", packet.texture_generators[0].enabled = 0);
    EXPECT_INVALID("texgen coordinate mismatch", packet.texture_generators[0].coordinate_index = 1);
    EXPECT_INVALID("unsupported texgen function", packet.texture_generators[0].function = 0);
    EXPECT_INVALID("unsupported texgen source", packet.texture_generators[0].source = 0);
    EXPECT_INVALID("unsupported texgen matrix", packet.texture_generators[0].matrix = 0);
    EXPECT_INVALID("zero texture key", packet.texture_generators[0].texture_key = 0);
    EXPECT_INVALID("sampler key mismatch", packet.texture_generators[0].sampler_key = UINT32_C(0x101));
    EXPECT_INVALID("zero texture width", packet.texture_generators[0].width = 0);
    EXPECT_INVALID("oversized texture height", packet.texture_generators[0].height = 4097);
    EXPECT_INVALID("unsupported texture format", packet.texture_generators[0].format = UINT32_C(0xFFFFFFFF));
    EXPECT_INVALID("indexed texture missing TLUT", packet.texture_generators[1].tlut_key = 0);
    EXPECT_INVALID("indexed texture TLUT mismatch", packet.texture_generators[1].tlut_key = UINT32_C(0x201));
    EXPECT_INVALID("non-indexed texture with TLUT", {
        packet.texture_generators[1].format = ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_RGBA8;
        packet.texture_generators[1].tlut_key = UINT32_C(0x200);
    });

    /* TEV fields mirror GXSetTevOrder/GXSetTev* and direct-stage setup. */
    EXPECT_INVALID("invalid TEV color input", packet.tev_stages[0].color_input[0] = 99);
    EXPECT_INVALID("invalid TEV alpha input", packet.tev_stages[0].alpha_input[0] = 99);
    EXPECT_INVALID("invalid TEV color operation", packet.tev_stages[0].color_operation = 0);
    EXPECT_INVALID("invalid TEV alpha operation", packet.tev_stages[0].alpha_operation = 0);
    EXPECT_INVALID("unsupported TEV color bias", packet.tev_stages[0].color_bias = 2);
    EXPECT_INVALID("unsupported TEV alpha bias", packet.tev_stages[0].alpha_bias = 2);
    EXPECT_INVALID("unsupported TEV color scale", packet.tev_stages[0].color_scale = 2);
    EXPECT_INVALID("unsupported TEV alpha scale", packet.tev_stages[0].alpha_scale = 2);
    EXPECT_INVALID("TEV color clamp out of range", packet.tev_stages[0].color_clamp = 2);
    EXPECT_INVALID("TEV alpha clamp out of range", packet.tev_stages[0].alpha_clamp = 2);
    EXPECT_INVALID("unsupported TEV color output", packet.tev_stages[0].color_output = 2);
    EXPECT_INVALID("unsupported TEV alpha output", packet.tev_stages[0].alpha_output = 2);
    EXPECT_INVALID("TEV texture coordinate out of range", packet.tev_stages[0].texture_coordinate_index = 2);
    EXPECT_INVALID("TEV texture index out of range", packet.tev_stages[0].texture_index = 2);
    EXPECT_INVALID("TEV raster channel out of range", packet.tev_stages[0].raster_channel_index = 1);
    EXPECT_INVALID("unsupported TEV constant color", packet.tev_stages[0].constant_color_selector = 3);
    EXPECT_INVALID("unsupported TEV constant alpha", packet.tev_stages[0].constant_alpha_selector = 3);
    EXPECT_INVALID("TEV raster swap out of range", packet.tev_stages[0].raster_swap = 4);
    EXPECT_INVALID("TEV texture swap out of range", packet.tev_stages[0].texture_swap = 4);
    EXPECT_INVALID("non-zero TEV reserved field", packet.tev_stages[0].reserved = 1);

    /* Packet-wide masks/counts and inactive tails also reject unsupported state. */
    EXPECT_INVALID("missing state-mask bit", packet.state_mask = ACGC_GX_SEMANTIC_V2_STATE_SUPPORTED ^ ACGC_GX_SEMANTIC_V2_STATE_INDIRECT_KNOWN);
    EXPECT_INVALID("unknown state-mask bit", packet.state_mask = UINT32_C(0x400));
    EXPECT_INVALID("unsupported projection", packet.projection_type = 0);
    EXPECT_INVALID("zero channel count", packet.channel_count = 0);
    EXPECT_INVALID("zero texture-generator count", packet.texture_generator_count = 0);
    EXPECT_INVALID("zero TEV-stage count", packet.tev_stage_count = 0);
    EXPECT_INVALID("non-zero packet reserved field", packet.reserved[0] = 1);
    EXPECT_INVALID("non-zero inactive channel tail", {
        packet.channel_count = 1;
        packet.channels[1].enabled = 1;
    });
    EXPECT_INVALID("non-zero inactive texture-generator tail", {
        packet.texture_generator_count = 1;
        packet.texture_generators[1].enabled = 1;
    });
    EXPECT_INVALID("non-zero inactive TEV-stage tail", {
        packet.tev_stage_count = 1;
        packet.tev_stages[1].reserved = 1;
    });

    puts("pc GX texture/TLUT/TEV packet fixtures: PASS");
    puts("proof boundary: synthetic CPU packet validation only; no live callback, Metal, frame, pixel, or playability claim");
    return 0;
}
