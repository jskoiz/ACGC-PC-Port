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
    AcgcMetalPacketConsumerOutput candidate;
    uint32_t vertex_index;

    if (packet->primitive != ACGC_GX_SEMANTIC_PRIMITIVE_TRIANGLES ||
        packet->vertex_count !=
            ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES) {
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

    memset(&candidate, 0, sizeof(candidate));
    if (!acgc_metal_state_fixture_make(&candidate.state)) {
        return ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID;
    }
    if (!build_combined_transform(packet, &candidate.state.transform) ||
        !acgc_metal_state_fixture_validate(&candidate.state)) {
        return ACGC_METAL_PACKET_CONSUMER_TRANSFORM_OVERFLOW;
    }

    candidate.geometry.version = ACGC_RENDERER_GEOMETRY_VERSION;
    candidate.geometry.vertex_count =
        ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES;
    candidate.geometry.draw_count = ACGC_RENDERER_GEOMETRY_MAX_DRAWS;
    for (vertex_index = 0;
         vertex_index < ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES;
         vertex_index++) {
        const AcgcGxSemanticVertex* source = &packet->vertices[vertex_index];
        AcgcRendererVertex* destination =
            &candidate.geometry.vertices[vertex_index];

        destination->position_x = source->position[0];
        destination->position_y = source->position[1];
        destination->position_z = source->position[2];
        destination->color_rgba8 = materialize_vertex_color(
            packet,
            source,
            &texture_color
        );
    }
    candidate.geometry.draws[0].primitive = ACGC_RENDERER_PRIMITIVE_TRIANGLES;
    candidate.geometry.draws[0].first_vertex = 0;
    candidate.geometry.draws[0].vertex_count =
        ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES;
    if (!acgc_renderer_geometry_validate(&candidate.geometry)) {
        return ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID;
    }

    candidate.texture0_color = texture_color;
    candidate.material_flags = packet->material.flags;
    candidate.texture0_key = packet->material.texture0_key;
    candidate.semantic_version = semantic_version;
    candidate.source_kind = ACGC_METAL_PACKET_CONSUMER_SOURCE_SEMANTIC;
    candidate.alpha_write_enabled = 1;
    candidate.v2_extension_rendering_status = v2_extension_rendering_status;
    candidate.v3_extension_rendering_status = 0;
    candidate.v4_extension_rendering_status = 0;
    *output = candidate;
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

static int canonical_plan_resource_stage_ranges_are_valid(
    const AcgcAppleCanonicalPlan* plan,
    const AcgcMetalPacketConsumerCanonicalResourceStage* resource_stage,
    const AcgcMetalPacketConsumerOutput* output
) {
    uintptr_t plan_begin;
    uintptr_t plan_end;
    uintptr_t stage_begin;
    uintptr_t stage_end;
    uintptr_t output_begin;
    uintptr_t output_end;

    if (plan == NULL || resource_stage == NULL || output == NULL ||
        ((uintptr_t)resource_stage %
            (uintptr_t)_Alignof(
                AcgcMetalPacketConsumerCanonicalResourceStage)) != 0 ||
        !canonical_plan_pointer_range(
            resource_stage,
            sizeof(*resource_stage),
            &stage_begin,
            &stage_end
        ) || !canonical_plan_pointer_range(
            plan, sizeof(*plan), &plan_begin, &plan_end
        ) || !canonical_plan_pointer_range(
            output, sizeof(*output), &output_begin, &output_end
        )) {
        return 0;
    }
    return !canonical_plan_ranges_overlap(
            plan_begin, plan_end, stage_begin, stage_end
        ) &&
        !canonical_plan_ranges_overlap(
            plan_begin, plan_end, output_begin, output_end
        ) &&
        !canonical_plan_ranges_overlap(
            stage_begin, stage_end, output_begin, output_end
        );
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

static int canonical_plan_ordinary_texture_matrix_id_is_valid(uint32_t id) {
    uint32_t index;

    /* Zero means that no effective ordinary selector was retained. */
    if (id == 0) {
        return 1;
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_COUNT;
         index++) {
        if (id == ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_FIRST +
                index * ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_STRIDE) {
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

enum {
    ACGC_CANONICAL_CHANNEL_MODE_VERTEX = 0,
    ACGC_CANONICAL_CHANNEL_MODE_AF_NONE = 1
};

static int canonical_plan_channels_are_supported(
    const AcgcGxCanonicalChannelState* channels,
    uint32_t* mode
) {
    const AcgcGxCanonicalChannelRecord* record;

    if (channels == NULL || mode == NULL || channels->active_count != 1 ||
        channels->record_valid_mask != 1 ||
        !acgc_gx_canonical_channel_state_validate(channels)) {
        return 0;
    }
    record = &channels->records[0];
    if (record->channel_index != 0 || record->reserved != 0 ||
        record->alpha.enable != ACGC_GX_CANONICAL_CHANNEL_BOOLEAN_FALSE ||
        record->alpha.ambient_source !=
            ACGC_GX_CANONICAL_CHANNEL_SOURCE_REG ||
        record->alpha.material_source !=
            ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX ||
        record->alpha.light_mask != 0 ||
        record->alpha.diffuse_function !=
            ACGC_GX_CANONICAL_CHANNEL_DIFFUSE_NONE ||
        record->alpha.attenuation_function !=
            ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_NONE ||
        !canonical_plan_bytes_are_zero(
            &channels->records[1], sizeof(channels->records[1]))) {
        return 0;
    }

    if (record->color.enable == ACGC_GX_CANONICAL_CHANNEL_BOOLEAN_FALSE &&
        record->color.ambient_source == ACGC_GX_CANONICAL_CHANNEL_SOURCE_REG &&
        record->color.material_source == ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX &&
        record->color.light_mask == 0 &&
        record->color.diffuse_function ==
            ACGC_GX_CANONICAL_CHANNEL_DIFFUSE_NONE &&
        record->color.attenuation_function ==
            ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_NONE) {
        *mode = ACGC_CANONICAL_CHANNEL_MODE_VERTEX;
        return 1;
    }

    if (record->color.enable == ACGC_GX_CANONICAL_CHANNEL_BOOLEAN_TRUE &&
        record->color.ambient_source == ACGC_GX_CANONICAL_CHANNEL_SOURCE_REG &&
        record->color.material_source == ACGC_GX_CANONICAL_CHANNEL_SOURCE_REG &&
        record->color.light_mask != 0 &&
        record->color.diffuse_function ==
            ACGC_GX_CANONICAL_CHANNEL_DIFFUSE_CLAMP &&
        record->color.attenuation_function ==
            ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_NONE) {
        *mode = ACGC_CANONICAL_CHANNEL_MODE_AF_NONE;
        return 1;
    }
    return 0;
}

static int canonical_plan_texgen_matrix_is_exact(
    const AcgcGxCanonicalTexgenMatrixRecord* matrix,
    uint32_t logical_id,
    uint32_t load_type,
    uint32_t written_word_count,
    uint32_t known_word_mask,
    const uint32_t expected_words[12]
) {
    uint32_t word;

    if (matrix == NULL || expected_words == NULL ||
        matrix->logical_id != logical_id ||
        matrix->last_load_type != load_type ||
        matrix->last_written_word_count != written_word_count ||
        matrix->known_word_mask != known_word_mask) {
        return 0;
    }
    for (word = 0; word < 12; word++) {
        if (!canonical_plan_binary32_is_finite(matrix->words[word]) ||
            matrix->words[word] != expected_words[word]) {
            return 0;
        }
    }
    return 1;
}

static int canonical_plan_texgens_are_supported(
    const AcgcAppleCanonicalPlanGeometry* geometry,
    const AcgcGxCanonicalTexgenState* texgens
) {
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
    const uint32_t expected_present_mask = UINT32_C(0x00002E00);
    const uint32_t expected_component_mask = UINT32_C(0x00000053);
    const AcgcGxCanonicalTexgenRecord* record;
    uint32_t vertex;
    uint32_t coord;

    if (texgens == NULL ||
        !acgc_gx_canonical_texgen_state_validate(texgens)) {
        return 0;
    }

    /* Retained, fully validated state is still harmless when GX has no
     * active generators. Keep this dormant path source-faithful. */
    if (texgens->header.active_texgen_count == 0) {
        return 1;
    }

    /* This is one observed cumulative GX/J2D profile. It admits only the
     * canonical values needed to prove the next consumer frontier; it does
     * not transform or emit texture coordinates. */
    if (geometry == NULL || geometry->present_mask != expected_present_mask ||
        geometry->component_mask != expected_component_mask ||
        texgens->header.active_texgen_count != 2 ||
        texgens->header.texgen_known_mask != UINT32_C(0x000000FF) ||
        texgens->header.known_texgen_count != 8 ||
        texgens->header.ordinary_matrix_count != 2 ||
        texgens->header.ordinary_matrix_known_mask != UINT32_C(0x00000401) ||
        texgens->header.post_matrix_count != 1 ||
        texgens->header.post_matrix_known_mask != UINT32_C(0x00100000) ||
        texgens->header.su_count != 0 || texgens->header.su_known_mask != 0 ||
        texgens->header.component_known_summary !=
            ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_TEXGEN ||
        !canonical_plan_words_are_zero(texgens->header.reserved, 2) ||
        !canonical_plan_bytes_are_zero(texgens->su, sizeof(texgens->su))) {
        return 0;
    }

    record = &texgens->texgen[0];
    if (record->function != ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX2X4 ||
        record->source != ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX0 ||
        record->ordinary_matrix_id != 30 || record->normalize != 0 ||
        record->post_matrix_id != 125 ||
        record->component_known != ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL ||
        !canonical_plan_words_are_zero(record->reserved, 2)) {
        return 0;
    }
    record = &texgens->texgen[1];
    if (record->function != ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX2X4 ||
        record->source != ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX0 ||
        record->ordinary_matrix_id != 60 || record->normalize != 0 ||
        record->post_matrix_id != 125 ||
        record->component_known != ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL ||
        !canonical_plan_words_are_zero(record->reserved, 2)) {
        return 0;
    }
    if (!canonical_plan_texgen_matrix_is_exact(
            &texgens->ordinary_matrix[0], 30,
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX2X4,
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_2X4,
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_2X4,
            ordinary30_words) ||
        !canonical_plan_texgen_matrix_is_exact(
            &texgens->ordinary_matrix[10], 60,
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX3X4,
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_3X4,
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4,
            identity_words) ||
        !canonical_plan_texgen_matrix_is_exact(
            &texgens->post_matrix[20], 125,
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX3X4,
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_3X4,
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4,
            identity_words)) {
        return 0;
    }

    for (vertex = 0; vertex < geometry->vertex_count; vertex++) {
        const AcgcAppleCanonicalPlanVertex* source =
            &geometry->vertices[vertex];

        /* TEX0 is present, TEX1 is absent, yet record1 is retained. The
         * selectors are effective logical IDs from the plan, not raw VAT
         * fields; every vertex must carry the same bounded pair. */
        if (source->texture_matrix_id[0] != 30 ||
            source->texture_matrix_id[1] != 60) {
            return 0;
        }
        for (coord = 2; coord < ACGC_GX_CANONICAL_TEXGEN_COUNT; coord++) {
            if (source->texture_matrix_id[coord] != 0) {
                return 0;
            }
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

/*
 * Keep this cross-section predicate aligned with the canonical-plan builder's
 * TEV dependency check. The surrounding section gates have already validated
 * the complete Texture, Texgen, and Channel values and, for active Texture,
 * the matching value-owned resource stage. This final check only decides
 * whether every active TEV order can refer to those validated sections.
 */
static int canonical_plan_tev_dependencies_are_supported(
    const AcgcGxCanonicalTevState* tev,
    const AcgcGxCanonicalTextureState* texture,
    const AcgcGxCanonicalTexgenState* texgens,
    const AcgcGxCanonicalChannelState* channels
) {
    uint32_t stage_index;

    if (tev == NULL || texture == NULL || texgens == NULL ||
        channels == NULL ||
        !acgc_gx_canonical_tev_state_validate(tev) ||
        !acgc_gx_canonical_texture_state_validate(texture) ||
        !acgc_gx_canonical_texgen_state_validate(texgens) ||
        !acgc_gx_canonical_channel_state_validate(channels)) {
        return 0;
    }
    for (stage_index = 0;
         stage_index < tev->header.active_stage_count;
         stage_index++) {
        const AcgcGxCanonicalTevStage* stage = &tev->stages[stage_index];

        if (stage->tex_map <= ACGC_GX_CANONICAL_TEV_TEXMAP_MAX) {
            if ((texture->header.known_map_mask &
                    (UINT32_C(1) << stage->tex_map)) == 0 ||
                (stage->tex_coord <= ACGC_GX_CANONICAL_TEV_TEXCOORD_MAX &&
                 (texgens->header.active_texgen_count <= stage->tex_coord ||
                  (texgens->header.texgen_known_mask &
                      (UINT32_C(1) << stage->tex_coord)) == 0))) {
                return 0;
            }
        }
        if (stage->color_chan < ACGC_GX_CANONICAL_CHANNEL_STATE_CAPACITY &&
            (channels->record_valid_mask &
                (UINT32_C(1) << stage->color_chan)) == 0) {
            return 0;
        }
    }
    return 1;
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

static float canonical_plan_clamp_unit(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static int canonical_plan_normalize_float_vector(
    const float input[3],
    float output[3]
) {
    float values[3];
    float length_squared = 0.0f;
    float length;
    uint32_t component;

    if (input == NULL || output == NULL) {
        return 0;
    }
    for (component = 0; component < 3; component++) {
        float square;

        values[component] = input[component];
        if (!isfinite(values[component])) {
            return 0;
        }
        square = values[component] * values[component];
        if (!isfinite(square)) {
            return 0;
        }
        length_squared += square;
        if (!isfinite(length_squared)) {
            return 0;
        }
    }
    if (!(length_squared > 0.0f)) {
        return 0;
    }
    length = sqrtf(length_squared);
    if (!isfinite(length) || !(length > 0.0f)) {
        return 0;
    }
    for (component = 0; component < 3; component++) {
        output[component] = values[component] / length;
        if (!isfinite(output[component])) {
            return 0;
        }
    }
    return 1;
}

static int canonical_plan_normalize_binary32_vector(
    const uint32_t input[3],
    float output[3]
) {
    float values[3];
    uint32_t component;

    if (input == NULL || output == NULL) {
        return 0;
    }
    for (component = 0; component < 3; component++) {
        values[component] = float_from_bits(input[component]);
    }
    return canonical_plan_normalize_float_vector(values, output);
}

static int canonical_plan_transform_normal(
    const AcgcGxCanonicalTransformState* transform,
    uint32_t matrix_slot,
    const uint32_t source_normal[3],
    float output[3]
) {
    float source[3];
    float transformed[3];
    uint32_t row;
    uint32_t column;

    if (transform == NULL || source_normal == NULL || output == NULL ||
        matrix_slot >= ACGC_GX_CANONICAL_TRANSFORM_POSITION_SLOT_COUNT ||
        (transform->known_mask &
            ACGC_GX_CANONICAL_TRANSFORM_NORMAL_KNOWN_MASK(matrix_slot)) == 0) {
        return 0;
    }
    for (column = 0; column < 3; column++) {
        source[column] = float_from_bits(source_normal[column]);
        if (!isfinite(source[column])) {
            return 0;
        }
    }
    for (row = 0; row < 3; row++) {
        float value = 0.0f;

        for (column = 0; column < 3; column++) {
            float matrix_value = float_from_bits(
                transform->normal[matrix_slot][row * 3 + column]);
            float product;

            if (!isfinite(matrix_value)) {
                return 0;
            }
            product = matrix_value * source[column];
            if (!isfinite(product)) {
                return 0;
            }
            value += product;
            if (!isfinite(value)) {
                return 0;
            }
        }
        transformed[row] = value;
    }
    return canonical_plan_normalize_float_vector(transformed, output);
}

static float canonical_plan_rgba8_component(
    uint32_t color,
    uint32_t shift
) {
    return (float)((color >> shift) & UINT32_C(0xFF)) / 255.0f;
}

static int canonical_plan_quantize_unit(
    float value,
    uint8_t* output
) {
    float scaled;

    if (output == NULL || !isfinite(value)) {
        return 0;
    }
    value = canonical_plan_clamp_unit(value);
    scaled = value * 255.0f + 0.5f;
    if (!isfinite(scaled)) {
        return 0;
    }
    if (scaled <= 0.0f) {
        *output = 0;
    } else if (scaled >= 255.0f) {
        *output = 255;
    } else {
        *output = (uint8_t)scaled;
    }
    return 1;
}

static int canonical_plan_lighting_is_supported(
    const AcgcAppleCanonicalPlanGeometry* geometry,
    const AcgcGxCanonicalTransformState* transform,
    const AcgcGxCanonicalChannelState* channels,
    const AcgcGxCanonicalLightingState* lighting,
    uint32_t matrix_slot
) {
    const AcgcGxCanonicalChannelRecord* record;
    const uint32_t normal_mask =
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM;
    uint32_t vertex;
    uint32_t slot;

    if (geometry == NULL || transform == NULL || channels == NULL ||
        lighting == NULL || matrix_slot >=
            ACGC_GX_CANONICAL_TRANSFORM_POSITION_SLOT_COUNT ||
        (geometry->present_mask & normal_mask) == 0 ||
        (transform->known_mask &
            ACGC_GX_CANONICAL_TRANSFORM_NORMAL_KNOWN_MASK(matrix_slot)) == 0 ||
        !acgc_gx_canonical_lighting_state_validate(lighting)) {
        return 0;
    }
    record = &channels->records[0];
    if ((lighting->loaded_mask & record->color.light_mask) !=
            record->color.light_mask) {
        return 0;
    }
    for (slot = 0;
         slot < ACGC_GX_CANONICAL_LIGHTING_SLOT_COUNT;
         slot++) {
        if ((record->color.light_mask & (UINT32_C(1) << slot)) != 0) {
            float light[3];

            if (!canonical_plan_normalize_binary32_vector(
                    lighting->records[slot].position, light)) {
                return 0;
            }
        }
    }
    for (vertex = 0; vertex < geometry->vertex_count; vertex++) {
        float normal[3];

        if (!canonical_plan_transform_normal(
                transform,
                matrix_slot,
                geometry->vertices[vertex].normal,
                normal)) {
            return 0;
        }
    }
    return 1;
}

static int canonical_plan_blend_factor_to_metal(
    uint32_t factor,
    int source_factor,
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
        /* GX's numeric color aliases are position-dependent: the source
         * field's value 2/3 names destination color, while the destination
         * field's value 2/3 names source color. */
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_SOURCE_COLOR:
            *output = source_factor
                ? ACGC_METAL_BLEND_DESTINATION_COLOR
                : ACGC_METAL_BLEND_SOURCE_COLOR;
            return 1;
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_SOURCE_COLOR:
            *output = source_factor
                ? ACGC_METAL_BLEND_ONE_MINUS_DESTINATION_COLOR
                : ACGC_METAL_BLEND_ONE_MINUS_SOURCE_COLOR;
            return 1;
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_SOURCE_ALPHA:
            *output = ACGC_METAL_BLEND_SOURCE_ALPHA;
            return 1;
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_SOURCE_ALPHA:
            *output = ACGC_METAL_BLEND_ONE_MINUS_SOURCE_ALPHA;
            return 1;
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_DEST_ALPHA:
            *output = ACGC_METAL_BLEND_DESTINATION_ALPHA;
            return 1;
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_DEST_ALPHA:
            *output = ACGC_METAL_BLEND_ONE_MINUS_DESTINATION_ALPHA;
            return 1;
        default:
            return 0;
    }
}

static int canonical_plan_blend_is_mapped(
    const AcgcGxCanonicalBlendState* blend,
    uint32_t* source_factor,
    uint32_t* destination_factor,
    uint32_t* operation
) {
    if (blend == NULL ||
        !acgc_gx_canonical_blend_state_validate(blend) ||
        operation == NULL ||
        blend->mode == ACGC_GX_SEMANTIC_V3_BLEND_MODE_LOGIC ||
        !canonical_plan_blend_factor_to_metal(
            blend->source_factor, 1, source_factor) ||
        !canonical_plan_blend_factor_to_metal(
            blend->destination_factor, 0, destination_factor)) {
        return 0;
    }
    *operation = blend->mode == ACGC_GX_SEMANTIC_V3_BLEND_MODE_SUBTRACT
        ? ACGC_METAL_BLEND_REVERSE_SUBTRACT
        : ACGC_METAL_BLEND_ADD;
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

static int canonical_plan_alpha_compare_is_true(
    uint32_t compare,
    uint32_t fragment_alpha,
    uint32_t reference
) {
    switch (compare) {
        case ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN:
            return 0;
        case ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN + 1:
            return fragment_alpha < reference;
        case ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN + 2:
            return fragment_alpha == reference;
        case ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN + 3:
            return fragment_alpha <= reference;
        case ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN + 4:
            return fragment_alpha > reference;
        case ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN + 5:
            return fragment_alpha != reference;
        case ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN + 6:
            return fragment_alpha >= reference;
        case ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX:
            return 1;
    }
    return 0;
}

static int canonical_plan_alpha_predicate_is_tautology(
    const AcgcGxCanonicalAlphaState* alpha
) {
    uint32_t fragment_alpha;

    if (alpha == NULL ||
        !acgc_gx_canonical_alpha_state_validate(alpha) ||
        alpha->color_update_enable != ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX) {
        return 0;
    }
    for (fragment_alpha = ACGC_GX_CANONICAL_ALPHA_REFERENCE_MIN;
         fragment_alpha <= ACGC_GX_CANONICAL_ALPHA_REFERENCE_MAX;
         fragment_alpha++) {
        const int first = canonical_plan_alpha_compare_is_true(
            alpha->comp0, fragment_alpha, alpha->ref0);
        const int second = canonical_plan_alpha_compare_is_true(
            alpha->comp1, fragment_alpha, alpha->ref1);
        int result;

        switch (alpha->op) {
            case ACGC_GX_CANONICAL_ALPHA_OPERATOR_MIN:
                result = first && second;
                break;
            case ACGC_GX_CANONICAL_ALPHA_OPERATOR_MIN + 1:
                result = first || second;
                break;
            case ACGC_GX_CANONICAL_ALPHA_OPERATOR_MIN + 2:
                result = first != second;
                break;
            case ACGC_GX_CANONICAL_ALPHA_OPERATOR_MAX:
                result = first == second;
                break;
            default:
                return 0;
        }
        if (!result) {
            return 0;
        }
    }
    return 1;
}

static int canonical_plan_alpha_classify(
    const AcgcGxCanonicalAlphaState* alpha,
    AcgcMetalPacketConsumerCanonicalAlphaDisposition* disposition
) {
    if (alpha == NULL || disposition == NULL ||
        !acgc_gx_canonical_alpha_state_validate(alpha)) {
        return 0;
    }
    *disposition = canonical_plan_alpha_predicate_is_tautology(alpha)
        ? ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_DISPOSITION_PASSTHROUGH
        : ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_DISPOSITION_STAGED_UNRENDERED;
    return 1;
}

static int canonical_plan_depth_is_supported(
    const AcgcGxCanonicalDepthState* depth
) {
    return depth != NULL &&
        acgc_gx_canonical_depth_state_validate(depth) &&
        depth->z_compare_enable == ACGC_GX_CANONICAL_DEPTH_BOOLEAN_MAX;
}

static int canonical_plan_raster_dimension(
    uint32_t bits,
    uint32_t* dimension
) {
    const float value = float_from_bits(bits);

    if (dimension == NULL || !canonical_plan_binary32_is_finite(bits) ||
        !(value > 0.0f) ||
        !(value < (float)ACGC_GX_CANONICAL_RASTER_SCISSOR_LIMIT) ||
        floorf(value) != value) {
        return 0;
    }
    *dimension = (uint32_t)value;
    return *dimension != 0;
}

static int canonical_plan_raster_optional_words_are_supported(
    const AcgcGxCanonicalRasterState* raster
) {
    const int legacy_fixture_shape =
        raster->dither == 0 &&
        raster->field_mode == 0 &&
        raster->half_aspect_ratio == 0 &&
        raster->field_odd_mask == 0 &&
        raster->field_even_mask == 0;
    const int decomp_initialization_shape =
        raster->dither == 1 &&
        raster->field_mode == 1 &&
        raster->half_aspect_ratio == 0 &&
        raster->field_odd_mask == 1 &&
        raster->field_even_mask == 1;

    /* Line/point state is retained but cannot affect the one triangle draw.
     * Dither and field words are admitted only for the old zero fixture shape
     * or the exact GXNtsc480IntDf initialization shape. */
    return legacy_fixture_shape || decomp_initialization_shape;
}

static int canonical_plan_raster_is_supported_subset(
    const AcgcGxCanonicalRasterState* raster,
    uint32_t* cull_mode
) {
    uint32_t viewport_width;
    uint32_t viewport_height;

    if (raster == NULL || cull_mode == NULL ||
        !acgc_gx_canonical_raster_state_validate(raster)) {
        return 0;
    }
    if (raster->viewport_bits[0] != ACGC_METAL_FLOAT_ZERO ||
        raster->viewport_bits[1] != ACGC_METAL_FLOAT_ZERO ||
        raster->viewport_bits[4] != ACGC_METAL_FLOAT_ZERO ||
        raster->viewport_bits[5] != ACGC_METAL_FLOAT_ONE ||
        raster->scissor[0] != 0 || raster->scissor[1] != 0 ||
        !canonical_plan_raster_dimension(
            raster->viewport_bits[2], &viewport_width) ||
        !canonical_plan_raster_dimension(
            raster->viewport_bits[3], &viewport_height) ||
        raster->scissor[2] != viewport_width ||
        raster->scissor[3] != viewport_height ||
        raster->scissor_offset[0] != 0 || raster->scissor_offset[1] != 0 ||
        raster->clip_mode != ACGC_GX_CANONICAL_RASTER_CLIP_MODE_ENABLE ||
        raster->co_planar_enable != 0 ||
        raster->dst_alpha_enable != 0 || raster->dst_alpha != 0 ||
        !canonical_plan_raster_optional_words_are_supported(raster) ||
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

static int canonical_plan_raster_classify(
    const AcgcGxCanonicalRasterState* raster,
    uint32_t* cull_mode,
    AcgcMetalPacketConsumerCanonicalRasterDisposition* disposition
) {
    if (raster == NULL || cull_mode == NULL || disposition == NULL ||
        !acgc_gx_canonical_raster_state_validate(raster)) {
        return 0;
    }
    if (canonical_plan_raster_is_supported_subset(raster, cull_mode)) {
        *disposition =
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_DISPOSITION_MAPPED;
        return 1;
    }

    /* Preserve every valid Raster word in the typed output while keeping the
     * fixture state harmless and valid until a future raster consumer exists. */
    *cull_mode = ACGC_METAL_CULL_NONE;
    *disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_DISPOSITION_STAGED_UNRENDERED;
    return 1;
}

static int canonical_plan_fog_classify(
    const AcgcGxCanonicalFogState* fog,
    AcgcMetalPacketConsumerCanonicalFogDisposition* disposition
) {
    if (fog == NULL || disposition == NULL ||
        !acgc_gx_canonical_fog_state_validate(fog)) {
        return 0;
    }
    *disposition = fog->fog_type == ACGC_GX_CANONICAL_FOG_TYPE_NONE &&
            fog->range_adjust_enable == 0
        ? ACGC_METAL_PACKET_CONSUMER_CANONICAL_FOG_DISPOSITION_INACTIVE
        : ACGC_METAL_PACKET_CONSUMER_CANONICAL_FOG_DISPOSITION_STAGED_UNRENDERED;
    return 1;
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

static int canonical_plan_geometry_output_vertex_count(
    const AcgcAppleCanonicalPlanGeometry* geometry,
    uint32_t* output_vertex_count
) {
    if (geometry == NULL || output_vertex_count == NULL ||
        geometry->vertex_count > ACGC_APPLE_CANONICAL_PLAN_MAX_VERTEX_COUNT) {
        return 0;
    }
    if (geometry->primitive == ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES) {
        if (geometry->vertex_count < 3 ||
            (geometry->vertex_count % 3) != 0) {
            return 0;
        }
        *output_vertex_count = geometry->vertex_count;
    } else if (geometry->primitive ==
               ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS) {
        if (geometry->vertex_count < 4 ||
            (geometry->vertex_count % 4) != 0) {
            return 0;
        }
        *output_vertex_count = (geometry->vertex_count / 4) * 6;
    } else {
        return 0;
    }
    return *output_vertex_count > 0 &&
        *output_vertex_count <= ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
}

static int canonical_plan_geometry_is_supported(
    const AcgcAppleCanonicalPlanGeometry* geometry,
    const AcgcGxCanonicalTransformState* transform,
    uint32_t* matrix_slot,
    uint32_t* output_vertex_count
) {
    const uint32_t required_present_mask =
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0);
    const uint32_t normal_mask =
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM;
    const uint32_t texcoord0_mask =
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0;
    const uint32_t position_matrix_mask =
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_PNMTXIDX;
    const uint32_t allowed_present_mask =
        required_present_mask | normal_mask | texcoord0_mask |
        position_matrix_mask;
    uint32_t expected_component_mask =
        ACGC_APPLE_CANONICAL_PLAN_COMPONENT_POSITION |
        ACGC_APPLE_CANONICAL_PLAN_COMPONENT_COLOR0;
    uint32_t vertex;
    uint32_t coord;
    uint32_t slot;
    uint32_t selected_id;
    int has_explicit_position_matrix;

    if (geometry == NULL || transform == NULL || matrix_slot == NULL ||
        output_vertex_count == NULL ||
        !canonical_plan_geometry_output_vertex_count(
            geometry, output_vertex_count) ||
        geometry->vtxfmt >= ACGC_GX_CANONICAL_GEOMETRY_VTXFMT_COUNT ||
        (geometry->present_mask & ~allowed_present_mask) != 0 ||
        (geometry->present_mask & required_present_mask) !=
            required_present_mask ||
        !acgc_gx_canonical_transform_state_validate(transform)) {
        return 0;
    }
    if ((geometry->present_mask & normal_mask) != 0) {
        expected_component_mask |= ACGC_APPLE_CANONICAL_PLAN_COMPONENT_NORMAL;
    }
    if ((geometry->present_mask & texcoord0_mask) != 0) {
        expected_component_mask |=
            ACGC_APPLE_CANONICAL_PLAN_COMPONENT_TEXCOORD0;
    }
    if (geometry->component_mask != expected_component_mask) {
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
    for (vertex = 0; vertex < geometry->vertex_count; vertex++) {
        const AcgcAppleCanonicalPlanVertex* source = &geometry->vertices[vertex];

        if (source->present_mask != geometry->present_mask ||
            source->component_mask != expected_component_mask ||
            source->position_matrix_id != selected_id ||
            !canonical_plan_words_are_zero(source->binormal, 3) ||
            !canonical_plan_words_are_zero(source->tangent, 3) ||
            source->color_rgba8[1] != 0) {
            return 0;
        }
        for (coord = 0; coord < ACGC_GX_CANONICAL_TEXGEN_COUNT; coord++) {
            /* These are effective logical selectors, not raw attributes.
             * A source Texgen may retain one even when TEXnMTXIDX is absent;
             * validate it here and let the later Texgen gate decide whether
             * that state is renderable. */
            if (!canonical_plan_ordinary_texture_matrix_id_is_valid(
                    source->texture_matrix_id[coord])) {
                return 0;
            }
        }
        for (coord = 0; coord < 3; coord++) {
            if (!canonical_plan_binary32_is_finite(source->position[coord])) {
                return 0;
            }
        }
        if ((geometry->present_mask & normal_mask) != 0) {
            for (coord = 0; coord < 3; coord++) {
                if (!canonical_plan_binary32_is_finite(source->normal[coord])) {
                    return 0;
                }
            }
        } else if (!canonical_plan_words_are_zero(source->normal, 3)) {
            return 0;
        }
        if ((geometry->present_mask & texcoord0_mask) != 0) {
            for (coord = 0; coord < 2; coord++) {
                if (!canonical_plan_binary32_is_finite(
                        source->texcoord[0][coord])) {
                    return 0;
                }
            }
        } else if (!canonical_plan_words_are_zero(source->texcoord[0], 2)) {
            return 0;
        }
        for (coord = 1; coord < ACGC_GX_CANONICAL_TEXGEN_COUNT; coord++) {
            if (!canonical_plan_words_are_zero(source->texcoord[coord], 2)) {
                return 0;
            }
        }
    }
    for (vertex = geometry->vertex_count;
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

static int canonical_plan_texture_resource_stage_is_supported(
    const AcgcGxCanonicalTextureState* texture,
    const AcgcGxCanonicalDynamicState* dynamic,
    const AcgcMetalPacketConsumerCanonicalResourceStage* resource_stage
);

static AcgcMetalPacketConsumerStatus canonical_plan_sections_status(
    const AcgcAppleCanonicalPlan* plan,
    const AcgcMetalPacketConsumerCanonicalResourceStage* resource_stage,
    uint32_t* matrix_slot,
    uint32_t* output_vertex_count,
    uint32_t* channel_mode,
    uint32_t* source_factor,
    uint32_t* destination_factor,
    uint32_t* blend_operation,
    uint32_t* depth_compare,
    uint32_t* cull_mode,
    AcgcMetalPacketConsumerCanonicalTevDisposition* tev_disposition,
    AcgcMetalPacketConsumerCanonicalBlendDisposition* blend_disposition,
    AcgcMetalPacketConsumerCanonicalAlphaDisposition* alpha_disposition,
    AcgcMetalPacketConsumerCanonicalRasterDisposition* raster_disposition,
    AcgcMetalPacketConsumerCanonicalFogDisposition* fog_disposition
) {
    if (plan == NULL || matrix_slot == NULL || output_vertex_count == NULL ||
        channel_mode == NULL || source_factor == NULL ||
        destination_factor == NULL || blend_operation == NULL ||
        depth_compare == NULL || cull_mode == NULL || tev_disposition == NULL ||
        blend_disposition == NULL || alpha_disposition == NULL ||
        raster_disposition == NULL || fog_disposition == NULL) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT;
    }
    /*
     * These are direct normalized-plan dependency predicates: Geometry's
     * selector/Transform knownness is checked here, TEV must be a structurally
     * valid value whose Texture/Texgen/Channel references
     * have passed the cross-section dependency check, channel 0 must be either
     * the bounded disabled vertex-color mode or the exact supported AF_NONE
     * lighting mode, and resource sections are either inactive or matched to
     * their value-owned staged resources.
     */
    if (!acgc_gx_canonical_transform_state_validate(&plan->transform) ||
        (plan->transform.known_mask &
            ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_KNOWN_MASK) == 0) {
        return ACGC_METAL_PACKET_CONSUMER_CANONICAL_TRANSFORM_UNSUPPORTED;
    }
    if (!canonical_plan_geometry_is_supported(
            &plan->geometry, &plan->transform, matrix_slot,
            output_vertex_count)) {
        return ACGC_METAL_PACKET_CONSUMER_CANONICAL_GEOMETRY_UNSUPPORTED;
    }
    if (!canonical_plan_channels_are_supported(
            &plan->channels, channel_mode)) {
        return ACGC_METAL_PACKET_CONSUMER_CANONICAL_CHANNELS_UNSUPPORTED;
    }
    if (!canonical_plan_texgens_are_supported(
            &plan->geometry, &plan->texgens)) {
        return ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEXGENS_UNSUPPORTED;
    }
    if (!acgc_gx_canonical_texture_state_validate(&plan->texture)) {
        return ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEXTURE_UNSUPPORTED;
    }
    if (!acgc_gx_canonical_dynamic_state_validate(&plan->dynamic)) {
        return ACGC_METAL_PACKET_CONSUMER_CANONICAL_DYNAMIC_UNSUPPORTED;
    }
    if (canonical_plan_texture_is_inactive(&plan->texture)) {
        if (!canonical_plan_dynamic_is_inactive(&plan->dynamic)) {
            return ACGC_METAL_PACKET_CONSUMER_CANONICAL_DYNAMIC_UNSUPPORTED;
        }
    } else if (!acgc_gx_canonical_texture_dynamic_validate(
            &plan->texture, &plan->dynamic)) {
        return
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DEPENDENCY_UNSUPPORTED;
    }
    if (!canonical_plan_texture_resource_stage_is_supported(
            &plan->texture, &plan->dynamic, resource_stage)) {
        return
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DEPENDENCY_UNSUPPORTED;
    }
    if (!canonical_plan_tev_dependencies_are_supported(
            &plan->tev,
            &plan->texture,
            &plan->texgens,
            &plan->channels)) {
        return ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_UNSUPPORTED;
    }
    *tev_disposition = canonical_plan_tev_is_vertex_color_passthrough(
            &plan->tev)
        ? ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_VERTEX_COLOR_PASSTHROUGH
        : ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_STAGED_UNRENDERED;
    if (*channel_mode == ACGC_CANONICAL_CHANNEL_MODE_AF_NONE) {
        if (!canonical_plan_lighting_is_supported(
                &plan->geometry,
                &plan->transform,
                &plan->channels,
                &plan->lighting,
                *matrix_slot)) {
            return ACGC_METAL_PACKET_CONSUMER_CANONICAL_LIGHTING_UNSUPPORTED;
        }
    } else if (!canonical_plan_lighting_is_inactive(&plan->lighting)) {
        return ACGC_METAL_PACKET_CONSUMER_CANONICAL_LIGHTING_UNSUPPORTED;
    }
    if (!acgc_gx_canonical_blend_state_validate(&plan->blend)) {
        return ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_UNSUPPORTED;
    }
    if (canonical_plan_blend_is_mapped(
            &plan->blend,
            source_factor,
            destination_factor,
            blend_operation)) {
        *blend_disposition =
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_DISPOSITION_MAPPED;
    } else {
        /* The canonical words remain available for a later renderer, but the
         * present fixture state must stay valid and harmless if a caller
         * accidentally inspects it before the sink rejects the disposition. */
        *source_factor = ACGC_METAL_BLEND_ZERO;
        *destination_factor = ACGC_METAL_BLEND_ZERO;
        *blend_operation = ACGC_METAL_BLEND_ADD;
        *blend_disposition =
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_DISPOSITION_STAGED_UNRENDERED;
    }
    if (!canonical_plan_alpha_classify(&plan->alpha, alpha_disposition)) {
        return ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_UNSUPPORTED;
    }
    if (!canonical_plan_depth_is_supported(&plan->depth) ||
        !canonical_plan_depth_compare_to_metal(
            plan->depth.z_compare_func, depth_compare)) {
        return ACGC_METAL_PACKET_CONSUMER_CANONICAL_DEPTH_UNSUPPORTED;
    }
    if (!canonical_plan_raster_classify(
            &plan->raster, cull_mode, raster_disposition)) {
        return ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_UNSUPPORTED;
    }
    if (!canonical_plan_fog_classify(&plan->fog, fog_disposition)) {
        return ACGC_METAL_PACKET_CONSUMER_CANONICAL_FOG_UNSUPPORTED;
    }
    if (!canonical_plan_indirect_is_inactive(&plan->indirect)) {
        return ACGC_METAL_PACKET_CONSUMER_CANONICAL_INDIRECT_UNSUPPORTED;
    }
    return ACGC_METAL_PACKET_CONSUMER_OK;
}

static uint64_t canonical_resource_generation(
    uint32_t generation_lo,
    uint32_t generation_hi
) {
    return (uint64_t)generation_lo |
        ((uint64_t)generation_hi << 32);
}

static int canonical_resource_image_matches(
    const AcgcGxCanonicalTextureRecord* texture_record,
    const AcgcGxCanonicalDynamicRecord* dynamic_record,
    const PCGXTextureBorrowedResource* lease
) {
    uint64_t generation;

    if (texture_record == NULL || dynamic_record == NULL || lease == NULL ||
        (texture_record->flags &
            ACGC_GX_CANONICAL_TEXTURE_FLAG_RESOURCE_REQUIRED) == 0 ||
        dynamic_record->kind != ACGC_GX_CANONICAL_DYNAMIC_KIND_IMAGE ||
        dynamic_record->owner_slot >= PC_GX_TEXTURE_RAW_MAP_COUNT ||
        (dynamic_record->byte_flags &
            (ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE |
             ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED)) !=
            (ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE |
             ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED) ||
        lease->bytes == NULL || lease->byte_size == 0) {
        return 0;
    }
    generation = canonical_resource_generation(
        texture_record->image_generation_lo,
        texture_record->image_generation_hi
    );
    return texture_record->image_resource_id == dynamic_record->resource_id &&
        texture_record->image_owner_epoch == dynamic_record->owner_epoch &&
        generation == canonical_resource_generation(
            dynamic_record->generation_lo,
            dynamic_record->generation_hi
        ) &&
        texture_record->image_owner_epoch == lease->owner_epoch &&
        generation == lease->generation &&
        texture_record->image_format == dynamic_record->format &&
        texture_record->image_format == lease->format &&
        texture_record->image_byte_size == dynamic_record->byte_size &&
        texture_record->image_byte_size == lease->byte_size &&
        texture_record->image_byte_order == dynamic_record->byte_order &&
        texture_record->image_byte_order == lease->byte_order &&
        texture_record->image_source_kind == dynamic_record->source_kind &&
        texture_record->image_source_kind == lease->source_kind;
}

static int canonical_resource_tlut_matches(
    const AcgcGxCanonicalTextureRecord* texture_record,
    const AcgcGxCanonicalDynamicRecord* dynamic_record,
    const PCGXTextureBorrowedResource* lease,
    uint32_t slot
) {
    uint64_t generation;

    if (texture_record == NULL || dynamic_record == NULL || lease == NULL ||
        slot >= PC_GX_TEXTURE_RAW_TLUT_COUNT ||
        texture_record->tlut_name != slot ||
        texture_record->tlut_resource_id == 0 ||
        dynamic_record->kind != ACGC_GX_CANONICAL_DYNAMIC_KIND_TLUT ||
        dynamic_record->owner_slot != slot ||
        (dynamic_record->byte_flags &
            (ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE |
             ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED)) !=
            (ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE |
             ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED) ||
        lease->bytes == NULL || lease->byte_size == 0) {
        return 0;
    }
    generation = canonical_resource_generation(
        texture_record->tlut_generation_lo,
        texture_record->tlut_generation_hi
    );
    return texture_record->tlut_resource_id == dynamic_record->resource_id &&
        texture_record->tlut_owner_epoch == dynamic_record->owner_epoch &&
        generation == canonical_resource_generation(
            dynamic_record->generation_lo,
            dynamic_record->generation_hi
        ) &&
        texture_record->tlut_owner_epoch == lease->owner_epoch &&
        generation == lease->generation &&
        texture_record->tlut_format == dynamic_record->format &&
        texture_record->tlut_format == lease->format &&
        texture_record->tlut_entry_count == dynamic_record->element_count &&
        texture_record->tlut_entry_count == lease->element_count &&
        texture_record->tlut_byte_size == dynamic_record->byte_size &&
        texture_record->tlut_byte_size == lease->byte_size &&
        texture_record->tlut_byte_order == dynamic_record->byte_order &&
        texture_record->tlut_byte_order == lease->byte_order &&
        texture_record->tlut_source_kind == dynamic_record->source_kind &&
        texture_record->tlut_source_kind == lease->source_kind;
}

static int canonical_resource_stage_input_ranges_are_valid(
    const AcgcGxCanonicalTextureState* texture,
    const AcgcGxCanonicalDynamicState* dynamic,
    const PCGXTextureDynamicLease* lease,
    const AcgcMetalPacketConsumerCanonicalResourceStage* stage
) {
    uintptr_t texture_begin;
    uintptr_t texture_end;
    uintptr_t dynamic_begin;
    uintptr_t dynamic_end;
    uintptr_t lease_begin;
    uintptr_t lease_end;
    uintptr_t stage_begin;
    uintptr_t stage_end;

    if (texture == NULL || dynamic == NULL || lease == NULL || stage == NULL ||
        ((uintptr_t)texture %
            (uintptr_t)_Alignof(AcgcGxCanonicalTextureState)) != 0 ||
        ((uintptr_t)dynamic %
            (uintptr_t)_Alignof(AcgcGxCanonicalDynamicState)) != 0 ||
        ((uintptr_t)lease % (uintptr_t)_Alignof(PCGXTextureDynamicLease)) != 0 ||
        ((uintptr_t)stage %
            (uintptr_t)_Alignof(
                AcgcMetalPacketConsumerCanonicalResourceStage)) != 0 ||
        !canonical_plan_pointer_range(
            texture, sizeof(*texture), &texture_begin, &texture_end
        ) || !canonical_plan_pointer_range(
            dynamic, sizeof(*dynamic), &dynamic_begin, &dynamic_end
        ) || !canonical_plan_pointer_range(
            lease, sizeof(*lease), &lease_begin, &lease_end
        ) || !canonical_plan_pointer_range(
            stage, sizeof(*stage), &stage_begin, &stage_end
        )) {
        return 0;
    }
    return !canonical_plan_ranges_overlap(
            texture_begin, texture_end, dynamic_begin, dynamic_end
        ) &&
        !canonical_plan_ranges_overlap(
            texture_begin, texture_end, lease_begin, lease_end
        ) &&
        !canonical_plan_ranges_overlap(
            texture_begin, texture_end, stage_begin, stage_end
        ) &&
        !canonical_plan_ranges_overlap(
            dynamic_begin, dynamic_end, lease_begin, lease_end
        ) &&
        !canonical_plan_ranges_overlap(
            dynamic_begin, dynamic_end, stage_begin, stage_end
        ) &&
        !canonical_plan_ranges_overlap(
            lease_begin, lease_end, stage_begin, stage_end
        );
}

int acgc_metal_packet_consumer_stage_canonical_resources(
    uint64_t attempt_id,
    const AcgcGxCanonicalTextureState* texture,
    const AcgcGxCanonicalDynamicState* dynamic,
    const PCGXTextureDynamicLease* lease,
    AcgcMetalPacketConsumerCanonicalResourceStage* stage
) {
    AcgcMetalPacketConsumerCanonicalResourceStage candidate;
    uint32_t map;
    uint32_t slot;
    uint32_t required_map_mask;
    uint32_t required_tlut_mask;

    if (stage == NULL ||
        !canonical_resource_stage_input_ranges_are_valid(
            texture, dynamic, lease, stage
        )) {
        return 0;
    }
    memset(&candidate, 0, sizeof(candidate));
    candidate.attempt_id = attempt_id;
    if (attempt_id == 0 ||
        !acgc_gx_canonical_texture_state_validate(texture) ||
        !acgc_gx_canonical_dynamic_state_validate(dynamic) ||
        !acgc_gx_canonical_texture_dynamic_validate(texture, dynamic) ||
        dynamic->header.owner_epoch != lease->owner_epoch ||
        lease->reserved0 != 0 || lease->reserved1 != 0) {
        goto failure;
    }

    required_map_mask = texture->header.required_map_mask;
    required_tlut_mask = dynamic->header.required_tlut_mask;
    if (required_map_mask != dynamic->header.required_image_mask ||
        (lease->image_mask & required_map_mask) != required_map_mask ||
        ((uint32_t)lease->tlut_mask & required_tlut_mask) !=
            required_tlut_mask) {
        goto failure;
    }

    for (slot = 0; slot < PC_GX_TEXTURE_RAW_TLUT_COUNT; slot++) {
        const uint32_t slot_mask = UINT32_C(1) << slot;
        const AcgcGxCanonicalDynamicRecord* dynamic_record;
        const PCGXTextureBorrowedResource* borrowed;
        const AcgcGxCanonicalTextureRecord* texture_record = NULL;
        uint32_t map_for_slot;

        if ((required_tlut_mask & slot_mask) == 0) {
            continue;
        }
        dynamic_record = &dynamic->records[
            ACGC_GX_CANONICAL_DYNAMIC_IMAGE_MAP_COUNT + slot];
        borrowed = &lease->tluts[slot];
        for (map_for_slot = 0;
             map_for_slot < ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY;
             map_for_slot++) {
            const AcgcGxCanonicalTextureRecord* candidate =
                &texture->records[map_for_slot];
            if ((candidate->flags &
                    ACGC_GX_CANONICAL_TEXTURE_FLAG_RESOURCE_REQUIRED) != 0 &&
                candidate->tlut_name == slot) {
                texture_record = candidate;
                break;
            }
        }
        if (!canonical_resource_tlut_matches(
                texture_record, dynamic_record, borrowed, slot) ||
            borrowed->byte_size >
                ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_TLUT_BYTES) {
            goto failure;
        }
        memcpy(
            candidate.tlut_bytes[slot],
            borrowed->bytes,
            borrowed->byte_size
        );
        candidate.tlut_byte_sizes[slot] = borrowed->byte_size;
        candidate.tlut_mask |= slot_mask;
    }

    for (map = 0; map < PC_GX_TEXTURE_RAW_MAP_COUNT; map++) {
        const uint32_t map_mask = UINT32_C(1) << map;
        const AcgcGxCanonicalTextureRecord* texture_record;
        const AcgcGxCanonicalDynamicRecord* dynamic_record;
        const PCGXTextureBorrowedResource* borrowed;
        AcgcRendererFixtureTextureDescription* description;
        AcgcRendererFixtureSamplerDescription* sampler;
        AcgcRendererFixtureSamplerState sampler_state;
        uint32_t source_byte_size;
        uint32_t decoded_byte_size;
        uint64_t texel_count;

        if ((required_map_mask & map_mask) == 0) {
            continue;
        }
        texture_record = &texture->records[map];
        dynamic_record = &dynamic->records[map];
        borrowed = &lease->images[map];
        if (!canonical_resource_image_matches(
                texture_record, dynamic_record, borrowed) ||
            (texture_record->flags & ACGC_GX_CANONICAL_TEXTURE_FLAG_MIPMAP) !=
                0 ||
            borrowed->byte_size >
                ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_IMAGE_BYTES ||
            texture_record->width == 0 || texture_record->height == 0 ||
            borrowed->element_count != 0) {
            goto failure;
        }
        source_byte_size = acgc_renderer_fixture_texture_bytes(
            texture_record->width,
            texture_record->height,
            texture_record->image_format
        );
        texel_count = (uint64_t)texture_record->width *
            (uint64_t)texture_record->height;
        if (source_byte_size == 0 || borrowed->byte_size != source_byte_size ||
            borrowed->byte_size >
                ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_IMAGE_BYTES ||
            texel_count >
                ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DECODED_RGBA_BYTES /
                    4) {
            goto failure;
        }
        decoded_byte_size = (uint32_t)(texel_count * 4);
        if ((texture_record->flags &
                ACGC_GX_CANONICAL_TEXTURE_FLAG_INDEXED) != 0 &&
            (texture_record->tlut_name >= PC_GX_TEXTURE_RAW_TLUT_COUNT ||
             (candidate.tlut_mask &
                (UINT32_C(1) << texture_record->tlut_name)) == 0)) {
            goto failure;
        }

        memcpy(
            candidate.image_bytes[map],
            borrowed->bytes,
            borrowed->byte_size
        );
        description = &candidate.descriptions[map];
        description->version = ACGC_RENDERER_FIXTURE_VERSION;
        description->width = texture_record->width;
        description->height = texture_record->height;
        description->format = texture_record->image_format;
        description->data_byte_order = texture_record->image_byte_order;
        description->data_size = borrowed->byte_size;
        description->tlut_format = texture_record->tlut_format;
        description->tlut_entries = texture_record->tlut_entry_count;
        description->tlut_data_size = texture_record->tlut_byte_size;
        description->tlut_byte_order = texture_record->tlut_byte_order;
        if (!acgc_renderer_fixture_decode_texture(
                description,
                candidate.image_bytes[map],
                description->tlut_entries != 0
                    ? candidate.tlut_bytes[texture_record->tlut_name]
                    : NULL,
                candidate.decoded_rgba[map],
                ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DECODED_RGBA_BYTES
            )) {
            goto failure;
        }
        sampler = &candidate.samplers[map];
        sampler->version = ACGC_RENDERER_FIXTURE_VERSION;
        sampler->wrap_s = texture_record->wrap_s;
        sampler->wrap_t = texture_record->wrap_t;
        sampler->min_filter = texture_record->min_filter;
        sampler->mag_filter = texture_record->mag_filter;
        sampler->filtering_enabled = 1;
        if (!acgc_renderer_fixture_resolve_sampler(sampler, &sampler_state)) {
            goto failure;
        }
        candidate.image_byte_sizes[map] = borrowed->byte_size;
        candidate.decoded_rgba_byte_sizes[map] = decoded_byte_size;
        candidate.image_mask |= map_mask;
        candidate.decoded_image_mask |= map_mask;
    }

    if (candidate.image_mask != required_map_mask ||
        candidate.tlut_mask != required_tlut_mask ||
        candidate.decoded_image_mask != required_map_mask) {
        goto failure;
    }
    candidate.valid = 1;
    *stage = candidate;
    return 1;

failure:
    memset(stage, 0, sizeof(*stage));
    return 0;
}

static int canonical_resource_stage_is_empty(
    const AcgcMetalPacketConsumerCanonicalResourceStage* resource_stage
) {
    return resource_stage != NULL &&
        resource_stage->image_mask == 0 &&
        resource_stage->tlut_mask == 0 &&
        resource_stage->decoded_image_mask == 0 &&
        canonical_plan_bytes_are_zero(
            resource_stage->image_byte_sizes,
            sizeof(resource_stage->image_byte_sizes)) &&
        canonical_plan_bytes_are_zero(
            resource_stage->tlut_byte_sizes,
            sizeof(resource_stage->tlut_byte_sizes)) &&
        canonical_plan_bytes_are_zero(
            resource_stage->decoded_rgba_byte_sizes,
            sizeof(resource_stage->decoded_rgba_byte_sizes)) &&
        canonical_plan_bytes_are_zero(
            resource_stage->descriptions,
            sizeof(resource_stage->descriptions)) &&
        canonical_plan_bytes_are_zero(
            resource_stage->samplers,
            sizeof(resource_stage->samplers)) &&
        canonical_plan_bytes_are_zero(
            resource_stage->image_bytes,
            sizeof(resource_stage->image_bytes)) &&
        canonical_plan_bytes_are_zero(
            resource_stage->tlut_bytes,
            sizeof(resource_stage->tlut_bytes)) &&
        canonical_plan_bytes_are_zero(
            resource_stage->decoded_rgba,
            sizeof(resource_stage->decoded_rgba));
}

static int canonical_resource_stage_description_is_source_faithful(
    const AcgcGxCanonicalTextureRecord* texture_record,
    const AcgcMetalPacketConsumerCanonicalResourceStage* resource_stage,
    uint32_t map
) {
    AcgcRendererFixtureTextureDescription expected_description;
    AcgcRendererFixtureSamplerDescription expected_sampler;
    AcgcRendererFixtureSamplerState sampler_state;

    if (texture_record == NULL || resource_stage == NULL || map >=
            PC_GX_TEXTURE_RAW_MAP_COUNT) {
        return 0;
    }
    memset(&expected_description, 0, sizeof(expected_description));
    expected_description.version = ACGC_RENDERER_FIXTURE_VERSION;
    expected_description.width = texture_record->width;
    expected_description.height = texture_record->height;
    expected_description.format = texture_record->image_format;
    expected_description.data_byte_order = texture_record->image_byte_order;
    expected_description.data_size = texture_record->image_byte_size;
    expected_description.tlut_format = texture_record->tlut_format;
    expected_description.tlut_entries = texture_record->tlut_entry_count;
    expected_description.tlut_data_size = texture_record->tlut_byte_size;
    expected_description.tlut_byte_order = texture_record->tlut_byte_order;
    if (memcmp(
            &resource_stage->descriptions[map],
            &expected_description,
            sizeof(expected_description)) != 0) {
        return 0;
    }

    memset(&expected_sampler, 0, sizeof(expected_sampler));
    expected_sampler.version = ACGC_RENDERER_FIXTURE_VERSION;
    expected_sampler.wrap_s = texture_record->wrap_s;
    expected_sampler.wrap_t = texture_record->wrap_t;
    expected_sampler.min_filter = texture_record->min_filter;
    expected_sampler.mag_filter = texture_record->mag_filter;
    expected_sampler.filtering_enabled = 1;
    return memcmp(
            &resource_stage->samplers[map],
            &expected_sampler,
            sizeof(expected_sampler)) == 0 &&
        acgc_renderer_fixture_resolve_sampler(
            &resource_stage->samplers[map], &sampler_state);
}

static int canonical_plan_texture_resource_stage_is_supported(
    const AcgcGxCanonicalTextureState* texture,
    const AcgcGxCanonicalDynamicState* dynamic,
    const AcgcMetalPacketConsumerCanonicalResourceStage* resource_stage
) {
    uint32_t required_map_mask;
    uint32_t required_tlut_mask;
    uint32_t map;
    uint32_t slot;

    if (texture == NULL || dynamic == NULL || resource_stage == NULL ||
        resource_stage->valid != 1 || resource_stage->attempt_id == 0 ||
        !acgc_gx_canonical_texture_state_validate(texture) ||
        !acgc_gx_canonical_dynamic_state_validate(dynamic)) {
        return 0;
    }
    if (canonical_plan_texture_is_inactive(texture)) {
        return canonical_plan_dynamic_is_inactive(dynamic) &&
            canonical_resource_stage_is_empty(resource_stage);
    }
    if (!acgc_gx_canonical_texture_dynamic_validate(texture, dynamic)) {
        return 0;
    }

    required_map_mask = texture->header.required_map_mask;
    required_tlut_mask = dynamic->header.required_tlut_mask;
    if (required_map_mask == 0 ||
        required_map_mask != dynamic->header.required_image_mask ||
        resource_stage->image_mask != required_map_mask ||
        resource_stage->tlut_mask != required_tlut_mask ||
        resource_stage->decoded_image_mask != required_map_mask) {
        return 0;
    }

    for (slot = 0; slot < PC_GX_TEXTURE_RAW_TLUT_COUNT; slot++) {
        const uint32_t slot_mask = UINT32_C(1) << slot;
        const AcgcGxCanonicalDynamicRecord* dynamic_record;
        const AcgcGxCanonicalTextureRecord* texture_record = NULL;
        uint32_t map_for_slot;

        if ((required_tlut_mask & slot_mask) == 0) {
            if (resource_stage->tlut_byte_sizes[slot] != 0 ||
                !canonical_plan_bytes_are_zero(
                    resource_stage->tlut_bytes[slot],
                    sizeof(resource_stage->tlut_bytes[slot]))) {
                return 0;
            }
            continue;
        }
        dynamic_record = &dynamic->records[
            ACGC_GX_CANONICAL_DYNAMIC_IMAGE_MAP_COUNT + slot];
        for (map_for_slot = 0;
             map_for_slot < ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY;
             map_for_slot++) {
            const AcgcGxCanonicalTextureRecord* candidate =
                &texture->records[map_for_slot];

            if ((candidate->flags &
                    (ACGC_GX_CANONICAL_TEXTURE_FLAG_RESOURCE_REQUIRED |
                     ACGC_GX_CANONICAL_TEXTURE_FLAG_INDEXED)) ==
                    (ACGC_GX_CANONICAL_TEXTURE_FLAG_RESOURCE_REQUIRED |
                     ACGC_GX_CANONICAL_TEXTURE_FLAG_INDEXED) &&
                candidate->tlut_name == slot) {
                if (texture_record != NULL) {
                    return 0;
                }
                texture_record = candidate;
            }
        }
        if (texture_record == NULL ||
            dynamic_record->kind != ACGC_GX_CANONICAL_DYNAMIC_KIND_TLUT ||
            dynamic_record->owner_slot != slot ||
            texture_record->tlut_byte_size == 0 ||
            dynamic_record->byte_size != texture_record->tlut_byte_size ||
            resource_stage->tlut_byte_sizes[slot] !=
                texture_record->tlut_byte_size ||
            resource_stage->tlut_byte_sizes[slot] >
                ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_TLUT_BYTES ||
            !canonical_plan_bytes_are_zero(
                &resource_stage->tlut_bytes[slot][
                    resource_stage->tlut_byte_sizes[slot]],
                ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_TLUT_BYTES -
                    resource_stage->tlut_byte_sizes[slot])) {
            return 0;
        }
    }

    for (map = 0; map < PC_GX_TEXTURE_RAW_MAP_COUNT; map++) {
        const uint32_t map_mask = UINT32_C(1) << map;
        const AcgcGxCanonicalTextureRecord* texture_record =
            &texture->records[map];
        const AcgcGxCanonicalDynamicRecord* dynamic_record =
            &dynamic->records[map];
        uint32_t source_byte_size;
        uint64_t texel_count;
        uint32_t decoded_byte_size;

        if ((required_map_mask & map_mask) == 0) {
            if (resource_stage->image_byte_sizes[map] != 0 ||
                resource_stage->decoded_rgba_byte_sizes[map] != 0 ||
                !canonical_plan_bytes_are_zero(
                    resource_stage->image_bytes[map],
                    sizeof(resource_stage->image_bytes[map])) ||
                !canonical_plan_bytes_are_zero(
                    resource_stage->decoded_rgba[map],
                    sizeof(resource_stage->decoded_rgba[map])) ||
                !canonical_plan_bytes_are_zero(
                    &resource_stage->descriptions[map],
                    sizeof(resource_stage->descriptions[map])) ||
                !canonical_plan_bytes_are_zero(
                    &resource_stage->samplers[map],
                    sizeof(resource_stage->samplers[map]))) {
                return 0;
            }
            continue;
        }
        source_byte_size = acgc_renderer_fixture_texture_bytes(
            texture_record->width,
            texture_record->height,
            texture_record->image_format
        );
        texel_count = (uint64_t)texture_record->width *
            (uint64_t)texture_record->height;
        if ((texture_record->flags &
                ACGC_GX_CANONICAL_TEXTURE_FLAG_RESOURCE_REQUIRED) == 0 ||
            dynamic_record->kind != ACGC_GX_CANONICAL_DYNAMIC_KIND_IMAGE ||
            dynamic_record->owner_slot != map || source_byte_size == 0 ||
            source_byte_size != texture_record->image_byte_size ||
            source_byte_size != dynamic_record->byte_size ||
            source_byte_size >
                ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_IMAGE_BYTES ||
            texel_count >
                ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DECODED_RGBA_BYTES /
                    4 ||
            resource_stage->image_byte_sizes[map] != source_byte_size ||
            resource_stage->decoded_rgba_byte_sizes[map] != texel_count * 4 ||
            !canonical_resource_stage_description_is_source_faithful(
                texture_record, resource_stage, map) ||
            !canonical_plan_bytes_are_zero(
                &resource_stage->image_bytes[map][source_byte_size],
                ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_IMAGE_BYTES -
                    source_byte_size)) {
            return 0;
        }
        decoded_byte_size = (uint32_t)(texel_count * 4);
        if (!canonical_plan_bytes_are_zero(
                &resource_stage->decoded_rgba[map][decoded_byte_size],
                ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DECODED_RGBA_BYTES -
                    decoded_byte_size)) {
            return 0;
        }
        {
            uint8_t decoded[
                ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DECODED_RGBA_BYTES];

            memset(decoded, 0, sizeof(decoded));
            if (!acgc_renderer_fixture_decode_texture(
                    &resource_stage->descriptions[map],
                    resource_stage->image_bytes[map],
                    resource_stage->descriptions[map].tlut_entries != 0
                        ? resource_stage->tlut_bytes[
                            texture_record->tlut_name]
                        : NULL,
                    decoded,
                    sizeof(decoded)) ||
                memcmp(
                    decoded,
                    resource_stage->decoded_rgba[map],
                    decoded_byte_size) != 0) {
                return 0;
            }
        }
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

static int canonical_plan_materialize_renderer_color(
    const AcgcAppleCanonicalPlan* plan,
    uint32_t matrix_slot,
    uint32_t channel_mode,
    const AcgcAppleCanonicalPlanVertex* source,
    uint32_t* output_color
) {
    const AcgcGxCanonicalChannelRecord* channel;
    float normal[3];
    float accumulation[3];
    uint8_t color[3];
    uint8_t alpha;
    uint32_t component;
    uint32_t slot;

    if (plan == NULL || source == NULL || output_color == NULL) {
        return 0;
    }
    if (channel_mode == ACGC_CANONICAL_CHANNEL_MODE_VERTEX) {
        *output_color = canonical_plan_renderer_color(source->color_rgba8[0]);
        return 1;
    }
    if (channel_mode != ACGC_CANONICAL_CHANNEL_MODE_AF_NONE ||
        !canonical_plan_transform_normal(
            &plan->transform,
            matrix_slot,
            source->normal,
            normal)) {
        return 0;
    }

    channel = &plan->channels.records[0];
    for (component = 0; component < 3; component++) {
        accumulation[component] = canonical_plan_rgba8_component(
            channel->ambient_rgba8,
            component * 8
        );
        if (!isfinite(accumulation[component])) {
            return 0;
        }
    }
    for (slot = 0;
         slot < ACGC_GX_CANONICAL_LIGHTING_SLOT_COUNT;
         slot++) {
        const AcgcGxCanonicalLightingRecord* light_record;
        float light[3];
        float dot;
        float diffuse;

        if ((channel->color.light_mask & (UINT32_C(1) << slot)) == 0) {
            continue;
        }
        light_record = &plan->lighting.records[slot];
        if (!canonical_plan_normalize_binary32_vector(
                light_record->position,
                light)) {
            return 0;
        }
        dot = normal[0] * light[0] +
            normal[1] * light[1] +
            normal[2] * light[2];
        if (!isfinite(dot)) {
            return 0;
        }
        diffuse = canonical_plan_clamp_unit(dot);
        for (component = 0; component < 3; component++) {
            float light_color = canonical_plan_rgba8_component(
                light_record->color_rgba8,
                component * 8
            );
            float contribution = diffuse * light_color;

            if (!isfinite(contribution)) {
                return 0;
            }
            accumulation[component] += contribution;
            if (!isfinite(accumulation[component])) {
                return 0;
            }
        }
    }
    for (component = 0; component < 3; component++) {
        float material = canonical_plan_rgba8_component(
            channel->material_rgba8,
            component * 8
        );
        float materialized = material *
            canonical_plan_clamp_unit(accumulation[component]);

        if (!isfinite(material) || !isfinite(materialized) ||
            !canonical_plan_quantize_unit(materialized, &color[component])) {
            return 0;
        }
    }
    alpha = (uint8_t)(source->color_rgba8[0] >> 24);
    *output_color = canonical_plan_renderer_color(
        (uint32_t)color[0] |
        ((uint32_t)color[1] << 8) |
        ((uint32_t)color[2] << 16) |
        ((uint32_t)alpha << 24)
    );
    return 1;
}

static int canonical_plan_copy_renderer_vertex(
    const AcgcAppleCanonicalPlan* plan,
    uint32_t matrix_slot,
    uint32_t channel_mode,
    const AcgcAppleCanonicalPlanVertex* source,
    AcgcRendererVertex* destination
) {
    uint32_t color;

    if (plan == NULL || source == NULL || destination == NULL ||
        !canonical_plan_materialize_renderer_color(
            plan,
            matrix_slot,
            channel_mode,
            source,
            &color)) {
        return 0;
    }
    destination->position_x = source->position[0];
    destination->position_y = source->position[1];
    destination->position_z = source->position[2];
    destination->color_rgba8 = color;
    return 1;
}

AcgcMetalPacketConsumerStatus acgc_metal_packet_consumer_prepare_canonical_plan(
    const AcgcAppleCanonicalPlan* plan,
    const AcgcMetalPacketConsumerCanonicalResourceStage* resource_stage,
    AcgcMetalPacketConsumerOutput* output
) {
    AcgcMetalPacketConsumerOutput candidate;
    AcgcMetalFixedTransform transform;
    uint32_t matrix_slot;
    uint32_t channel_mode;
    uint32_t source_factor;
    uint32_t destination_factor;
    uint32_t blend_operation;
    uint32_t depth_compare;
    uint32_t cull_mode;
    uint32_t vertex;
    uint32_t corner;
    uint32_t output_vertex;
    uint32_t output_vertex_count;
    AcgcMetalPacketConsumerCanonicalTevDisposition tev_disposition;
    AcgcMetalPacketConsumerCanonicalBlendDisposition blend_disposition;
    AcgcMetalPacketConsumerCanonicalAlphaDisposition alpha_disposition;
    AcgcMetalPacketConsumerCanonicalRasterDisposition raster_disposition;
    AcgcMetalPacketConsumerCanonicalFogDisposition fog_disposition;
    AcgcMetalPacketConsumerStatus section_status;

    if (!canonical_plan_input_output_ranges_are_valid(plan, output)) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT;
    }
    if (resource_stage != NULL &&
        !canonical_plan_resource_stage_ranges_are_valid(
            plan, resource_stage, output)) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT;
    }
    section_status = canonical_plan_sections_status(
        plan,
        resource_stage,
        &matrix_slot,
        &output_vertex_count,
        &channel_mode,
        &source_factor,
        &destination_factor,
        &blend_operation,
        &depth_compare,
        &cull_mode,
        &tev_disposition,
        &blend_disposition,
        &alpha_disposition,
        &raster_disposition,
        &fog_disposition
    );
    if (section_status != ACGC_METAL_PACKET_CONSUMER_OK) {
        return section_status;
    }
    if (!canonical_plan_build_transform(plan, matrix_slot, &transform)) {
        return ACGC_METAL_PACKET_CONSUMER_CANONICAL_TRANSFORM_UNSUPPORTED;
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
    candidate.state.blend.enabled =
        blend_disposition ==
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_DISPOSITION_MAPPED &&
        plan->blend.mode != ACGC_GX_SEMANTIC_V3_BLEND_MODE_NONE;
    candidate.state.blend.source_rgb_factor = source_factor;
    candidate.state.blend.destination_rgb_factor = destination_factor;
    candidate.state.blend.source_alpha_factor = source_factor;
    candidate.state.blend.destination_alpha_factor = destination_factor;
    candidate.state.blend.rgb_operation = blend_operation;
    candidate.state.blend.alpha_operation = blend_operation;
    candidate.state.raster.cull_mode = cull_mode;
    /* These two fields are fixed sink contract values, not fixture defaults. */
    candidate.state.raster.front_facing_winding =
        ACGC_METAL_WINDING_COUNTER_CLOCKWISE;
    candidate.state.raster.triangle_fill_mode = ACGC_METAL_TRIANGLE_FILL;
    if (raster_disposition ==
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_DISPOSITION_STAGED_UNRENDERED) {
        /* The canonical words remain available to a future raster consumer;
         * staged state must not expose unsupported live viewport/cull values
         * to the bounded fixture sink. */
        candidate.state.viewport.origin_x = ACGC_METAL_FLOAT_ZERO;
        candidate.state.viewport.origin_y = ACGC_METAL_FLOAT_ZERO;
        candidate.state.viewport.width = ACGC_METAL_FLOAT_SIXTY_FOUR;
        candidate.state.viewport.height = ACGC_METAL_FLOAT_SIXTY_FOUR;
        candidate.state.viewport.znear = ACGC_METAL_FLOAT_ZERO;
        candidate.state.viewport.zfar = ACGC_METAL_FLOAT_ONE;
        candidate.state.raster.cull_mode = ACGC_METAL_CULL_NONE;
    }
    candidate.geometry.version = ACGC_RENDERER_GEOMETRY_VERSION;
    candidate.geometry.vertex_count = output_vertex_count;
    candidate.geometry.draw_count = ACGC_RENDERER_GEOMETRY_MAX_DRAWS;
    output_vertex = 0;
    if (plan->geometry.primitive ==
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES) {
        for (vertex = 0; vertex < plan->geometry.vertex_count; vertex++) {
            if (!canonical_plan_copy_renderer_vertex(
                plan,
                matrix_slot,
                channel_mode,
                &plan->geometry.vertices[vertex],
                &candidate.geometry.vertices[output_vertex++])) {
                return ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID;
            }
        }
    } else {
        static const uint32_t quad_triangle_order[6] = {0, 1, 2, 0, 2, 3};

        for (vertex = 0;
             vertex < plan->geometry.vertex_count;
             vertex += 4) {
            for (corner = 0; corner < 6; corner++) {
                if (!canonical_plan_copy_renderer_vertex(
                    plan,
                    matrix_slot,
                    channel_mode,
                    &plan->geometry.vertices[
                        vertex + quad_triangle_order[corner]],
                    &candidate.geometry.vertices[output_vertex++])) {
                    return ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID;
                }
            }
        }
    }
    if (output_vertex != output_vertex_count) {
        return ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID;
    }
    candidate.geometry.draws[0].primitive = ACGC_RENDERER_PRIMITIVE_TRIANGLES;
    candidate.geometry.draws[0].first_vertex = 0;
    candidate.geometry.draws[0].vertex_count = output_vertex_count;
    candidate.texture0_color = (AcgcRendererFixtureColor){255, 255, 255, 255};
    candidate.material_flags = ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR;
    candidate.texture0_key = 0;
    candidate.semantic_version = 0;
    candidate.source_kind = ACGC_METAL_PACKET_CONSUMER_SOURCE_CANONICAL_PLAN;
    candidate.alpha_write_enabled = plan->alpha.alpha_update_enable;
    candidate.canonical_resource_stage = *resource_stage;
    candidate.canonical_tev_disposition = tev_disposition;
    candidate.canonical_tev = plan->tev;
    candidate.canonical_blend_disposition = blend_disposition;
    candidate.canonical_blend = plan->blend;
    candidate.canonical_alpha_disposition = alpha_disposition;
    candidate.canonical_alpha = plan->alpha;
    candidate.canonical_raster_disposition = raster_disposition;
    candidate.canonical_raster = plan->raster;
    candidate.canonical_fog_disposition = fog_disposition;
    candidate.canonical_fog = plan->fog;

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
         vertex_index < ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES;
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
        case ACGC_METAL_PACKET_CONSUMER_CANONICAL_GEOMETRY_UNSUPPORTED:
            return "unsupported canonical Geometry section";
        case ACGC_METAL_PACKET_CONSUMER_CANONICAL_TRANSFORM_UNSUPPORTED:
            return "unsupported canonical Transform section";
        case ACGC_METAL_PACKET_CONSUMER_CANONICAL_CHANNELS_UNSUPPORTED:
            return "unsupported canonical Channels section";
        case ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEXGENS_UNSUPPORTED:
            return "unsupported canonical Texgen section";
        case ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEXTURE_UNSUPPORTED:
            return "unsupported canonical Texture section";
        case ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_UNSUPPORTED:
            return "unsupported canonical TEV section";
        case ACGC_METAL_PACKET_CONSUMER_CANONICAL_LIGHTING_UNSUPPORTED:
            return "unsupported canonical Lighting section";
        case ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_UNSUPPORTED:
            return "unsupported canonical Blend section";
        case ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_UNSUPPORTED:
            return "unsupported canonical Alpha section";
        case ACGC_METAL_PACKET_CONSUMER_CANONICAL_DEPTH_UNSUPPORTED:
            return "unsupported canonical Depth section";
        case ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_UNSUPPORTED:
            return "unsupported canonical Raster section";
        case ACGC_METAL_PACKET_CONSUMER_CANONICAL_FOG_UNSUPPORTED:
            return "unsupported canonical Fog section";
        case ACGC_METAL_PACKET_CONSUMER_CANONICAL_INDIRECT_UNSUPPORTED:
            return "unsupported canonical Indirect section";
        case ACGC_METAL_PACKET_CONSUMER_CANONICAL_DYNAMIC_UNSUPPORTED:
            return "unsupported canonical Dynamic section";
        case
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DEPENDENCY_UNSUPPORTED:
            return "unsupported canonical resource dependency";
    }
    return "unknown consumer status";
}
