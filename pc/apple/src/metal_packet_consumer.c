#include "acgc/metal_packet_consumer.h"

#include <math.h>
#include <string.h>

static float float_from_bits(uint32_t bits) {
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t bits_from_float(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int map_v4_blend_factor(
    uint32_t factor,
    uint32_t* output
) {
    if (output == NULL) {
        return 0;
    }
    switch (factor) {
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_ZERO:
            *output = ACGC_METAL_BLEND_ZERO;
            return 1;
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_ONE:
            *output = ACGC_METAL_BLEND_ONE;
            return 1;
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_SOURCE_ALPHA:
            *output = ACGC_METAL_BLEND_SOURCE_ALPHA;
            return 1;
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_SOURCE_ALPHA:
            *output = ACGC_METAL_BLEND_ONE_MINUS_SOURCE_ALPHA;
            return 1;
        default:
            return 0;
    }
}

static int configure_v4_render_state(
    const AcgcGxSemanticPacketV4* packet,
    AcgcMetalPacketConsumerOutput* output
) {
    uint32_t source_factor;
    uint32_t destination_factor;

    if (packet == NULL || output == NULL ||
        !map_v4_blend_factor(
            packet->blend.source_factor,
            &source_factor
        ) ||
        !map_v4_blend_factor(
            packet->blend.destination_factor,
            &destination_factor
        )) {
        return 0;
    }

    /* GX_BM_LOGIC and GX_BM_SUBTRACT require separate Metal contracts. */
    if (packet->blend.mode != ACGC_GX_SEMANTIC_V3_BLEND_MODE_NONE &&
        packet->blend.mode != ACGC_GX_SEMANTIC_V3_BLEND_MODE_BLEND) {
        return 0;
    }

    output->state.blend.enabled =
        packet->blend.mode == ACGC_GX_SEMANTIC_V3_BLEND_MODE_BLEND;
    output->state.blend.source_rgb_factor = source_factor;
    output->state.blend.destination_rgb_factor = destination_factor;
    output->state.blend.source_alpha_factor = source_factor;
    output->state.blend.destination_alpha_factor = destination_factor;
    output->alpha_write_enabled =
        packet->alpha_update_enable == ACGC_GX_SEMANTIC_V4_ALPHA_UPDATE_ENABLED;
    return acgc_metal_state_fixture_validate(&output->state);
}

static int build_combined_transform(
    const AcgcGxSemanticPacket* packet,
    AcgcMetalFixedTransform* transform
) {
    float projection[4][4];
    float modelview[4][4];
    float combined[4][4];
    uint32_t row;
    uint32_t column;
    uint32_t inner;

    if (packet == NULL || transform == NULL) {
        return 0;
    }

    memset(modelview, 0, sizeof(modelview));
    for (row = 0; row < 4; row++) {
        for (column = 0; column < 4; column++) {
            projection[row][column] = float_from_bits(
                packet->transform.projection[row * 4 + column]
            );
        }
    }
    for (row = 0; row < 3; row++) {
        for (column = 0; column < 4; column++) {
            modelview[row][column] = float_from_bits(
                packet->transform.modelview[row * 4 + column]
            );
        }
    }
    modelview[3][3] = 1.0f;

    for (row = 0; row < 4; row++) {
        for (column = 0; column < 4; column++) {
            float value = 0.0f;

            for (inner = 0; inner < 4; inner++) {
                value += projection[row][inner] * modelview[inner][column];
            }
            if (!isfinite(value)) {
                return 0;
            }
            combined[row][column] = value;
        }
    }

    /* The Metal fixture consumes four words per column. */
    for (column = 0; column < 4; column++) {
        for (row = 0; row < 4; row++) {
            transform->matrix[column * 4 + row] =
                bits_from_float(combined[row][column]);
        }
    }
    return 1;
}

static float clamped_unit_float(uint32_t bits) {
    float value = float_from_bits(bits);

    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static uint8_t materialize_channel(
    uint8_t vertex_channel,
    float material_channel,
    uint8_t texture_channel
) {
    float value = ((float)vertex_channel / 255.0f) *
        material_channel * ((float)texture_channel / 255.0f);

    if (value <= 0.0f) {
        return 0;
    }
    if (value >= 1.0f) {
        return 255;
    }
    return (uint8_t)(value * 255.0f + 0.5f);
}

static AcgcRendererFixtureColor color_from_rgba_bytes(const uint8_t* rgba) {
    AcgcRendererFixtureColor color = { 0, 0, 0, 0 };

    if (rgba != NULL) {
        color.r = rgba[0];
        color.g = rgba[1];
        color.b = rgba[2];
        color.a = rgba[3];
    }
    return color;
}

static uint32_t pack_fixture_color(AcgcRendererFixtureColor color) {
    return ((uint32_t)color.r << 24) |
        ((uint32_t)color.g << 16) |
        ((uint32_t)color.b << 8) |
        color.a;
}

static int fixture_color_from_float_words(
    const uint32_t words[4],
    AcgcRendererFixtureColor* output
) {
    uint32_t index;
    uint8_t channels[4];

    /* The byte-valued fixture cannot represent signed or HDR TEV registers. */
    if (words == NULL || output == NULL) {
        return 0;
    }
    for (index = 0; index < 4; index++) {
        float value = float_from_bits(words[index]);

        if (!isfinite(value) || value < 0.0f || value > 1.0f) {
            return 0;
        }
        if (value == 0.0f) {
            channels[index] = 0;
        } else if (value == 1.0f) {
            channels[index] = 255;
        } else {
            channels[index] = (uint8_t)(value * 255.0f + 0.5f);
        }
    }
    output->r = channels[0];
    output->g = channels[1];
    output->b = channels[2];
    output->a = channels[3];
    return 1;
}

static int v2_texture_format_uses_tlut(uint32_t format) {
    return format == ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_C4 ||
        format == ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_C8 ||
        format == ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_C14X2;
}

static int v2_texture_source_kind_is_valid(uint32_t source_kind) {
    return source_kind ==
            ACGC_METAL_PACKET_CONSUMER_V2_TEXTURE_SOURCE_RAW_GUEST ||
        source_kind ==
            ACGC_METAL_PACKET_CONSUMER_V2_TEXTURE_SOURCE_EMU64_CONVERTED;
}

static int v2_texture_source_is_valid(
    const AcgcGxSemanticV2TextureGenerator* generator,
    const AcgcMetalPacketConsumerV2TextureSource* source
) {
    uint32_t expected_image_bytes;

    if (generator == NULL || source == NULL || source->generation == 0 ||
        source->image_ptr == NULL || source->image_byte_size == 0 ||
        source->width == 0 || source->width > 1024 || source->height == 0 ||
        source->height > 1024 || source->width != generator->width ||
        source->height != generator->height ||
        source->format != generator->format ||
        ((uintptr_t)source->image_ptr & 0x1Fu) != 0 ||
        source->wrap_s > ACGC_RENDERER_FIXTURE_WRAP_MIRROR ||
        source->wrap_t > ACGC_RENDERER_FIXTURE_WRAP_MIRROR ||
        source->min_filter > ACGC_RENDERER_FIXTURE_FILTER_LINEAR_MIP_LINEAR ||
        source->mag_filter > ACGC_RENDERER_FIXTURE_FILTER_LINEAR_MIP_LINEAR ||
        source->effective_filter >
            ACGC_RENDERER_FIXTURE_FILTER_LINEAR_MIP_LINEAR ||
        (source->effective_filter != source->min_filter &&
         source->effective_filter != ACGC_RENDERER_FIXTURE_FILTER_NEAREST) ||
        source->tlut_is_be > 1 ||
        !v2_texture_source_kind_is_valid(source->source_kind)) {
        return 0;
    }

    expected_image_bytes = acgc_renderer_fixture_texture_bytes(
        source->width,
        source->height,
        source->format
    );
    if (expected_image_bytes == 0 ||
        source->image_byte_size != expected_image_bytes) {
        return 0;
    }

    if (v2_texture_format_uses_tlut(source->format)) {
        if (source->tlut_ptr == NULL || source->tlut_byte_size == 0 ||
            source->tlut_entries == 0 || source->tlut_entries > 0x4000u ||
            source->tlut_byte_size != source->tlut_entries * 2u ||
            source->tlut_format > ACGC_RENDERER_FIXTURE_TL_RGB5A3 ||
            source->tlut_name >= 16 || source->tlut_source_kind ==
                ACGC_METAL_PACKET_CONSUMER_V2_TEXTURE_SOURCE_NONE ||
            !v2_texture_source_kind_is_valid(source->tlut_source_kind) ||
            ((uintptr_t)source->tlut_ptr & 0x1Fu) != 0) {
            return 0;
        }
    } else if (source->tlut_ptr != NULL || source->tlut_byte_size != 0 ||
               source->tlut_format != 0 || source->tlut_entries != 0 ||
               source->tlut_name != UINT32_MAX || source->tlut_source_kind !=
                   ACGC_METAL_PACKET_CONSUMER_V2_TEXTURE_SOURCE_NONE) {
        return 0;
    }
    return 1;
}

static void v2_texture_fixture_from_source(
    const AcgcMetalPacketConsumerV2TextureSource* source,
    const AcgcMetalPacketConsumerV2TextureFixture* template_fixture,
    AcgcMetalPacketConsumerV2TextureFixture* fixture
) {
    *fixture = *template_fixture;
    fixture->description.version = ACGC_RENDERER_FIXTURE_VERSION;
    fixture->description.width = source->width;
    fixture->description.height = source->height;
    fixture->description.format = source->format;
    fixture->description.data_byte_order =
        source->source_kind ==
            ACGC_METAL_PACKET_CONSUMER_V2_TEXTURE_SOURCE_RAW_GUEST ?
                ACGC_RENDERER_FIXTURE_BIG_ENDIAN :
                ACGC_RENDERER_FIXTURE_LITTLE_ENDIAN;
    fixture->description.data_size = source->image_byte_size;
    fixture->description.tlut_format = source->tlut_format;
    fixture->description.tlut_entries = source->tlut_entries;
    fixture->description.tlut_data_size = source->tlut_byte_size;
    fixture->description.tlut_byte_order = source->tlut_is_be ?
        ACGC_RENDERER_FIXTURE_BIG_ENDIAN : ACGC_RENDERER_FIXTURE_LITTLE_ENDIAN;
    fixture->data = (const uint8_t*)source->image_ptr;
    fixture->tlut_data = (const uint8_t*)source->tlut_ptr;
    fixture->sampler.version = ACGC_RENDERER_FIXTURE_VERSION;
    fixture->sampler.wrap_s = source->wrap_s;
    fixture->sampler.wrap_t = source->wrap_t;
    fixture->sampler.min_filter = source->effective_filter;
    fixture->sampler.mag_filter = source->mag_filter;
    fixture->sampler.filtering_enabled =
        source->effective_filter == source->min_filter;
}

static int v2_texture_source_matches(
    const AcgcMetalPacketConsumerV2TextureSource* first,
    const AcgcMetalPacketConsumerV2TextureSource* second
) {
    return first != NULL && second != NULL &&
        first->image_ptr == second->image_ptr &&
        first->image_byte_size == second->image_byte_size &&
        first->tlut_ptr == second->tlut_ptr &&
        first->tlut_byte_size == second->tlut_byte_size &&
        first->tlut_format == second->tlut_format &&
        first->tlut_entries == second->tlut_entries &&
        first->tlut_name == second->tlut_name &&
        first->tlut_is_be == second->tlut_is_be &&
        first->width == second->width && first->height == second->height &&
        first->format == second->format && first->wrap_s == second->wrap_s &&
        first->wrap_t == second->wrap_t &&
        first->min_filter == second->min_filter &&
        first->mag_filter == second->mag_filter &&
        first->effective_filter == second->effective_filter &&
        first->source_kind == second->source_kind &&
        first->tlut_source_kind == second->tlut_source_kind &&
        first->generation == second->generation;
}

static int prepare_v2_texture_fixture(
    const AcgcGxSemanticV2TextureGenerator* generator,
    const AcgcMetalPacketConsumerV2TextureFixture* fixture,
    AcgcRendererFixtureColor* sample
) {
    AcgcRendererFixtureSamplerState sampler_state;
    uint32_t source_bytes;
    uint64_t rgba_bytes;
    int indexed;

    if (generator == NULL || fixture == NULL || sample == NULL ||
        fixture->key == 0 || fixture->key != generator->texture_key ||
        fixture->description.version != ACGC_RENDERER_FIXTURE_VERSION ||
        fixture->description.width != generator->width ||
        fixture->description.height != generator->height ||
        fixture->description.format != generator->format ||
        fixture->data == NULL || fixture->decoded_rgba == NULL) {
        return 0;
    }

    source_bytes = acgc_renderer_fixture_texture_bytes(
        fixture->description.width,
        fixture->description.height,
        fixture->description.format
    );
    rgba_bytes = (uint64_t)fixture->description.width *
        fixture->description.height * 4;
    if (source_bytes == 0 || rgba_bytes > UINT32_MAX ||
        fixture->description.data_size < source_bytes ||
        fixture->decoded_rgba_capacity < (uint32_t)rgba_bytes) {
        return 0;
    }

    indexed = v2_texture_format_uses_tlut(generator->format);
    if (indexed) {
        if (generator->tlut_key != fixture->key ||
            fixture->tlut_data == NULL ||
            fixture->description.tlut_entries == 0 ||
            fixture->description.tlut_data_size <
                fixture->description.tlut_entries * 2) {
            return 0;
        }
    } else if (generator->tlut_key != 0 ||
               fixture->tlut_data != NULL ||
               fixture->description.tlut_entries != 0 ||
               fixture->description.tlut_data_size != 0) {
        return 0;
    }

    if (!acgc_renderer_fixture_resolve_sampler(
            &fixture->sampler, &sampler_state) ||
        !acgc_renderer_fixture_decode_texture(
            &fixture->description,
            fixture->data,
            fixture->tlut_data,
            fixture->decoded_rgba,
            fixture->decoded_rgba_capacity)) {
        return 0;
    }
    *sample = color_from_rgba_bytes(fixture->decoded_rgba);
    return 1;
}

static const AcgcMetalPacketConsumerV2TextureFixture* find_v2_texture_fixture(
    const AcgcMetalPacketConsumerV2TextureFixture* textures,
    uint32_t texture_count,
    uint32_t key
) {
    uint32_t index;

    for (index = 0; index < texture_count; index++) {
        if (textures[index].key == key) {
            return &textures[index];
        }
    }
    return NULL;
}

static int map_v2_color_input(uint32_t input, uint32_t* output) {
    if (output == NULL) {
        return 0;
    }
    switch (input) {
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_ZERO:
            *output = ACGC_RENDERER_FIXTURE_CC_ZERO;
            return 1;
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_PREVIOUS:
            *output = ACGC_RENDERER_FIXTURE_CC_CPREV;
            return 1;
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_REGISTER0:
            *output = ACGC_RENDERER_FIXTURE_CC_C0;
            return 1;
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_REGISTER1:
            *output = ACGC_RENDERER_FIXTURE_CC_C1;
            return 1;
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_REGISTER2:
            *output = ACGC_RENDERER_FIXTURE_CC_C2;
            return 1;
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_TEXTURE:
            *output = ACGC_RENDERER_FIXTURE_CC_TEXC;
            return 1;
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_RASTER:
            *output = ACGC_RENDERER_FIXTURE_CC_RASC;
            return 1;
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_ONE:
            *output = ACGC_RENDERER_FIXTURE_CC_ONE;
            return 1;
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_HALF:
            *output = ACGC_RENDERER_FIXTURE_CC_HALF;
            return 1;
        case ACGC_GX_SEMANTIC_V2_COLOR_INPUT_CONSTANT:
            *output = ACGC_RENDERER_FIXTURE_CC_KONST;
            return 1;
        default:
            return 0;
    }
}

static int map_v2_alpha_input(uint32_t input, uint32_t* output) {
    if (output == NULL) {
        return 0;
    }
    switch (input) {
        case ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_ZERO:
            *output = ACGC_RENDERER_FIXTURE_CA_ZERO;
            return 1;
        case ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_PREVIOUS:
            *output = ACGC_RENDERER_FIXTURE_CA_APREV;
            return 1;
        case ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_REGISTER0:
            *output = ACGC_RENDERER_FIXTURE_CA_A0;
            return 1;
        case ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_REGISTER1:
            *output = ACGC_RENDERER_FIXTURE_CA_A1;
            return 1;
        case ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_REGISTER2:
            *output = ACGC_RENDERER_FIXTURE_CA_A2;
            return 1;
        case ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_TEXTURE:
            *output = ACGC_RENDERER_FIXTURE_CA_TEXA;
            return 1;
        case ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_RASTER:
            *output = ACGC_RENDERER_FIXTURE_CA_RASA;
            return 1;
        case ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_CONSTANT:
            *output = ACGC_RENDERER_FIXTURE_CA_KONST;
            return 1;
        default:
            return 0;
    }
}

static int map_v2_tev_stage(
    const AcgcGxSemanticV2TevStage* source,
    AcgcRendererFixtureTevStage* destination
) {
    uint32_t color_inputs[4];
    uint32_t alpha_inputs[4];
    uint32_t index;

    if (source == NULL || destination == NULL) {
        return 0;
    }
    memset(destination, 0, sizeof(*destination));
    for (index = 0; index < 4; index++) {
        if (!map_v2_color_input(
                source->color_input[index],
                &color_inputs[index]) ||
            !map_v2_alpha_input(
                source->alpha_input[index],
                &alpha_inputs[index])) {
            return 0;
        }
    }
    destination->color_a = color_inputs[0];
    destination->color_b = color_inputs[1];
    destination->color_c = color_inputs[2];
    destination->color_d = color_inputs[3];
    destination->alpha_a = alpha_inputs[0];
    destination->alpha_b = alpha_inputs[1];
    destination->alpha_c = alpha_inputs[2];
    destination->alpha_d = alpha_inputs[3];
    if ((source->color_operation != ACGC_GX_SEMANTIC_V2_TEV_OP_ADD &&
         source->color_operation != ACGC_GX_SEMANTIC_V2_TEV_OP_SUBTRACT) ||
        (source->alpha_operation != ACGC_GX_SEMANTIC_V2_TEV_OP_ADD &&
         source->alpha_operation != ACGC_GX_SEMANTIC_V2_TEV_OP_SUBTRACT) ||
        source->color_bias != ACGC_GX_SEMANTIC_V2_TEV_BIAS_ZERO ||
        source->alpha_bias != ACGC_GX_SEMANTIC_V2_TEV_BIAS_ZERO ||
        source->color_scale != ACGC_GX_SEMANTIC_V2_TEV_SCALE_ONE ||
        source->alpha_scale != ACGC_GX_SEMANTIC_V2_TEV_SCALE_ONE ||
        source->color_output != ACGC_GX_SEMANTIC_V2_TEV_OUTPUT_PREVIOUS ||
        source->alpha_output != ACGC_GX_SEMANTIC_V2_TEV_OUTPUT_PREVIOUS) {
        return 0;
    }
    destination->color_op = source->color_operation ==
        ACGC_GX_SEMANTIC_V2_TEV_OP_SUBTRACT
        ? ACGC_RENDERER_FIXTURE_TEV_SUB
        : ACGC_RENDERER_FIXTURE_TEV_ADD;
    destination->alpha_op = source->alpha_operation ==
        ACGC_GX_SEMANTIC_V2_TEV_OP_SUBTRACT
        ? ACGC_RENDERER_FIXTURE_TEV_SUB
        : ACGC_RENDERER_FIXTURE_TEV_ADD;
    destination->color_bias = ACGC_RENDERER_FIXTURE_TEV_BIAS_ZERO;
    destination->alpha_bias = ACGC_RENDERER_FIXTURE_TEV_BIAS_ZERO;
    destination->color_scale = ACGC_RENDERER_FIXTURE_TEV_SCALE_ONE;
    destination->alpha_scale = ACGC_RENDERER_FIXTURE_TEV_SCALE_ONE;
    destination->color_clamp = source->color_clamp;
    destination->alpha_clamp = source->alpha_clamp;
    destination->color_out = ACGC_RENDERER_FIXTURE_TEV_PREV;
    destination->alpha_out = ACGC_RENDERER_FIXTURE_TEV_PREV;
    switch (source->constant_color_selector) {
        case ACGC_GX_SEMANTIC_V2_TEV_KCOLOR_ONE:
            destination->konst_color_sel = 0;
            break;
        case ACGC_GX_SEMANTIC_V2_TEV_KCOLOR_ONE_QUARTER:
            destination->konst_color_sel = 6;
            break;
        default:
            return 0;
    }
    switch (source->constant_alpha_selector) {
        case ACGC_GX_SEMANTIC_V2_TEV_KALPHA_ONE:
            destination->konst_alpha_sel = 0;
            break;
        case ACGC_GX_SEMANTIC_V2_TEV_KALPHA_ONE_QUARTER:
            destination->konst_alpha_sel = 6;
            break;
        default:
            return 0;
    }
    return 1;
}

static uint8_t swap_component(
    AcgcRendererFixtureColor color,
    uint32_t selector
) {
    switch (selector) {
        case 0: return color.r;
        case 1: return color.g;
        case 2: return color.b;
        default: return color.a;
    }
}

static AcgcRendererFixtureColor apply_swap(
    AcgcRendererFixtureColor color,
    const uint32_t table[4]
) {
    AcgcRendererFixtureColor result;

    result.r = swap_component(color, table[0]);
    result.g = swap_component(color, table[1]);
    result.b = swap_component(color, table[2]);
    result.a = swap_component(color, table[3]);
    return result;
}

static int evaluate_v2_tev_vertex(
    const AcgcGxSemanticPacketV2* packet,
    const AcgcRendererFixtureColor* textures,
    AcgcRendererFixtureColor raster,
    AcgcRendererFixtureColor* output
) {
    AcgcRendererFixtureColor previous = { 0, 0, 0, 0 };
    AcgcRendererFixtureColor registers[3];
    uint32_t register_index;
    uint32_t stage_index;

    if (packet == NULL || textures == NULL || output == NULL) {
        return 0;
    }
    for (register_index = 0; register_index < 3; register_index++) {
        if (!fixture_color_from_float_words(
                packet->tev_register_colors[register_index],
                &registers[register_index])) {
            return 0;
        }
    }

    /*
     * The v2 packet intentionally fixes both output registers to PREVIOUS.
     * Evaluating one stage at a time lets each stage retain its own GX swap
     * selections while carrying the previous color forward.
     */
    for (stage_index = 0; stage_index < packet->tev_stage_count; stage_index++) {
        const AcgcGxSemanticV2TevStage* source =
            &packet->tev_stages[stage_index];
        AcgcRendererFixtureTevState state;
        AcgcRendererFixtureColor stage_output;

        memset(&state, 0, sizeof(state));
        state.version = ACGC_RENDERER_FIXTURE_VERSION;
        state.stage_count = 1;
        state.prev = previous;
        state.reg0 = registers[0];
        state.reg1 = registers[1];
        state.reg2 = registers[2];
        state.texture[0] = apply_swap(
            textures[source->texture_index],
            packet->tev_swap_tables[source->texture_swap]
        );
        state.raster = apply_swap(
            raster,
            packet->tev_swap_tables[source->raster_swap]
        );
        if (!map_v2_tev_stage(source, &state.stages[0]) ||
            !acgc_renderer_fixture_tev_evaluate(&state, &stage_output)) {
            return 0;
        }
        previous = stage_output;
    }
    *output = previous;
    return 1;
}

static uint32_t materialize_vertex_color(
    const AcgcGxSemanticPacket* packet,
    const AcgcGxSemanticVertex* vertex,
    const AcgcRendererFixtureColor* texture_color
) {
    const int use_vertex_color =
        (packet->material.flags & ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR) != 0;
    const uint8_t vertex_channels[4] = {
        use_vertex_color ? (uint8_t)(vertex->color_rgba8 >> 24) : 255,
        use_vertex_color ? (uint8_t)(vertex->color_rgba8 >> 16) : 255,
        use_vertex_color ? (uint8_t)(vertex->color_rgba8 >> 8) : 255,
        use_vertex_color ? (uint8_t)vertex->color_rgba8 : 255
    };
    const uint8_t texture_channels[4] = {
        texture_color->r,
        texture_color->g,
        texture_color->b,
        texture_color->a
    };
    uint8_t channels[4];
    uint32_t channel;

    for (channel = 0; channel < 4; channel++) {
        channels[channel] = materialize_channel(
            vertex_channels[channel],
            clamped_unit_float(packet->material.color[channel]),
            texture_channels[channel]
        );
    }
    return ((uint32_t)channels[0] << 24) |
        ((uint32_t)channels[1] << 16) |
        ((uint32_t)channels[2] << 8) |
        channels[3];
}

static AcgcMetalPacketConsumerStatus prepare_validated_packet(
    const AcgcGxSemanticPacket* packet,
    const AcgcMetalPacketConsumerTexture* texture,
    AcgcMetalPacketConsumerOutput* output,
    uint32_t semantic_version,
    uint32_t v2_extension_rendering_status
) {
    AcgcRendererFixtureColor texture_color = { 255, 255, 255, 255 };
    uint32_t vertex_index;

    if (packet->primitive != ACGC_GX_SEMANTIC_PRIMITIVE_TRIANGLES ||
        packet->vertex_count != ACGC_RENDERER_GEOMETRY_MAX_VERTICES) {
        return ACGC_METAL_PACKET_CONSUMER_UNSUPPORTED_TOPOLOGY;
    }

    if ((packet->material.flags & ACGC_GX_SEMANTIC_MATERIAL_USE_TEXTURE0) != 0) {
        if (texture == NULL) {
            return ACGC_METAL_PACKET_CONSUMER_TEXTURE_REQUIRED;
        }
        if (texture->key != packet->material.texture0_key) {
            return ACGC_METAL_PACKET_CONSUMER_TEXTURE_KEY_MISMATCH;
        }
        texture_color = texture->color;
    }

    memset(output, 0, sizeof(*output));
    if (!acgc_metal_state_fixture_make(&output->state)) {
        return ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID;
    }
    if (!build_combined_transform(packet, &output->state.transform) ||
        !acgc_metal_state_fixture_validate(&output->state)) {
        return ACGC_METAL_PACKET_CONSUMER_TRANSFORM_OVERFLOW;
    }

    output->geometry.version = ACGC_RENDERER_GEOMETRY_VERSION;
    output->geometry.vertex_count = ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
    output->geometry.draw_count = ACGC_RENDERER_GEOMETRY_MAX_DRAWS;
    for (vertex_index = 0;
         vertex_index < ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
         vertex_index++) {
        const AcgcGxSemanticVertex* source = &packet->vertices[vertex_index];
        AcgcRendererVertex* destination = &output->geometry.vertices[vertex_index];

        destination->position_x = source->position[0];
        destination->position_y = source->position[1];
        destination->position_z = source->position[2];
        destination->color_rgba8 = materialize_vertex_color(
            packet,
            source,
            &texture_color
        );
    }
    output->geometry.draws[0].primitive = ACGC_RENDERER_PRIMITIVE_TRIANGLES;
    output->geometry.draws[0].first_vertex = 0;
    output->geometry.draws[0].vertex_count = ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
    if (!acgc_renderer_geometry_validate(&output->geometry)) {
        return ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID;
    }

    output->texture0_color = texture_color;
    output->material_flags = packet->material.flags;
    output->texture0_key = packet->material.texture0_key;
    output->semantic_version = semantic_version;
    output->source_kind = ACGC_METAL_PACKET_CONSUMER_SOURCE_SEMANTIC;
    output->alpha_write_enabled = 1;
    output->v2_extension_rendering_status = v2_extension_rendering_status;
    output->v3_extension_rendering_status = 0;
    output->v4_extension_rendering_status = 0;
    return ACGC_METAL_PACKET_CONSUMER_OK;
}

/*
 * The canonical plan is already a value-only, producer-owned snapshot. This
 * consumer intentionally repeats the section and dependency gates here: a
 * future producer may publish a plan through another path, and the Apple
 * sink-facing subset must never infer support from the fact that a struct is
 * populated. Original Geometry provenance was validated by the plan builder
 * and is intentionally unavailable in this normalized plan; the Geometry
 * checks below therefore use only its normalized values and native selector
 * knownness, without fabricating a wire section, VAT, or index provenance.
 */
enum {
    ACGC_CANONICAL_TEV_COLOR_ZERO =
        ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX,
    ACGC_CANONICAL_TEV_COLOR_RASTER = 10,
    ACGC_CANONICAL_TEV_ALPHA_ZERO =
        ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX,
    ACGC_CANONICAL_TEV_ALPHA_RASTER = 5,
    ACGC_CANONICAL_TEV_CHANNEL_COLOR0A0 = 0
};

static int canonical_plan_words_are_zero(
    const uint32_t* words,
    size_t word_count
) {
    size_t index;

    if (words == NULL) {
        return 0;
    }
    for (index = 0; index < word_count; index++) {
        if (words[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int canonical_plan_bytes_are_zero(
    const void* bytes,
    size_t byte_count
) {
    const uint8_t* cursor = (const uint8_t*)bytes;
    size_t index;

    if (cursor == NULL) {
        return 0;
    }
    for (index = 0; index < byte_count; index++) {
        if (cursor[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int canonical_plan_binary32_is_finite(uint32_t bits) {
    return (bits & UINT32_C(0x7F800000)) != UINT32_C(0x7F800000);
}

static int canonical_plan_pointer_range(
    const void* pointer,
    size_t byte_size,
    uintptr_t* begin,
    uintptr_t* end
) {
    uintptr_t address;

    if (pointer == NULL || (uintmax_t)byte_size > (uintmax_t)UINTPTR_MAX) {
        return 0;
    }
    address = (uintptr_t)pointer;
    if ((uintmax_t)byte_size >
        (uintmax_t)UINTPTR_MAX - (uintmax_t)address) {
        return 0;
    }
    if (begin != NULL) {
        *begin = address;
    }
    if (end != NULL) {
        *end = address + (uintptr_t)byte_size;
    }
    return 1;
}

static int canonical_plan_ranges_overlap(
    uintptr_t first_begin,
    uintptr_t first_end,
    uintptr_t second_begin,
    uintptr_t second_end
) {
    return first_begin < second_end && second_begin < first_end;
}

static int canonical_plan_input_output_ranges_are_valid(
    const AcgcAppleCanonicalPlan* plan,
    const AcgcMetalPacketConsumerOutput* output
) {
    uintptr_t plan_begin;
    uintptr_t plan_end;
    uintptr_t output_begin;
    uintptr_t output_end;

    if (plan == NULL || output == NULL ||
        ((uintptr_t)plan % (uintptr_t)_Alignof(AcgcAppleCanonicalPlan)) != 0 ||
        ((uintptr_t)output %
            (uintptr_t)_Alignof(AcgcMetalPacketConsumerOutput)) != 0 ||
        !canonical_plan_pointer_range(
            plan, sizeof(*plan), &plan_begin, &plan_end) ||
        !canonical_plan_pointer_range(
            output, sizeof(*output), &output_begin, &output_end)) {
        return 0;
    }
    return !canonical_plan_ranges_overlap(
        plan_begin, plan_end, output_begin, output_end);
}

static int canonical_plan_position_matrix_id_to_slot(
    uint32_t id,
    uint32_t* slot
) {
    uint32_t index;

    if (slot == NULL) {
        return 0;
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_COUNT;
         index++) {
        if (id == ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_FIRST +
                index * ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_STRIDE) {
            *slot = index;
            return 1;
        }
    }
    return 0;
}

static uint32_t canonical_plan_renderer_color(uint32_t canonical_rgba8) {
    return ((canonical_rgba8 & UINT32_C(0x000000FF)) << 24) |
        ((canonical_rgba8 & UINT32_C(0x0000FF00)) << 8) |
        ((canonical_rgba8 & UINT32_C(0x00FF0000)) >> 8) |
        ((canonical_rgba8 & UINT32_C(0xFF000000)) >> 24);
}

static int canonical_plan_channels_are_supported(
    const AcgcGxCanonicalChannelState* channels
) {
    const AcgcGxCanonicalChannelRecord* record;

    if (channels == NULL || channels->active_count != 1 ||
        channels->record_valid_mask != 1 ||
        !acgc_gx_canonical_channel_state_validate(channels)) {
        return 0;
    }
    record = &channels->records[0];
    return canonical_plan_bytes_are_zero(record, sizeof(*record)) &&
        canonical_plan_bytes_are_zero(
            &channels->records[1], sizeof(channels->records[1]));
}

static int canonical_plan_texgens_are_inactive(
    const AcgcGxCanonicalTexgenState* texgens
) {
    uint32_t index;

    if (texgens == NULL ||
        !acgc_gx_canonical_texgen_state_validate(texgens) ||
        texgens->header.active_texgen_count != 0 ||
        texgens->header.known_texgen_count != 0 ||
        texgens->header.ordinary_matrix_count != 0 ||
        texgens->header.post_matrix_count != 0 ||
        texgens->header.su_count != 0 ||
        texgens->header.texgen_known_mask != 0 ||
        texgens->header.ordinary_matrix_known_mask != 0 ||
        texgens->header.post_matrix_known_mask != 0 ||
        texgens->header.su_known_mask != 0 ||
        texgens->header.component_known_summary != 0) {
        return 0;
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEXGEN_COUNT; index++) {
        if (!canonical_plan_bytes_are_zero(
                &texgens->texgen[index], sizeof(texgens->texgen[index]))) {
            return 0;
        }
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_COUNT;
         index++) {
        const AcgcGxCanonicalTexgenMatrixRecord* record =
            &texgens->ordinary_matrix[index];
        if (record->last_load_type != 0 ||
            record->last_written_word_count != 0 ||
            record->known_word_mask != 0 ||
            !canonical_plan_words_are_zero(
                record->words,
                ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_3X4)) {
            return 0;
        }
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_COUNT;
         index++) {
        const AcgcGxCanonicalTexgenMatrixRecord* record =
            &texgens->post_matrix[index];
        if (record->last_load_type != 0 ||
            record->last_written_word_count != 0 ||
            record->known_word_mask != 0 ||
            !canonical_plan_words_are_zero(
                record->words,
                ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_3X4)) {
            return 0;
        }
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEXGEN_SU_COUNT; index++) {
        if (!canonical_plan_bytes_are_zero(
                &texgens->su[index], sizeof(texgens->su[index]))) {
            return 0;
        }
    }
    return 1;
}

static int canonical_plan_texture_is_inactive(
    const AcgcGxCanonicalTextureState* texture
) {
    uint32_t index;

    if (texture == NULL ||
        !acgc_gx_canonical_texture_state_validate(texture) ||
        texture->header.known_map_mask != 0 ||
        texture->header.known_map_count != 0 ||
        texture->header.indexed_map_mask != 0 ||
        texture->header.mipmap_map_mask != 0 ||
        texture->header.tlut_present_map_mask != 0 ||
        texture->header.required_map_mask != 0) {
        return 0;
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY; index++) {
        if (!canonical_plan_bytes_are_zero(
                &texture->records[index], sizeof(texture->records[index]))) {
            return 0;
        }
    }
    return 1;
}

static int canonical_plan_tev_is_vertex_color_passthrough(
    const AcgcGxCanonicalTevState* tev
) {
    const AcgcGxCanonicalTevStage* stage;

    if (tev == NULL || !acgc_gx_canonical_tev_state_validate(tev) ||
        tev->header.active_stage_count != 1 ||
        !canonical_plan_bytes_are_zero(
            &tev->stages[1],
            sizeof(tev->stages) - sizeof(tev->stages[0])) ||
        !canonical_plan_bytes_are_zero(
            tev->registers, sizeof(tev->registers)) ||
        !canonical_plan_bytes_are_zero(tev->konst, sizeof(tev->konst)) ||
        !canonical_plan_bytes_are_zero(
            tev->swap_tables, sizeof(tev->swap_tables))) {
        return 0;
    }
    stage = &tev->stages[0];
    return stage->color_a == ACGC_CANONICAL_TEV_COLOR_ZERO &&
        stage->color_b == ACGC_CANONICAL_TEV_COLOR_ZERO &&
        stage->color_c == ACGC_CANONICAL_TEV_COLOR_ZERO &&
        stage->color_d == ACGC_CANONICAL_TEV_COLOR_RASTER &&
        stage->alpha_a == ACGC_CANONICAL_TEV_ALPHA_ZERO &&
        stage->alpha_b == ACGC_CANONICAL_TEV_ALPHA_ZERO &&
        stage->alpha_c == ACGC_CANONICAL_TEV_ALPHA_ZERO &&
        stage->alpha_d == ACGC_CANONICAL_TEV_ALPHA_RASTER &&
        stage->color_op == ACGC_GX_CANONICAL_TEV_OPERATION_ADD &&
        stage->color_bias == ACGC_GX_CANONICAL_TEV_BIAS_MIN &&
        stage->color_scale == ACGC_GX_CANONICAL_TEV_SCALE_MIN &&
        stage->color_clamp == ACGC_GX_CANONICAL_TEV_BOOLEAN_MAX &&
        stage->color_out == ACGC_GX_CANONICAL_TEV_REGISTER_INDEX_MIN &&
        stage->alpha_op == ACGC_GX_CANONICAL_TEV_OPERATION_ADD &&
        stage->alpha_bias == ACGC_GX_CANONICAL_TEV_BIAS_MIN &&
        stage->alpha_scale == ACGC_GX_CANONICAL_TEV_SCALE_MIN &&
        stage->alpha_clamp == ACGC_GX_CANONICAL_TEV_BOOLEAN_MAX &&
        stage->alpha_out == ACGC_GX_CANONICAL_TEV_REGISTER_INDEX_MIN &&
        stage->tex_coord == ACGC_GX_CANONICAL_TEV_TEXCOORD_NULL &&
        stage->tex_map == ACGC_GX_CANONICAL_TEV_TEXMAP_NULL &&
        stage->color_chan == ACGC_CANONICAL_TEV_CHANNEL_COLOR0A0 &&
        stage->k_color_sel == 0 && stage->k_alpha_sel == 0 &&
        stage->ras_swap == 0 && stage->tex_swap == 0 &&
        stage->ind_stage == 0 && stage->ind_format == 0 &&
        stage->ind_bias == 0 && stage->ind_mtx == 0 &&
        stage->ind_wrap_s == 0 && stage->ind_wrap_t == 0 &&
        stage->ind_add_prev == 0 && stage->ind_lod == 0 &&
        stage->ind_alpha == 0 && stage->reserved[0] == 0 &&
        stage->reserved[1] == 0;
}

static int canonical_plan_lighting_is_inactive(
    const AcgcGxCanonicalLightingState* lighting
) {
    return lighting != NULL &&
        acgc_gx_canonical_lighting_state_validate(lighting) &&
        lighting->loaded_mask == 0 &&
        canonical_plan_bytes_are_zero(
            lighting->records, sizeof(lighting->records));
}

static int canonical_plan_blend_factor_to_metal(
    uint32_t factor,
    uint32_t* output
) {
    if (output == NULL) {
        return 0;
    }
    switch (factor) {
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_ZERO:
            *output = ACGC_METAL_BLEND_ZERO;
            return 1;
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_ONE:
            *output = ACGC_METAL_BLEND_ONE;
            return 1;
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_SOURCE_ALPHA:
            *output = ACGC_METAL_BLEND_SOURCE_ALPHA;
            return 1;
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_SOURCE_ALPHA:
            *output = ACGC_METAL_BLEND_ONE_MINUS_SOURCE_ALPHA;
            return 1;
        default:
            return 0;
    }
}

static int canonical_plan_blend_is_supported(
    const AcgcGxCanonicalBlendState* blend,
    uint32_t* source_factor,
    uint32_t* destination_factor
) {
    if (blend == NULL ||
        !acgc_gx_canonical_blend_state_validate(blend) ||
        (blend->mode != 0 && blend->mode != 1) ||
        blend->logic_op != 0 ||
        !canonical_plan_blend_factor_to_metal(
            blend->source_factor, source_factor) ||
        !canonical_plan_blend_factor_to_metal(
            blend->destination_factor, destination_factor)) {
        return 0;
    }
    return 1;
}

static int canonical_plan_depth_compare_to_metal(
    uint32_t compare_function,
    uint32_t* output
) {
    if (output == NULL) {
        return 0;
    }
    switch (compare_function) {
        case ACGC_GX_CANONICAL_DEPTH_COMPARE_MIN:
            *output = ACGC_METAL_DEPTH_NEVER;
            return 1;
        case ACGC_GX_CANONICAL_DEPTH_COMPARE_MIN + 1:
            *output = ACGC_METAL_DEPTH_LESS;
            return 1;
        case ACGC_GX_CANONICAL_DEPTH_COMPARE_MIN + 2:
            *output = ACGC_METAL_DEPTH_EQUAL;
            return 1;
        case ACGC_GX_CANONICAL_DEPTH_COMPARE_MIN + 3:
            *output = ACGC_METAL_DEPTH_LESS_EQUAL;
            return 1;
        case ACGC_GX_CANONICAL_DEPTH_COMPARE_MIN + 4:
            *output = ACGC_METAL_DEPTH_GREATER;
            return 1;
        case ACGC_GX_CANONICAL_DEPTH_COMPARE_MIN + 5:
            *output = ACGC_METAL_DEPTH_NOT_EQUAL;
            return 1;
        case ACGC_GX_CANONICAL_DEPTH_COMPARE_MIN + 6:
            *output = ACGC_METAL_DEPTH_GREATER_EQUAL;
            return 1;
        case ACGC_GX_CANONICAL_DEPTH_COMPARE_MAX:
            *output = ACGC_METAL_DEPTH_ALWAYS;
            return 1;
        default:
            return 0;
    }
}

static int canonical_plan_alpha_is_supported(
    const AcgcGxCanonicalAlphaState* alpha
) {
    return alpha != NULL &&
        acgc_gx_canonical_alpha_state_validate(alpha) &&
        alpha->comp0 == ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX &&
        alpha->ref0 == 0 && alpha->op == ACGC_GX_CANONICAL_ALPHA_OPERATOR_MIN &&
        alpha->comp1 == ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX &&
        alpha->ref1 == 0 &&
        alpha->color_update_enable == ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX &&
        alpha->z_comp_loc_before_tex == 0;
}

static int canonical_plan_depth_is_supported(
    const AcgcGxCanonicalDepthState* depth
) {
    return depth != NULL &&
        acgc_gx_canonical_depth_state_validate(depth) &&
        depth->z_compare_enable == ACGC_GX_CANONICAL_DEPTH_BOOLEAN_MAX;
}

static int canonical_plan_raster_is_supported(
    const AcgcGxCanonicalRasterState* raster,
    uint32_t* cull_mode
) {
    if (raster == NULL || cull_mode == NULL ||
        !acgc_gx_canonical_raster_state_validate(raster)) {
        return 0;
    }
    if (raster->viewport_bits[0] != ACGC_METAL_FLOAT_ZERO ||
        raster->viewport_bits[1] != ACGC_METAL_FLOAT_ZERO ||
        raster->viewport_bits[2] != ACGC_METAL_FLOAT_SIXTY_FOUR ||
        raster->viewport_bits[3] != ACGC_METAL_FLOAT_SIXTY_FOUR ||
        raster->viewport_bits[4] != ACGC_METAL_FLOAT_ZERO ||
        raster->viewport_bits[5] != ACGC_METAL_FLOAT_ONE ||
        raster->scissor[0] != 0 || raster->scissor[1] != 0 ||
        raster->scissor[2] != 64 || raster->scissor[3] != 64 ||
        raster->scissor_offset[0] != 0 || raster->scissor_offset[1] != 0 ||
        raster->clip_mode != ACGC_GX_CANONICAL_RASTER_CLIP_MODE_ENABLE ||
        raster->cull_mode == ACGC_GX_CANONICAL_RASTER_CULL_MODE_ALL ||
        raster->co_planar_enable != 0 || raster->line_width != 0 ||
        raster->line_tex_offsets != 0 || raster->point_size != 0 ||
        raster->point_tex_offsets != 0 || raster->line_texcoord_mask != 0 ||
        raster->point_texcoord_mask != 0 || raster->dither != 0 ||
        raster->dst_alpha_enable != 0 || raster->dst_alpha != 0 ||
        raster->field_mode != 0 || raster->half_aspect_ratio != 0 ||
        raster->field_odd_mask != 0 || raster->field_even_mask != 0 ||
        !canonical_plan_words_are_zero(
            raster->reserved, ACGC_GX_CANONICAL_RASTER_RESERVED_WORD_COUNT)) {
        return 0;
    }
    switch (raster->cull_mode) {
        case ACGC_GX_CANONICAL_RASTER_CULL_MODE_NONE:
            *cull_mode = ACGC_METAL_CULL_NONE;
            return 1;
        case ACGC_GX_CANONICAL_RASTER_CULL_MODE_FRONT:
            *cull_mode = ACGC_METAL_CULL_FRONT;
            return 1;
        case ACGC_GX_CANONICAL_RASTER_CULL_MODE_BACK:
            *cull_mode = ACGC_METAL_CULL_BACK;
            return 1;
        default:
            break;
    }
    return 0;
}

static int canonical_plan_indirect_is_inactive(
    const AcgcGxCanonicalIndirectState* indirect
) {
    return indirect != NULL &&
        acgc_gx_canonical_indirect_state_validate(indirect) &&
        indirect->header.active_indirect_stage_count == 0 &&
        indirect->header.active_order_mask == 0 &&
        indirect->header.matrix_valid_mask == 0 &&
        canonical_plan_bytes_are_zero(
            indirect->orders, sizeof(indirect->orders)) &&
        canonical_plan_bytes_are_zero(
            indirect->matrices, sizeof(indirect->matrices));
}

static int canonical_plan_dynamic_is_inactive(
    const AcgcGxCanonicalDynamicState* dynamic
) {
    return dynamic != NULL &&
        acgc_gx_canonical_dynamic_state_validate(dynamic) &&
        dynamic->header.owner_epoch != 0 &&
        dynamic->header.present_image_mask == 0 &&
        dynamic->header.present_tlut_mask == 0 &&
        dynamic->header.required_image_mask == 0 &&
        dynamic->header.required_tlut_mask == 0 &&
        dynamic->header.present_resource_count == 0 &&
        canonical_plan_bytes_are_zero(
            dynamic->records, sizeof(dynamic->records));
}

static int canonical_plan_geometry_is_supported(
    const AcgcAppleCanonicalPlanGeometry* geometry,
    const AcgcGxCanonicalTransformState* transform,
    uint32_t* matrix_slot
) {
    const uint32_t required_present_mask =
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0);
    const uint32_t position_matrix_mask =
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_PNMTXIDX;
    const uint32_t allowed_present_mask =
        required_present_mask | position_matrix_mask;
    const uint32_t expected_component_mask =
        ACGC_APPLE_CANONICAL_PLAN_COMPONENT_POSITION |
        ACGC_APPLE_CANONICAL_PLAN_COMPONENT_COLOR0;
    uint32_t vertex;
    uint32_t coord;
    uint32_t slot;
    uint32_t selected_id;
    int has_explicit_position_matrix;

    if (geometry == NULL || transform == NULL || matrix_slot == NULL ||
        geometry->primitive != ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES ||
        geometry->vtxfmt >= ACGC_GX_CANONICAL_GEOMETRY_VTXFMT_COUNT ||
        geometry->vertex_count != ACGC_RENDERER_GEOMETRY_MAX_VERTICES ||
        (geometry->present_mask & ~allowed_present_mask) != 0 ||
        (geometry->present_mask & required_present_mask) !=
            required_present_mask ||
        geometry->component_mask != expected_component_mask ||
        !acgc_gx_canonical_transform_state_validate(transform)) {
        return 0;
    }
    has_explicit_position_matrix =
        (geometry->present_mask & position_matrix_mask) != 0;
    if (has_explicit_position_matrix) {
        selected_id = geometry->vertices[0].position_matrix_id;
        if (!canonical_plan_position_matrix_id_to_slot(selected_id, &slot) ||
            (transform->known_mask &
                ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(slot)) == 0) {
            return 0;
        }
    } else {
        selected_id = transform->current_position_id;
        if (!canonical_plan_position_matrix_id_to_slot(selected_id, &slot) ||
            (transform->known_mask &
                ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_KNOWN_MASK) == 0 ||
            (transform->known_mask &
                ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(slot)) == 0) {
            return 0;
        }
    }
    for (vertex = 0;
         vertex < ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
         vertex++) {
        const AcgcAppleCanonicalPlanVertex* source = &geometry->vertices[vertex];

        if (source->present_mask != geometry->present_mask ||
            source->component_mask != expected_component_mask ||
            source->position_matrix_id != selected_id ||
            !canonical_plan_words_are_zero(source->texture_matrix_id, 8) ||
            !canonical_plan_words_are_zero(source->normal, 3) ||
            !canonical_plan_words_are_zero(source->binormal, 3) ||
            !canonical_plan_words_are_zero(source->tangent, 3) ||
            source->color_rgba8[1] != 0) {
            return 0;
        }
        for (coord = 0; coord < 3; coord++) {
            if (!canonical_plan_binary32_is_finite(source->position[coord])) {
                return 0;
            }
        }
        for (coord = 0; coord < ACGC_GX_CANONICAL_TEXGEN_COUNT; coord++) {
            if (!canonical_plan_words_are_zero(source->texcoord[coord], 2)) {
                return 0;
            }
        }
    }
    for (vertex = ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
         vertex < ACGC_APPLE_CANONICAL_PLAN_MAX_VERTEX_COUNT;
         vertex++) {
        if (!canonical_plan_bytes_are_zero(
                &geometry->vertices[vertex],
                sizeof(geometry->vertices[vertex]))) {
            return 0;
        }
    }
    *matrix_slot = slot;
    return 1;
}

static int canonical_plan_sections_are_supported(
    const AcgcAppleCanonicalPlan* plan,
    uint32_t* matrix_slot,
    uint32_t* source_factor,
    uint32_t* destination_factor,
    uint32_t* depth_compare,
    uint32_t* cull_mode
) {
    if (plan == NULL || matrix_slot == NULL || source_factor == NULL ||
        destination_factor == NULL || depth_compare == NULL ||
        cull_mode == NULL) {
        return 0;
    }
    /*
     * These are direct normalized-plan dependency predicates: Geometry's
     * selector/Transform knownness is checked here, TEV must be the exact
     * raster-color/raster-alpha pass-through with no texture, channel 0 must
     * be the one valid disabled channel, and all resource-producing sections
     * must remain inactive.
     */
    if (!acgc_gx_canonical_transform_state_validate(&plan->transform) ||
        (plan->transform.known_mask &
            ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_KNOWN_MASK) == 0 ||
        !canonical_plan_geometry_is_supported(
            &plan->geometry, &plan->transform, matrix_slot) ||
        !canonical_plan_channels_are_supported(&plan->channels) ||
        !canonical_plan_texgens_are_inactive(&plan->texgens) ||
        !canonical_plan_texture_is_inactive(&plan->texture) ||
        !canonical_plan_tev_is_vertex_color_passthrough(&plan->tev) ||
        !canonical_plan_lighting_is_inactive(&plan->lighting) ||
        !canonical_plan_blend_is_supported(
            &plan->blend, source_factor, destination_factor) ||
        !canonical_plan_alpha_is_supported(&plan->alpha) ||
        !canonical_plan_depth_is_supported(&plan->depth) ||
        !canonical_plan_raster_is_supported(&plan->raster, cull_mode) ||
        !acgc_gx_canonical_fog_state_validate(&plan->fog) ||
        !canonical_plan_bytes_are_zero(&plan->fog, sizeof(plan->fog)) ||
        !canonical_plan_indirect_is_inactive(&plan->indirect) ||
        !canonical_plan_dynamic_is_inactive(&plan->dynamic) ||
        !acgc_gx_canonical_texture_dynamic_validate(
            &plan->texture, &plan->dynamic) ||
        !canonical_plan_depth_compare_to_metal(
            plan->depth.z_compare_func, depth_compare)) {
        return 0;
    }
    return 1;
}

static int canonical_plan_build_transform(
    const AcgcAppleCanonicalPlan* plan,
    uint32_t matrix_slot,
    AcgcMetalFixedTransform* transform
) {
    float projection[4][4];
    float modelview[4][4];
    float combined[4][4];
    uint32_t row;
    uint32_t column;
    uint32_t inner;

    if (plan == NULL || transform == NULL || matrix_slot >=
            ACGC_GX_CANONICAL_TRANSFORM_POSITION_SLOT_COUNT) {
        return 0;
    }
    memset(projection, 0, sizeof(projection));
    memset(modelview, 0, sizeof(modelview));
    for (row = 0; row < 4; row++) {
        for (column = 0; column < 4; column++) {
            projection[row][column] = 0.0f;
            modelview[row][column] = 0.0f;
        }
    }
    projection[0][0] = float_from_bits(plan->transform.projection[0]);
    projection[1][1] = float_from_bits(plan->transform.projection[2]);
    projection[2][2] = float_from_bits(plan->transform.projection[4]);
    projection[2][3] = float_from_bits(plan->transform.projection[5]);
    if (plan->transform.projection_type ==
            ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_ORTHOGRAPHIC) {
        projection[0][3] = float_from_bits(plan->transform.projection[1]);
        projection[1][3] = float_from_bits(plan->transform.projection[3]);
        projection[3][3] = 1.0f;
    } else if (plan->transform.projection_type ==
            ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE) {
        projection[0][2] = float_from_bits(plan->transform.projection[1]);
        projection[1][2] = float_from_bits(plan->transform.projection[3]);
        projection[3][2] = -1.0f;
    } else {
        return 0;
    }
    for (row = 0; row < 3; row++) {
        for (column = 0; column < 4; column++) {
            modelview[row][column] = float_from_bits(
                plan->transform.position[matrix_slot][row * 4 + column]);
        }
    }
    modelview[3][3] = 1.0f;
    for (row = 0; row < 4; row++) {
        for (column = 0; column < 4; column++) {
            float value = 0.0f;
            for (inner = 0; inner < 4; inner++) {
                value += projection[row][inner] * modelview[inner][column];
            }
            if (!isfinite(value)) {
                return 0;
            }
            combined[row][column] = value;
        }
    }
    for (column = 0; column < 4; column++) {
        for (row = 0; row < 4; row++) {
            transform->matrix[column * 4 + row] = bits_from_float(
                combined[row][column]);
        }
    }
    return 1;
}

AcgcMetalPacketConsumerStatus acgc_metal_packet_consumer_prepare_canonical_plan(
    const AcgcAppleCanonicalPlan* plan,
    AcgcMetalPacketConsumerOutput* output
) {
    AcgcMetalPacketConsumerOutput candidate;
    AcgcMetalFixedTransform transform;
    uint32_t matrix_slot;
    uint32_t source_factor;
    uint32_t destination_factor;
    uint32_t depth_compare;
    uint32_t cull_mode;
    uint32_t vertex;

    if (!canonical_plan_input_output_ranges_are_valid(plan, output)) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT;
    }
    if (!canonical_plan_sections_are_supported(
            plan,
            &matrix_slot,
            &source_factor,
            &destination_factor,
            &depth_compare,
            &cull_mode) ||
        !canonical_plan_build_transform(plan, matrix_slot, &transform)) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET;
    }

    memset(&candidate, 0, sizeof(candidate));
    candidate.state.version = ACGC_METAL_STATE_FIXTURE_VERSION;
    candidate.state.transform = transform;
    candidate.state.viewport.origin_x = plan->raster.viewport_bits[0];
    candidate.state.viewport.origin_y = plan->raster.viewport_bits[1];
    candidate.state.viewport.width = plan->raster.viewport_bits[2];
    candidate.state.viewport.height = plan->raster.viewport_bits[3];
    candidate.state.viewport.znear = plan->raster.viewport_bits[4];
    candidate.state.viewport.zfar = plan->raster.viewport_bits[5];
    candidate.state.depth.compare_function = depth_compare;
    candidate.state.depth.write_enabled = plan->depth.z_update_enable;
    candidate.state.blend.enabled = plan->blend.mode == 1;
    candidate.state.blend.source_rgb_factor = source_factor;
    candidate.state.blend.destination_rgb_factor = destination_factor;
    candidate.state.blend.source_alpha_factor = source_factor;
    candidate.state.blend.destination_alpha_factor = destination_factor;
    candidate.state.blend.rgb_operation = ACGC_METAL_BLEND_ADD;
    candidate.state.blend.alpha_operation = ACGC_METAL_BLEND_ADD;
    candidate.state.raster.cull_mode = cull_mode;
    /* These two fields are fixed sink contract values, not fixture defaults. */
    candidate.state.raster.front_facing_winding =
        ACGC_METAL_WINDING_COUNTER_CLOCKWISE;
    candidate.state.raster.triangle_fill_mode = ACGC_METAL_TRIANGLE_FILL;
    candidate.geometry.version = ACGC_RENDERER_GEOMETRY_VERSION;
    candidate.geometry.vertex_count = ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
    candidate.geometry.draw_count = ACGC_RENDERER_GEOMETRY_MAX_DRAWS;
    for (vertex = 0;
         vertex < ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
         vertex++) {
        const AcgcAppleCanonicalPlanVertex* source =
            &plan->geometry.vertices[vertex];
        AcgcRendererVertex* destination = &candidate.geometry.vertices[vertex];

        destination->position_x = source->position[0];
        destination->position_y = source->position[1];
        destination->position_z = source->position[2];
        destination->color_rgba8 = canonical_plan_renderer_color(
            source->color_rgba8[0]);
    }
    candidate.geometry.draws[0].primitive = ACGC_RENDERER_PRIMITIVE_TRIANGLES;
    candidate.geometry.draws[0].first_vertex = 0;
    candidate.geometry.draws[0].vertex_count =
        ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
    candidate.texture0_color = (AcgcRendererFixtureColor){255, 255, 255, 255};
    candidate.material_flags = ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR;
    candidate.texture0_key = 0;
    candidate.semantic_version = 0;
    candidate.source_kind = ACGC_METAL_PACKET_CONSUMER_SOURCE_CANONICAL_PLAN;
    candidate.alpha_write_enabled = plan->alpha.alpha_update_enable;

    if (!acgc_metal_state_fixture_validate(&candidate.state) ||
        !acgc_renderer_geometry_validate(&candidate.geometry)) {
        return ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID;
    }
    *output = candidate;
    return ACGC_METAL_PACKET_CONSUMER_OK;
}

/*
 * The packet validator owns the value contract; the Apple seam repeats the
 * channel gate before consuming the V2 prefix so a future validator extension
 * cannot silently turn an unimplemented lighting source into a render path.
 */
static int v2_channel_source_contract_is_supported(
    const AcgcGxSemanticPacketV2* packet
) {
    uint32_t index;

    if (packet == NULL ||
        packet->base.material.flags !=
            ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR ||
        packet->channel_count == 0 ||
        packet->channel_count > ACGC_GX_SEMANTIC_MAX_CHANNELS) {
        return 0;
    }
    for (index = 0; index < packet->channel_count; index++) {
        if (!acgc_gx_semantic_packet_v2_channel_source_is_valid(
                &packet->channels[index])) {
            return 0;
        }
    }
    return 1;
}

AcgcMetalPacketConsumerStatus acgc_metal_packet_consumer_prepare(
    const AcgcGxSemanticPacket* packet,
    const AcgcMetalPacketConsumerTexture* texture,
    AcgcMetalPacketConsumerOutput* output
) {
    if (packet == NULL || output == NULL) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT;
    }
    if (!acgc_gx_semantic_packet_validate(packet)) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET;
    }
    return prepare_validated_packet(
        packet,
        texture,
        output,
        ACGC_GX_SEMANTIC_PACKET_VERSION,
        ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_APPLICABLE
    );
}

AcgcMetalPacketConsumerStatus acgc_metal_packet_consumer_prepare_v2(
    const AcgcGxSemanticPacketV2* packet,
    const AcgcMetalPacketConsumerTexture* texture,
    AcgcMetalPacketConsumerOutput* output
) {
    AcgcMetalPacketConsumerStatus status;

    if (packet == NULL || output == NULL) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT;
    }
    if (!acgc_gx_semantic_packet_v2_validate(packet) ||
        !v2_channel_source_contract_is_supported(packet)) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET;
    }

    /* The extension is validated but intentionally not interpreted here. */
    status = prepare_validated_packet(
        &packet->base,
        texture,
        output,
        ACGC_GX_SEMANTIC_PACKET_V2_VERSION,
        ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_RENDERED
    );
    return status;
}

static int v2_packet_uses_texture(const AcgcGxSemanticPacketV2* packet) {
    uint32_t stage_index;
    uint32_t input_index;

    if (packet == NULL) {
        return 0;
    }
    for (stage_index = 0;
         stage_index < packet->tev_stage_count &&
         stage_index < ACGC_GX_SEMANTIC_MAX_TEV_STAGES;
         stage_index++) {
        for (input_index = 0; input_index < 4; input_index++) {
            if (packet->tev_stages[stage_index].color_input[input_index] ==
                    ACGC_GX_SEMANTIC_V2_COLOR_INPUT_TEXTURE ||
                packet->tev_stages[stage_index].alpha_input[input_index] ==
                    ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_TEXTURE) {
                return 1;
            }
        }
    }
    return 0;
}

AcgcMetalPacketConsumerStatus
acgc_metal_packet_consumer_prepare_v2_texture_tev(
    const AcgcGxSemanticPacketV2* packet,
    const AcgcMetalPacketConsumerV2TextureFixture* textures,
    uint32_t texture_count,
    AcgcMetalPacketConsumerOutput* output
) {
    AcgcRendererFixtureColor texture_samples[
        ACGC_METAL_PACKET_CONSUMER_MAX_V2_TEXTURE_FIXTURES];
    AcgcMetalPacketConsumerStatus status;
    uint32_t generator_index;
    uint32_t stage_index;
    uint32_t vertex_index;

    if (packet == NULL || output == NULL || textures == NULL ||
        texture_count == 0 ||
        texture_count > ACGC_METAL_PACKET_CONSUMER_MAX_V2_TEXTURE_FIXTURES) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT;
    }
    if (!acgc_gx_semantic_packet_v2_validate(packet) ||
        !v2_channel_source_contract_is_supported(packet)) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET;
    }
    if (texture_count != packet->texture_generator_count) {
        return ACGC_METAL_PACKET_CONSUMER_TEXTURE_FIXTURE_INVALID;
    }
    for (stage_index = 0;
         stage_index < packet->tev_stage_count;
         stage_index++) {
        /* The value-only vertex packet exposes only raster channel 0. */
        if (packet->tev_stages[stage_index].raster_channel_index != 0) {
            return ACGC_METAL_PACKET_CONSUMER_TEV_STATE_UNSUPPORTED;
        }
    }

    for (generator_index = 0;
         generator_index < packet->texture_generator_count;
         generator_index++) {
        const AcgcMetalPacketConsumerV2TextureFixture* fixture =
            find_v2_texture_fixture(
                textures,
                texture_count,
                packet->texture_generators[generator_index].texture_key
            );

        if (fixture == NULL || !prepare_v2_texture_fixture(
                &packet->texture_generators[generator_index],
                fixture,
                &texture_samples[generator_index])) {
            return ACGC_METAL_PACKET_CONSUMER_TEXTURE_FIXTURE_INVALID;
        }
    }

    /* The ordinary v2 handoff remains a geometry-only, non-rendered seam. */
    status = prepare_validated_packet(
            &packet->base,
            NULL,
            output,
            ACGC_GX_SEMANTIC_PACKET_V2_VERSION,
            ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_RENDERED
        );
    if (status != ACGC_METAL_PACKET_CONSUMER_OK) {
        return status;
    }

    for (vertex_index = 0;
         vertex_index < ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
         vertex_index++) {
        const AcgcGxSemanticVertex* vertex = &packet->base.vertices[vertex_index];
        const AcgcRendererFixtureColor white = { 255, 255, 255, 255 };
        uint32_t materialized_raster = materialize_vertex_color(
            &packet->base,
            vertex,
            &white
        );
        AcgcRendererFixtureColor raster = {
            (uint8_t)(materialized_raster >> 24),
            (uint8_t)(materialized_raster >> 16),
            (uint8_t)(materialized_raster >> 8),
            (uint8_t)materialized_raster
        };
        AcgcRendererFixtureColor tev_color;

        if (!evaluate_v2_tev_vertex(
                packet,
                texture_samples,
                raster,
                &tev_color)) {
            return ACGC_METAL_PACKET_CONSUMER_TEV_STATE_UNSUPPORTED;
        }
        output->geometry.vertices[vertex_index].color_rgba8 =
            pack_fixture_color(tev_color);
        if (vertex_index == 0) {
            output->v2_tev_color = tev_color;
        }
    }
    if (!acgc_renderer_geometry_validate(&output->geometry)) {
        return ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID;
    }
    output->texture0_color = texture_samples[0];
    output->v2_extension_rendering_status =
        ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_CPU_RESOLVED;
    return ACGC_METAL_PACKET_CONSUMER_OK;
}

static AcgcMetalPacketConsumerStatus
acgc_metal_packet_consumer_prepare_v2_texture_source_tev(
    const AcgcGxSemanticPacketV2* packet,
    const AcgcMetalPacketConsumerV2TextureSideband* sideband,
    AcgcMetalPacketConsumerOutput* output
) {
    AcgcMetalPacketConsumerV2TextureFixture fixtures[
        ACGC_METAL_PACKET_CONSUMER_MAX_V2_TEXTURE_FIXTURES];
    AcgcMetalPacketConsumerV2TextureSource sources[
        ACGC_METAL_PACKET_CONSUMER_MAX_V2_TEXTURE_FIXTURES];
    AcgcMetalPacketConsumerV2TextureSource current;
    AcgcMetalPacketConsumerStatus status;
    uint32_t generator_index;

    if (packet == NULL || sideband == NULL || output == NULL ||
        sideband->textures == NULL || sideband->texture_count == 0 ||
        sideband->texture_count >
            ACGC_METAL_PACKET_CONSUMER_MAX_V2_TEXTURE_FIXTURES ||
        sideband->source_provider == NULL ||
        !acgc_gx_semantic_packet_v2_validate(packet) ||
        sideband->texture_count != packet->texture_generator_count) {
        return ACGC_METAL_PACKET_CONSUMER_TEXTURE_FIXTURE_INVALID;
    }

    for (generator_index = 0;
         generator_index < packet->texture_generator_count;
         generator_index++) {
        const AcgcGxSemanticV2TextureGenerator* generator =
            &packet->texture_generators[generator_index];
        const AcgcMetalPacketConsumerV2TextureFixture* template_fixture =
            find_v2_texture_fixture(
                sideband->textures,
                sideband->texture_count,
                generator->texture_key
            );
        uint32_t map;

        /* The current PC builder emits one V2 generator per TEV stage and
         * records the physical GX map in the corresponding stage. */
        if (template_fixture == NULL ||
            generator_index >= packet->tev_stage_count) {
            return ACGC_METAL_PACKET_CONSUMER_V2_TEXTURE_SOURCE_INVALID;
        }
        map = packet->tev_stages[generator_index].texture_index;
        if (map >= 8 ||
            !sideband->source_provider(
                sideband->source_provider_context,
                map,
                &sources[generator_index]
            ) ||
            !v2_texture_source_is_valid(generator, &sources[generator_index])) {
            return ACGC_METAL_PACKET_CONSUMER_V2_TEXTURE_SOURCE_INVALID;
        }
        v2_texture_fixture_from_source(
            &sources[generator_index],
            template_fixture,
            &fixtures[generator_index]
        );
    }

    status = acgc_metal_packet_consumer_prepare_v2_texture_tev(
        packet,
        fixtures,
        packet->texture_generator_count,
        output
    );
    if (status != ACGC_METAL_PACKET_CONSUMER_OK) {
        return status;
    }

    /* The PC record is borrowed. Re-read every record after decode so a
     * cache replacement, TLUT reload, or shutdown cannot leave the output
     * based on a stale pointer generation. */
    for (generator_index = 0;
         generator_index < packet->texture_generator_count;
         generator_index++) {
        if (generator_index >= packet->tev_stage_count ||
            !sideband->source_provider(
                sideband->source_provider_context,
                packet->tev_stages[generator_index].texture_index,
                &current
            ) ||
            !v2_texture_source_is_valid(
                &packet->texture_generators[generator_index],
                &current
            ) ||
            !v2_texture_source_matches(
                &sources[generator_index],
                &current
            )) {
            memset(output, 0, sizeof(*output));
            return ACGC_METAL_PACKET_CONSUMER_V2_TEXTURE_SOURCE_LIFETIME_CHANGED;
        }
    }
    return ACGC_METAL_PACKET_CONSUMER_OK;
}

AcgcMetalPacketConsumerStatus acgc_metal_packet_consumer_prepare_v3(
    const AcgcGxSemanticPacketV3* packet,
    const AcgcMetalPacketConsumerTexture* texture,
    AcgcMetalPacketConsumerOutput* output
) {
    AcgcMetalPacketConsumerStatus status;

    if (packet == NULL || output == NULL) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT;
    }
    if (!acgc_gx_semantic_packet_v3_validate(packet)) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET;
    }

    /* The state extension is intentionally observed but not interpreted. */
    status = prepare_validated_packet(
        &packet->base,
        texture,
        output,
        ACGC_GX_SEMANTIC_PACKET_V3_VERSION,
        ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_APPLICABLE
    );
    if (status == ACGC_METAL_PACKET_CONSUMER_OK) {
        output->v3_extension_rendering_status =
            ACGC_METAL_PACKET_CONSUMER_V3_EXTENSION_NOT_RENDERED;
    }
    return status;
}

