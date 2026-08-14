#include "acgc/gx_semantic_packet.h"

#include <string.h>

static int binary32_is_finite(uint32_t bits) {
    /* IEEE-754 exponent all-ones encodes both infinities and NaNs. */
    return (bits & UINT32_C(0x7F800000)) != UINT32_C(0x7F800000);
}

static int binary32_words_are_finite(const uint32_t* words, size_t count) {
    size_t index;

    if (words == NULL) {
        return 0;
    }
    for (index = 0; index < count; index++) {
        if (!binary32_is_finite(words[index])) {
            return 0;
        }
    }
    return 1;
}

static int words_are_zero(const uint32_t* words, size_t count) {
    size_t index;

    if (words == NULL) {
        return 0;
    }
    for (index = 0; index < count; index++) {
        if (words[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int vertex_is_valid(const AcgcGxSemanticVertex* vertex) {
    if (vertex == NULL ||
        !binary32_words_are_finite(vertex->position, 3) ||
        !binary32_words_are_finite(vertex->normal, 3) ||
        !binary32_words_are_finite(vertex->texcoord0, 2)) {
        return 0;
    }
    return 1;
}

static int vertex_is_zero(const AcgcGxSemanticVertex* vertex) {
    return vertex != NULL &&
        words_are_zero(vertex->position, 3) &&
        words_are_zero(vertex->normal, 3) &&
        vertex->color_rgba8 == 0 &&
        words_are_zero(vertex->texcoord0, 2);
}

static int vertex_count_is_valid(uint32_t primitive, uint32_t vertex_count) {
    if (vertex_count == 0 || vertex_count > ACGC_GX_SEMANTIC_MAX_VERTICES) {
        return 0;
    }
    switch (primitive) {
        case ACGC_GX_SEMANTIC_PRIMITIVE_TRIANGLES:
            return (vertex_count % 3) == 0;
        case ACGC_GX_SEMANTIC_PRIMITIVE_QUADS:
            return (vertex_count % 4) == 0;
        default:
            return 0;
    }
}

int acgc_gx_semantic_packet_init(AcgcGxSemanticPacket* packet) {
    if (packet == NULL) {
        return 0;
    }

    memset(packet, 0, sizeof(*packet));
    packet->version = ACGC_GX_SEMANTIC_PACKET_VERSION;
    packet->byte_size = ACGC_GX_SEMANTIC_PACKET_SIZE;
    packet->primitive = ACGC_GX_SEMANTIC_PRIMITIVE_TRIANGLES;

    /* Identity projection/model-view/normal transforms and white material. */
    packet->transform.projection[0] = UINT32_C(0x3F800000);
    packet->transform.projection[5] = UINT32_C(0x3F800000);
    packet->transform.projection[10] = UINT32_C(0x3F800000);
    packet->transform.projection[15] = UINT32_C(0x3F800000);
    packet->transform.modelview[0] = UINT32_C(0x3F800000);
    packet->transform.modelview[5] = UINT32_C(0x3F800000);
    packet->transform.modelview[10] = UINT32_C(0x3F800000);
    packet->transform.normal[0] = UINT32_C(0x3F800000);
    packet->transform.normal[4] = UINT32_C(0x3F800000);
    packet->transform.normal[8] = UINT32_C(0x3F800000);
    packet->material.color[0] = UINT32_C(0x3F800000);
    packet->material.color[1] = UINT32_C(0x3F800000);
    packet->material.color[2] = UINT32_C(0x3F800000);
    packet->material.color[3] = UINT32_C(0x3F800000);
    return 1;
}

static int semantic_packet_payload_is_valid(
    const AcgcGxSemanticPacket* packet
) {
    uint32_t vertex_index;

    if (packet == NULL ||
        packet->reserved != 0 ||
        !vertex_count_is_valid(packet->primitive, packet->vertex_count) ||
        !binary32_words_are_finite(packet->transform.projection, 16) ||
        !binary32_words_are_finite(packet->transform.modelview, 12) ||
        !binary32_words_are_finite(packet->transform.normal, 9) ||
        !binary32_words_are_finite(packet->material.color, 4) ||
        (packet->material.flags & ~ACGC_GX_SEMANTIC_MATERIAL_SUPPORTED_FLAGS) != 0) {
        return 0;
    }

    if ((packet->material.flags & ACGC_GX_SEMANTIC_MATERIAL_USE_TEXTURE0) != 0) {
        if (packet->material.texture0_key == 0) {
            return 0;
        }
    } else if (packet->material.texture0_key != 0) {
        return 0;
    }

    for (vertex_index = 0; vertex_index < packet->vertex_count; vertex_index++) {
        if (!vertex_is_valid(&packet->vertices[vertex_index])) {
            return 0;
        }
    }
    for (; vertex_index < ACGC_GX_SEMANTIC_MAX_VERTICES; vertex_index++) {
        if (!vertex_is_zero(&packet->vertices[vertex_index])) {
            return 0;
        }
    }
    return 1;
}

int acgc_gx_semantic_packet_validate(const AcgcGxSemanticPacket* packet) {
    if (packet == NULL ||
        packet->version != ACGC_GX_SEMANTIC_PACKET_VERSION ||
        packet->byte_size != ACGC_GX_SEMANTIC_PACKET_SIZE) {
        return 0;
    }
    return semantic_packet_payload_is_valid(packet);
}

static int v2_words_are_zero(const void* value, size_t byte_size) {
    return value != NULL &&
        words_are_zero((const uint32_t*)value, byte_size / sizeof(uint32_t));
}

static int v2_texture_format_is_valid(uint32_t format) {
    switch (format) {
        case ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_I4:
        case ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_I8:
        case ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_IA4:
        case ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_IA8:
        case ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_RGB565:
        case ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_RGB5A3:
        case ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_RGBA8:
        case ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_CMPR:
        case ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_C4:
        case ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_C8:
        case ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_C14X2:
            return 1;
        default:
            return 0;
    }
}

static int v2_texture_format_uses_tlut(uint32_t format) {
    return format == ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_C4 ||
        format == ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_C8 ||
        format == ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_C14X2;
}

int acgc_gx_semantic_packet_v2_channel_source_is_valid(
    const AcgcGxSemanticV2Channel* channel
) {
    if (channel == NULL ||
        channel->enabled != 0 ||
        channel->ambient_source != ACGC_GX_SEMANTIC_V2_CHANNEL_SOURCE_REGISTER ||
        (channel->material_source !=
             ACGC_GX_SEMANTIC_V2_CHANNEL_SOURCE_REGISTER &&
         channel->material_source !=
             ACGC_GX_SEMANTIC_V2_CHANNEL_SOURCE_VERTEX) ||
        channel->light_mask != 0 ||
        channel->diffuse_function != ACGC_GX_SEMANTIC_V2_CHANNEL_DIFFUSE_NONE ||
        channel->attenuation_function != ACGC_GX_SEMANTIC_V2_CHANNEL_ATTENUATION_NONE ||
        !binary32_words_are_finite(channel->ambient_color, 4) ||
        !binary32_words_are_finite(channel->material_color, 4)) {
        return 0;
    }
    return 1;
}

static int v2_texture_generator_is_valid(
    const AcgcGxSemanticV2TextureGenerator* generator,
    uint32_t index
) {
    int uses_tlut;

    if (generator == NULL ||
        generator->enabled != 1 ||
        generator->coordinate_index != index ||
        generator->function != ACGC_GX_SEMANTIC_V2_TEXGEN_FUNCTION_MTX2X4 ||
        generator->source != ACGC_GX_SEMANTIC_V2_TEXGEN_SOURCE_TEX0 ||
        generator->matrix != ACGC_GX_SEMANTIC_V2_TEXGEN_MATRIX_IDENTITY ||
        generator->texture_key == 0 ||
        generator->sampler_key != generator->texture_key ||
        generator->width == 0 || generator->width > 4096 ||
        generator->height == 0 || generator->height > 4096 ||
        !v2_texture_format_is_valid(generator->format)) {
        return 0;
    }

    uses_tlut = v2_texture_format_uses_tlut(generator->format);
    if ((uses_tlut && generator->tlut_key != generator->texture_key) ||
        (!uses_tlut && generator->tlut_key != 0)) {
        return 0;
    }
    return 1;
}

static int v2_color_input_is_valid(uint32_t input) {
    switch (input) {
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_ZERO:
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_PREVIOUS:
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_REGISTER0:
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_REGISTER1:
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_REGISTER2:
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_TEXTURE:
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_RASTER:
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_ONE:
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_HALF:
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_CONSTANT:
            return 1;
        default:
            return 0;
    }
}

static int v2_alpha_input_is_valid(uint32_t input) {
    switch (input) {
        case ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_ZERO:
        case ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_PREVIOUS:
        case ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_REGISTER0:
        case ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_REGISTER1:
        case ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_REGISTER2:
        case ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_TEXTURE:
        case ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_RASTER:
        case ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_CONSTANT:
            return 1;
        default:
            return 0;
    }
}

static int v2_tev_stage_is_valid(
    const AcgcGxSemanticV2TevStage* stage,
    uint32_t texture_generator_count,
    uint32_t channel_count
) {
    uint32_t index;

    if (stage == NULL ||
        stage->color_operation < ACGC_GX_SEMANTIC_V2_TEV_OP_ADD ||
        stage->color_operation > ACGC_GX_SEMANTIC_V2_TEV_OP_SUBTRACT ||
        stage->alpha_operation < ACGC_GX_SEMANTIC_V2_TEV_OP_ADD ||
        stage->alpha_operation > ACGC_GX_SEMANTIC_V2_TEV_OP_SUBTRACT ||
        stage->color_bias != ACGC_GX_SEMANTIC_V2_TEV_BIAS_ZERO ||
        stage->alpha_bias != ACGC_GX_SEMANTIC_V2_TEV_BIAS_ZERO ||
        stage->color_scale != ACGC_GX_SEMANTIC_V2_TEV_SCALE_ONE ||
        stage->alpha_scale != ACGC_GX_SEMANTIC_V2_TEV_SCALE_ONE ||
        stage->color_clamp > 1 || stage->alpha_clamp > 1 ||
        stage->color_output != ACGC_GX_SEMANTIC_V2_TEV_OUTPUT_PREVIOUS ||
        stage->alpha_output != ACGC_GX_SEMANTIC_V2_TEV_OUTPUT_PREVIOUS ||
        stage->texture_coordinate_index >= texture_generator_count ||
        stage->texture_index >= texture_generator_count ||
        stage->raster_channel_index >= channel_count ||
        (stage->constant_color_selector != ACGC_GX_SEMANTIC_V2_TEV_KCOLOR_ONE &&
         stage->constant_color_selector != ACGC_GX_SEMANTIC_V2_TEV_KCOLOR_ONE_QUARTER) ||
        (stage->constant_alpha_selector != ACGC_GX_SEMANTIC_V2_TEV_KALPHA_ONE &&
         stage->constant_alpha_selector != ACGC_GX_SEMANTIC_V2_TEV_KALPHA_ONE_QUARTER) ||
        stage->raster_swap > 3 || stage->texture_swap > 3 ||
        stage->reserved != 0) {
        return 0;
    }

    for (index = 0; index < 4; index++) {
        if (!v2_color_input_is_valid(stage->color_input[index]) ||
            !v2_alpha_input_is_valid(stage->alpha_input[index])) {
            return 0;
        }
    }
    return 1;
}

int acgc_gx_semantic_packet_v2_init(AcgcGxSemanticPacketV2* packet) {
    if (packet == NULL) {
        return 0;
    }

    memset(packet, 0, sizeof(*packet));
    if (!acgc_gx_semantic_packet_init(&packet->base)) {
        return 0;
    }
    packet->base.version = ACGC_GX_SEMANTIC_PACKET_V2_VERSION;
    packet->base.byte_size = ACGC_GX_SEMANTIC_PACKET_V2_SIZE;
    packet->base.material.flags = ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR;
    return 1;
}

int acgc_gx_semantic_packet_v2_validate(const AcgcGxSemanticPacketV2* packet) {
    uint32_t index;

    if (packet == NULL ||
        packet->base.version != ACGC_GX_SEMANTIC_PACKET_V2_VERSION ||
        packet->base.byte_size != ACGC_GX_SEMANTIC_PACKET_V2_SIZE ||
        !semantic_packet_payload_is_valid(&packet->base) ||
        packet->base.material.flags != ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR ||
        packet->base.material.texture0_key != 0 ||
        packet->state_mask != ACGC_GX_SEMANTIC_V2_STATE_SUPPORTED ||
        (packet->projection_type != ACGC_GX_SEMANTIC_V2_PROJECTION_PERSPECTIVE &&
         packet->projection_type != ACGC_GX_SEMANTIC_V2_PROJECTION_ORTHOGRAPHIC) ||
        packet->channel_count == 0 ||
        packet->channel_count > ACGC_GX_SEMANTIC_MAX_CHANNELS ||
        packet->texture_generator_count == 0 ||
        packet->texture_generator_count > ACGC_GX_SEMANTIC_MAX_TEXTURE_GENERATORS ||
        packet->tev_stage_count == 0 ||
        packet->tev_stage_count > ACGC_GX_SEMANTIC_MAX_TEV_STAGES ||
        !v2_words_are_zero(packet->reserved, sizeof(packet->reserved)) ||
        !binary32_words_are_finite(&packet->tev_register_colors[0][0], 12)) {
        return 0;
    }

    for (index = 0; index < 16; index++) {
        if (packet->tev_swap_tables[index / 4][index % 4] > 3) {
            return 0;
        }
    }
    for (index = 0; index < packet->channel_count; index++) {
        if (!acgc_gx_semantic_packet_v2_channel_source_is_valid(
                &packet->channels[index])) {
            return 0;
        }
    }
    for (; index < ACGC_GX_SEMANTIC_MAX_CHANNELS; index++) {
        if (!v2_words_are_zero(&packet->channels[index], sizeof(packet->channels[index]))) {
            return 0;
        }
    }

    for (index = 0; index < packet->texture_generator_count; index++) {
        if (!v2_texture_generator_is_valid(
                &packet->texture_generators[index], index)) {
            return 0;
        }
    }
    for (; index < ACGC_GX_SEMANTIC_MAX_TEXTURE_GENERATORS; index++) {
        if (!v2_words_are_zero(
                &packet->texture_generators[index],
                sizeof(packet->texture_generators[index]))) {
            return 0;
        }
    }

    for (index = 0; index < packet->tev_stage_count; index++) {
        if (!v2_tev_stage_is_valid(
                &packet->tev_stages[index],
                packet->texture_generator_count,
                packet->channel_count)) {
            return 0;
        }
    }
    for (; index < ACGC_GX_SEMANTIC_MAX_TEV_STAGES; index++) {
        if (!v2_words_are_zero(&packet->tev_stages[index], sizeof(packet->tev_stages[index]))) {
            return 0;
        }
    }
    return 1;
}

static int v3_blend_mode_is_valid(uint32_t mode) {
    return mode <= ACGC_GX_SEMANTIC_V3_BLEND_MODE_SUBTRACT;
}

static int v3_blend_factor_is_valid(uint32_t factor) {
    return factor <= ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_DEST_ALPHA;
}

static int v3_logic_op_is_valid(uint32_t logic_op) {
    return logic_op <= ACGC_GX_SEMANTIC_V3_LOGIC_SET;
}

static int v3_texture_matrix_is_valid(
    const AcgcGxSemanticV3TextureMatrix* matrix,
    uint32_t generator_index
) {
    if (matrix == NULL ||
        matrix->generator_index != generator_index ||
        (matrix->matrix_slot != ACGC_GX_SEMANTIC_V3_MATRIX_SLOT_NONE &&
         matrix->matrix_slot >= 10) ||
        matrix->normalize > 1 ||
        matrix->post_matrix != ACGC_GX_SEMANTIC_V3_POST_MATRIX_IDENTITY ||
        !binary32_words_are_finite(matrix->matrix, 12)) {
        return 0;
    }
    return 1;
}

int acgc_gx_semantic_packet_v3_init(AcgcGxSemanticPacketV3* packet) {
    if (packet == NULL) {
        return 0;
    }

    memset(packet, 0, sizeof(*packet));
    packet->version = ACGC_GX_SEMANTIC_PACKET_V3_VERSION;
    packet->byte_size = ACGC_GX_SEMANTIC_PACKET_V3_SIZE;
    if (!acgc_gx_semantic_packet_init(&packet->base)) {
        return 0;
    }
    packet->state_mask = ACGC_GX_SEMANTIC_PACKET_V3_STATE_SUPPORTED;
    return 1;
}

int acgc_gx_semantic_packet_v3_validate(const AcgcGxSemanticPacketV3* packet) {
    uint32_t index;

    if (packet == NULL ||
        packet->version != ACGC_GX_SEMANTIC_PACKET_V3_VERSION ||
        packet->byte_size != ACGC_GX_SEMANTIC_PACKET_V3_SIZE ||
        !acgc_gx_semantic_packet_validate(&packet->base) ||
        packet->state_mask != ACGC_GX_SEMANTIC_PACKET_V3_STATE_SUPPORTED ||
        packet->texture_matrix_count == 0 ||
        packet->texture_matrix_count > ACGC_GX_SEMANTIC_MAX_TEXTURE_MATRICES ||
        !v2_words_are_zero(packet->reserved, sizeof(packet->reserved)) ||
        !v3_blend_mode_is_valid(packet->blend.mode) ||
        !v3_blend_factor_is_valid(packet->blend.source_factor) ||
        !v3_blend_factor_is_valid(packet->blend.destination_factor) ||
        !v3_logic_op_is_valid(packet->blend.logic_op)) {
        return 0;
    }

    for (index = 0; index < packet->texture_matrix_count; index++) {
        if (!v3_texture_matrix_is_valid(
                &packet->texture_matrices[index], index)) {
            return 0;
        }
    }
    for (; index < ACGC_GX_SEMANTIC_MAX_TEXTURE_MATRICES; index++) {
        if (!v2_words_are_zero(
                &packet->texture_matrices[index],
                sizeof(packet->texture_matrices[index]))) {
            return 0;
        }
    }
    return 1;
}

int acgc_gx_semantic_packet_v4_init(AcgcGxSemanticPacketV4* packet) {
    if (packet == NULL) {
        return 0;
    }

    memset(packet, 0, sizeof(*packet));
    packet->version = ACGC_GX_SEMANTIC_PACKET_V4_VERSION;
    packet->byte_size = ACGC_GX_SEMANTIC_PACKET_V4_SIZE;
    if (!acgc_gx_semantic_packet_init(&packet->base)) {
        return 0;
    }
    packet->state_mask = ACGC_GX_SEMANTIC_PACKET_V4_STATE_SUPPORTED;
    return 1;
}

int acgc_gx_semantic_packet_v4_validate(const AcgcGxSemanticPacketV4* packet) {
    uint32_t index;

    if (packet == NULL ||
        packet->version != ACGC_GX_SEMANTIC_PACKET_V4_VERSION ||
        packet->byte_size != ACGC_GX_SEMANTIC_PACKET_V4_SIZE ||
        !acgc_gx_semantic_packet_validate(&packet->base) ||
        packet->state_mask != ACGC_GX_SEMANTIC_PACKET_V4_STATE_SUPPORTED ||
        packet->texture_matrix_count == 0 ||
        packet->texture_matrix_count > ACGC_GX_SEMANTIC_MAX_TEXTURE_MATRICES ||
        !v2_words_are_zero(packet->reserved, sizeof(packet->reserved)) ||
        !v3_blend_mode_is_valid(packet->blend.mode) ||
        !v3_blend_factor_is_valid(packet->blend.source_factor) ||
        !v3_blend_factor_is_valid(packet->blend.destination_factor) ||
        !v3_logic_op_is_valid(packet->blend.logic_op) ||
        (packet->alpha_update_enable !=
             ACGC_GX_SEMANTIC_V4_ALPHA_UPDATE_DISABLED &&
         packet->alpha_update_enable !=
             ACGC_GX_SEMANTIC_V4_ALPHA_UPDATE_ENABLED)) {
        return 0;
    }

    for (index = 0; index < packet->texture_matrix_count; index++) {
        if (!v3_texture_matrix_is_valid(
                &packet->texture_matrices[index], index)) {
            return 0;
        }
    }
    for (; index < ACGC_GX_SEMANTIC_MAX_TEXTURE_MATRICES; index++) {
        if (!v2_words_are_zero(
                &packet->texture_matrices[index],
                sizeof(packet->texture_matrices[index]))) {
            return 0;
        }
    }
    return 1;
}