AcgcMetalPacketConsumerStatus acgc_metal_packet_consumer_prepare_v4(
    const AcgcGxSemanticPacketV4* packet,
    const AcgcMetalPacketConsumerTexture* texture,
    AcgcMetalPacketConsumerOutput* output
) {
    AcgcMetalPacketConsumerStatus status;

    if (packet == NULL || output == NULL) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT;
    }
    if (!acgc_gx_semantic_packet_v4_validate(packet)) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET;
    }

    /* V4 is renderable only for the bounded blend/alpha state mapped above. */
    status = prepare_validated_packet(
        &packet->base,
        texture,
        output,
        ACGC_GX_SEMANTIC_PACKET_V4_VERSION,
        ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_APPLICABLE
    );
    if (status == ACGC_METAL_PACKET_CONSUMER_OK) {
        if (!configure_v4_render_state(packet, output)) {
            return ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET;
        }
        /* V4 renders the mapped blend/alpha subset; texture matrices remain
         * an explicit non-rendered extension until the shader consumes them. */
        output->v3_extension_rendering_status =
            ACGC_METAL_PACKET_CONSUMER_V3_EXTENSION_NOT_RENDERED;
    }
    return status;
}

int acgc_metal_packet_consumer_register_runtime_callback(
    AcgcMetalPacketConsumerHandoffContext* handoff,
    AcgcMetalPacketConsumerRuntimeCallback callback,
    void* context
) {
    if (handoff == NULL || callback == NULL) {
        return 0;
    }
    handoff->runtime_callback = callback;
    handoff->runtime_callback_context = context;
    return 1;
}

int acgc_metal_packet_consumer_bind_v2_texture_sideband(
    AcgcMetalPacketConsumerHandoffContext* handoff,
    const AcgcMetalPacketConsumerV2TextureFixture* textures,
    uint32_t texture_count
) {
    if (handoff == NULL) {
        return 0;
    }
    if (textures == NULL || texture_count == 0 ||
        texture_count > ACGC_METAL_PACKET_CONSUMER_MAX_V2_TEXTURE_FIXTURES) {
        handoff->v2_texture_sideband.textures = NULL;
        handoff->v2_texture_sideband.texture_count = 0;
        return 0;
    }
    handoff->v2_texture_sideband.textures = textures;
    handoff->v2_texture_sideband.texture_count = texture_count;
    return 1;
}

int acgc_metal_packet_consumer_bind_v2_texture_source_provider(
    AcgcMetalPacketConsumerHandoffContext* handoff,
    AcgcMetalPacketConsumerV2TextureSourceProvider provider,
    void* context
) {
    if (handoff == NULL) {
        return 0;
    }
    if (provider == NULL) {
        handoff->v2_texture_sideband.source_provider = NULL;
        handoff->v2_texture_sideband.source_provider_context = NULL;
        return 0;
    }
    handoff->v2_texture_sideband.source_provider = provider;
    handoff->v2_texture_sideband.source_provider_context = context;
    return 1;
}

void acgc_metal_packet_consumer_clear_v2_texture_source_provider(
    AcgcMetalPacketConsumerHandoffContext* handoff
) {
    if (handoff == NULL) {
        return;
    }
    handoff->v2_texture_sideband.source_provider = NULL;
    handoff->v2_texture_sideband.source_provider_context = NULL;
}

void acgc_metal_packet_consumer_clear_v2_texture_sideband(
    AcgcMetalPacketConsumerHandoffContext* handoff
) {
    if (handoff == NULL) {
        return;
    }
    handoff->v2_texture_sideband.textures = NULL;
    handoff->v2_texture_sideband.texture_count = 0;
    acgc_metal_packet_consumer_clear_v2_texture_source_provider(handoff);
}

void acgc_metal_packet_consumer_unregister_runtime_callback(
    AcgcMetalPacketConsumerHandoffContext* handoff
) {
    if (handoff == NULL) {
        return;
    }
    handoff->runtime_callback = NULL;
    handoff->runtime_callback_context = NULL;
}

void acgc_metal_packet_consumer_handoff(
    void* context,
    const AcgcGxSemanticPacket* packet
) {
    AcgcMetalPacketConsumerHandoffContext* handoff =
        (AcgcMetalPacketConsumerHandoffContext*)context;
    AcgcMetalPacketConsumerRuntimeCallback runtime_callback;
    void* runtime_callback_context;
    AcgcMetalPacketConsumerStatus status;

    if (handoff == NULL) {
        return;
    }
    status = acgc_metal_packet_consumer_prepare(
        packet,
        handoff->texture,
        handoff->output
    );
    handoff->status = status;

    /* Copy the borrowed pair before invoking it so the callback may unbind. */
    runtime_callback = handoff->runtime_callback;
    runtime_callback_context = handoff->runtime_callback_context;
    if (runtime_callback != NULL) {
        runtime_callback(
            runtime_callback_context,
            status == ACGC_METAL_PACKET_CONSUMER_OK ? handoff->output : NULL,
            status
        );
    }
}

void acgc_metal_packet_consumer_handoff_v2(
    void* context,
    const AcgcGxSemanticPacketV2* packet
) {
    AcgcMetalPacketConsumerHandoffContext* handoff =
        (AcgcMetalPacketConsumerHandoffContext*)context;
    AcgcMetalPacketConsumerRuntimeCallback runtime_callback;
    void* runtime_callback_context;
    AcgcMetalPacketConsumerStatus status;

    if (handoff == NULL) {
        return;
    }
    /* Invalid packets retain the ordinary V2 diagnostic; only a validated
     * textured packet requires the explicit caller-owned sideband. */
    if (packet != NULL && acgc_gx_semantic_packet_v2_validate(packet) &&
        v2_packet_uses_texture(packet)) {
        if (handoff->v2_texture_sideband.textures == NULL ||
            handoff->v2_texture_sideband.texture_count == 0) {
            status =
                ACGC_METAL_PACKET_CONSUMER_V2_TEXTURE_SOURCE_REQUIRED;
        } else if (handoff->v2_texture_sideband.source_provider != NULL) {
            status = acgc_metal_packet_consumer_prepare_v2_texture_source_tev(
                packet,
                &handoff->v2_texture_sideband,
                handoff->output
            );
        } else {
            status = acgc_metal_packet_consumer_prepare_v2_texture_tev(
                packet,
                handoff->v2_texture_sideband.textures,
                handoff->v2_texture_sideband.texture_count,
                handoff->output
            );
        }
    } else {
        status = acgc_metal_packet_consumer_prepare_v2(
            packet,
            handoff->texture,
            handoff->output
        );
    }
    handoff->status = status;

    /* Copy the borrowed pair before invoking it so the callback may unbind. */
    runtime_callback = handoff->runtime_callback;
    runtime_callback_context = handoff->runtime_callback_context;
    if (runtime_callback != NULL) {
        runtime_callback(
            runtime_callback_context,
            status == ACGC_METAL_PACKET_CONSUMER_OK ? handoff->output : NULL,
            status
        );
    }
}

void acgc_metal_packet_consumer_handoff_v3(
    void* context,
    const AcgcGxSemanticPacketV3* packet
) {
    AcgcMetalPacketConsumerHandoffContext* handoff =
        (AcgcMetalPacketConsumerHandoffContext*)context;
    AcgcMetalPacketConsumerRuntimeCallback runtime_callback;
    void* runtime_callback_context;
    AcgcMetalPacketConsumerStatus status;

    if (handoff == NULL) {
        return;
    }
    status = acgc_metal_packet_consumer_prepare_v3(
        packet,
        handoff->texture,
        handoff->output
    );
    handoff->status = status;

    /* Copy the borrowed pair before invoking it so the callback may unbind. */
    runtime_callback = handoff->runtime_callback;
    runtime_callback_context = handoff->runtime_callback_context;
    if (runtime_callback != NULL) {
        runtime_callback(
            runtime_callback_context,
            status == ACGC_METAL_PACKET_CONSUMER_OK ? handoff->output : NULL,
            status
        );
    }
}

void acgc_metal_packet_consumer_handoff_v4(
    void* context,
    const AcgcGxSemanticPacketV4* packet
) {
    AcgcMetalPacketConsumerHandoffContext* handoff =
        (AcgcMetalPacketConsumerHandoffContext*)context;
    AcgcMetalPacketConsumerRuntimeCallback runtime_callback;
    void* runtime_callback_context;
    AcgcMetalPacketConsumerStatus status;

    if (handoff == NULL) {
        return;
    }
    status = acgc_metal_packet_consumer_prepare_v4(
        packet,
        handoff->texture,
        handoff->output
    );
    handoff->status = status;

    /* Copy the borrowed pair before invoking it so the callback may unbind. */
    runtime_callback = handoff->runtime_callback;
    runtime_callback_context = handoff->runtime_callback_context;
    if (runtime_callback != NULL) {
        runtime_callback(
            runtime_callback_context,
            status == ACGC_METAL_PACKET_CONSUMER_OK ? handoff->output : NULL,
            status
        );
    }
}

const char* acgc_metal_packet_consumer_status_string(
    AcgcMetalPacketConsumerStatus status
) {
    switch (status) {
        case ACGC_METAL_PACKET_CONSUMER_OK: return "ok";
        case ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT:
            return "invalid argument";
        case ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET:
            return "invalid GX semantic packet";
        case ACGC_METAL_PACKET_CONSUMER_UNSUPPORTED_TOPOLOGY:
            return "unsupported topology";
        case ACGC_METAL_PACKET_CONSUMER_TEXTURE_REQUIRED:
            return "texture fixture required";
        case ACGC_METAL_PACKET_CONSUMER_TEXTURE_KEY_MISMATCH:
            return "texture key mismatch";
        case ACGC_METAL_PACKET_CONSUMER_TRANSFORM_OVERFLOW:
            return "transform overflow";
        case ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID:
            return "invalid consumer output";
        case ACGC_METAL_PACKET_CONSUMER_TEXTURE_FIXTURE_INVALID:
            return "invalid v2 texture fixture";
        case ACGC_METAL_PACKET_CONSUMER_TEV_STATE_UNSUPPORTED:
            return "unsupported v2 TEV state";
        case ACGC_METAL_PACKET_CONSUMER_V2_TEXTURE_SOURCE_REQUIRED:
            return "v2 texture source required";
        case ACGC_METAL_PACKET_CONSUMER_V2_TEXTURE_SOURCE_INVALID:
            return "invalid v2 texture source metadata";
        case ACGC_METAL_PACKET_CONSUMER_V2_TEXTURE_SOURCE_LIFETIME_CHANGED:
            return "v2 texture source lifetime changed";
    }
    return "unknown consumer status";
}
