#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "acgc/metal_sink.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

typedef struct AcgcMetalSinkState {
    atomic_uint_least32_t initialized;
    atomic_uint_least32_t available;
    atomic_uint_least32_t submit_count;
    atomic_uint_least32_t completed_count;
    atomic_uint_least32_t readback_count;
    atomic_uint_least32_t last_status;
    atomic_uint_least32_t last_pixel_rgba8;
    atomic_uint_least32_t last_checksum;
    atomic_uint_least32_t unavailable_status;
} AcgcMetalSinkState;

static AcgcMetalSinkState s_sink_state = {
    .last_status = ACGC_METAL_SINK_NOT_INITIALIZED,
    .unavailable_status = ACGC_METAL_SINK_NOT_INITIALIZED
};

static id<MTLDevice> s_device;
static id<MTLCommandQueue> s_command_queue;
static id<MTLLibrary> s_library;
static id<MTLLibrary> s_texture_library;

typedef struct AcgcMetalSinkTextureVertex {
    uint32_t position_x;
    uint32_t position_y;
    uint32_t position_z;
    uint32_t texcoord_s;
    uint32_t texcoord_t;
} AcgcMetalSinkTextureVertex;

static const char ACGC_METAL_SINK_SHADER[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "\n"
    "struct AcgcMetalSinkVertex {\n"
    "    uint position_x;\n"
    "    uint position_y;\n"
    "    uint position_z;\n"
    "    uint color_rgba8;\n"
    "};\n"
    "\n"
    "struct AcgcMetalFixedTransform {\n"
    "    uint matrix[16];\n"
    "};\n"
    "\n"
    "struct AcgcMetalSinkOutput {\n"
    "    float4 position [[position]];\n"
    "    float4 color;\n"
    "};\n"
    "\n"
    "vertex AcgcMetalSinkOutput acgc_metal_sink_vertex(\n"
    "    const device AcgcMetalSinkVertex* vertices [[buffer(0)]],\n"
    "    constant AcgcMetalFixedTransform& transform [[buffer(1)]],\n"
    "    uint vertex_id [[vertex_id]]\n"
    ") {\n"
    "    AcgcMetalSinkVertex sink_vertex = vertices[vertex_id];\n"
    "    float4x4 matrix = float4x4(\n"
    "        float4(as_type<float>(transform.matrix[0]),\n"
    "                as_type<float>(transform.matrix[1]),\n"
    "                as_type<float>(transform.matrix[2]),\n"
    "                as_type<float>(transform.matrix[3])),\n"
    "        float4(as_type<float>(transform.matrix[4]),\n"
    "                as_type<float>(transform.matrix[5]),\n"
    "                as_type<float>(transform.matrix[6]),\n"
    "                as_type<float>(transform.matrix[7])),\n"
    "        float4(as_type<float>(transform.matrix[8]),\n"
    "                as_type<float>(transform.matrix[9]),\n"
    "                as_type<float>(transform.matrix[10]),\n"
    "                as_type<float>(transform.matrix[11])),\n"
    "        float4(as_type<float>(transform.matrix[12]),\n"
    "                as_type<float>(transform.matrix[13]),\n"
    "                as_type<float>(transform.matrix[14]),\n"
    "                as_type<float>(transform.matrix[15]))\n"
    "    );\n"
    "    AcgcMetalSinkOutput output;\n"
    "    output.position = matrix * float4(\n"
    "        as_type<float>(sink_vertex.position_x),\n"
    "        as_type<float>(sink_vertex.position_y),\n"
    "        as_type<float>(sink_vertex.position_z),\n"
    "        1.0f\n"
    "    );\n"
    "    output.color = float4(\n"
    "        float((sink_vertex.color_rgba8 >> 24) & 0xffu),\n"
    "        float((sink_vertex.color_rgba8 >> 16) & 0xffu),\n"
    "        float((sink_vertex.color_rgba8 >> 8) & 0xffu),\n"
    "        float(sink_vertex.color_rgba8 & 0xffu)\n"
    "    ) / 255.0f;\n"
    "    return output;\n"
    "}\n"
    "\n"
    "fragment float4 acgc_metal_sink_fragment(\n"
    "    AcgcMetalSinkOutput input [[stage_in]]\n"
    ") {\n"
    "    return input.color;\n"
    "}\n";

static const char ACGC_METAL_SINK_TEXTURE_SHADER[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "\n"
    "struct AcgcMetalSinkTextureVertex {\n"
    "    uint position_x;\n"
    "    uint position_y;\n"
    "    uint position_z;\n"
    "    uint texcoord_s;\n"
    "    uint texcoord_t;\n"
    "};\n"
    "\n"
    "struct AcgcMetalFixedTransform {\n"
    "    uint matrix[16];\n"
    "};\n"
    "\n"
    "struct AcgcMetalSinkTextureOutput {\n"
    "    float4 position [[position]];\n"
    "    float2 texcoord;\n"
    "};\n"
    "\n"
    "vertex AcgcMetalSinkTextureOutput acgc_metal_sink_texture_vertex(\n"
    "    const device AcgcMetalSinkTextureVertex* vertices [[buffer(0)]],\n"
    "    constant AcgcMetalFixedTransform& transform [[buffer(1)]],\n"
    "    uint vertex_id [[vertex_id]]\n"
    ") {\n"
    "    AcgcMetalSinkTextureVertex sink_vertex = vertices[vertex_id];\n"
    "    float4x4 matrix = float4x4(\n"
    "        float4(as_type<float>(transform.matrix[0]),\n"
    "                as_type<float>(transform.matrix[1]),\n"
    "                as_type<float>(transform.matrix[2]),\n"
    "                as_type<float>(transform.matrix[3])),\n"
    "        float4(as_type<float>(transform.matrix[4]),\n"
    "                as_type<float>(transform.matrix[5]),\n"
    "                as_type<float>(transform.matrix[6]),\n"
    "                as_type<float>(transform.matrix[7])),\n"
    "        float4(as_type<float>(transform.matrix[8]),\n"
    "                as_type<float>(transform.matrix[9]),\n"
    "                as_type<float>(transform.matrix[10]),\n"
    "                as_type<float>(transform.matrix[11])),\n"
    "        float4(as_type<float>(transform.matrix[12]),\n"
    "                as_type<float>(transform.matrix[13]),\n"
    "                as_type<float>(transform.matrix[14]),\n"
    "                as_type<float>(transform.matrix[15]))\n"
    "    );\n"
    "    AcgcMetalSinkTextureOutput output;\n"
    "    output.position = matrix * float4(\n"
    "        as_type<float>(sink_vertex.position_x),\n"
    "        as_type<float>(sink_vertex.position_y),\n"
    "        as_type<float>(sink_vertex.position_z),\n"
    "        1.0f\n"
    "    );\n"
    "    output.texcoord = float2(\n"
    "        as_type<float>(sink_vertex.texcoord_s),\n"
    "        as_type<float>(sink_vertex.texcoord_t)\n"
    "    );\n"
    "    return output;\n"
    "}\n"
    "\n"
    "fragment float4 acgc_metal_sink_texture_fragment(\n"
    "    AcgcMetalSinkTextureOutput input [[stage_in]],\n"
    "    texture2d<float> texture [[texture(0)]],\n"
    "    sampler texture_sampler [[sampler(0)]]\n"
    ") {\n"
    "    return texture.sample(texture_sampler, input.texcoord);\n"
    "}\n";

static void increment_counter(atomic_uint_least32_t* counter) {
    uint_least32_t expected =
        atomic_load_explicit(counter, memory_order_relaxed);

    while (expected != UINT32_MAX &&
           !atomic_compare_exchange_weak_explicit(
               counter,
               &expected,
               expected + 1,
               memory_order_relaxed,
               memory_order_relaxed
           )) {
        /* expected is refreshed by a failed compare-exchange. */
    }
}

static void set_last_status(AcgcMetalSinkStatus status) {
    atomic_store_explicit(
        &s_sink_state.last_status,
        (uint_least32_t)status,
        memory_order_release
    );
}

static const char* error_description(NSError* error) {
    const char* description = error.localizedDescription.UTF8String;

    return description != NULL ? description : "unknown Metal error";
}

static float float_from_bits(uint32_t bits) {
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int sink_dimension_from_bits(uint32_t bits, uint32_t* dimension) {
    const float value = float_from_bits(bits);

    if (dimension == NULL ||
        (bits & UINT32_C(0x7F800000)) == UINT32_C(0x7F800000) ||
        !(value > 0.0f) ||
        !(value < (float)ACGC_METAL_SINK_MAX_EDGE) ||
        floorf(value) != value) {
        return 0;
    }
    *dimension = (uint32_t)value;
    return *dimension != 0;
}

static int sink_rgba8_readback_sizes_are_valid(
    uint32_t width,
    uint32_t height,
    size_t* bytes_per_row,
    size_t* byte_count
) {
    const size_t bytes_per_pixel =
        (size_t)ACGC_METAL_SINK_RGBA8_BYTES_PER_PIXEL;
    size_t row_bytes;
    size_t total_bytes;

    if (width == 0 || height == 0 ||
        width >= ACGC_METAL_SINK_MAX_EDGE ||
        height >= ACGC_METAL_SINK_MAX_EDGE ||
        (uintmax_t)width > (uintmax_t)SIZE_MAX / bytes_per_pixel) {
        return 0;
    }
    row_bytes = (size_t)width * bytes_per_pixel;
    if ((uintmax_t)height >
        (uintmax_t)SIZE_MAX / (uintmax_t)row_bytes) {
        return 0;
    }
    total_bytes = (size_t)height * row_bytes;
    if ((uintmax_t)width > (uintmax_t)NSUIntegerMax ||
        (uintmax_t)height > (uintmax_t)NSUIntegerMax ||
        (uintmax_t)row_bytes > (uintmax_t)NSUIntegerMax) {
        return 0;
    }
    if (bytes_per_row != NULL) {
        *bytes_per_row = row_bytes;
    }
    if (byte_count != NULL) {
        *byte_count = total_bytes;
    }
    return 1;
}

static int sink_bytes_are_zero(const void* bytes, size_t byte_count) {
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

static int sink_texture_address_mode(
    uint32_t value,
    MTLSamplerAddressMode* output
) {
    if (output == NULL) {
        return 0;
    }
    switch (value) {
        case ACGC_RENDERER_FIXTURE_WRAP_CLAMP:
            *output = MTLSamplerAddressModeClampToEdge;
            return 1;
        case ACGC_RENDERER_FIXTURE_WRAP_REPEAT:
            *output = MTLSamplerAddressModeRepeat;
            return 1;
        case ACGC_RENDERER_FIXTURE_WRAP_MIRROR:
            *output = MTLSamplerAddressModeMirrorRepeat;
            return 1;
    }
    return 0;
}

static int sink_texture_filter(
    uint32_t value,
    MTLSamplerMinMagFilter* output
) {
    if (output == NULL) {
        return 0;
    }
    switch (value) {
        case ACGC_RENDERER_FIXTURE_FILTER_NEAREST:
            *output = MTLSamplerMinMagFilterNearest;
            return 1;
        case ACGC_RENDERER_FIXTURE_FILTER_LINEAR:
            *output = MTLSamplerMinMagFilterLinear;
            return 1;
    }
    return 0;
}

static int sink_canonical_texture_replace_tev_is_valid(
    const AcgcMetalPacketConsumerOutput* output
) {
    static const uint32_t expected_swap_tables[
        ACGC_GX_CANONICAL_TEV_SWAP_TABLE_COUNT][4] = {
        {0, 1, 2, 3},
        {0, 0, 0, 3},
        {1, 1, 1, 3},
        {2, 2, 2, 3}
    };
    const AcgcGxCanonicalTevState* tev;
    const AcgcGxCanonicalTevStage* stage;
    uint32_t table;

    if (output == NULL ||
        output->canonical_tev_disposition !=
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_TEXTURE_REPLACE) {
        return 0;
    }
    tev = &output->canonical_tev;
    if (!acgc_gx_canonical_tev_state_validate(tev) ||
        tev->header.active_stage_count != 1 ||
        !sink_bytes_are_zero(
            &tev->stages[1], sizeof(tev->stages) - sizeof(tev->stages[0])
        ) ||
        !sink_bytes_are_zero(tev->registers, sizeof(tev->registers)) ||
        !sink_bytes_are_zero(tev->konst, sizeof(tev->konst))) {
        return 0;
    }
    for (table = 0;
         table < ACGC_GX_CANONICAL_TEV_SWAP_TABLE_COUNT;
         table++) {
        if (tev->swap_tables[table].r != expected_swap_tables[table][0] ||
            tev->swap_tables[table].g != expected_swap_tables[table][1] ||
            tev->swap_tables[table].b != expected_swap_tables[table][2] ||
            tev->swap_tables[table].a != expected_swap_tables[table][3]) {
            return 0;
        }
    }

    stage = &tev->stages[0];
    /* GX_REPLACE is ZERO/ZERO/ZERO/TEXC and ZERO/ZERO/ZERO/TEXA,
     * followed by ADD, no bias, scale one, clamp, and PREV. */
    return stage->color_a == ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX &&
        stage->color_b == ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX &&
        stage->color_c == ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX &&
        stage->color_d == 8 &&
        stage->alpha_a == ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX &&
        stage->alpha_b == ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX &&
        stage->alpha_c == ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX &&
        stage->alpha_d == 4 &&
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
        stage->tex_coord == ACGC_GX_CANONICAL_TEV_TEXCOORD_MIN &&
        stage->tex_map <= ACGC_GX_CANONICAL_TEV_TEXMAP_MAX &&
        stage->color_chan == ACGC_GX_CANONICAL_TEV_CHANNEL_MIN &&
        stage->k_color_sel == 0 && stage->k_alpha_sel == 0 &&
        stage->ras_swap == 0 && stage->tex_swap == 0 &&
        stage->ind_stage == 0 && stage->ind_format == 0 &&
        stage->ind_bias == 0 && stage->ind_mtx == 0 &&
        stage->ind_wrap_s == 0 && stage->ind_wrap_t == 0 &&
        stage->ind_add_prev == 0 && stage->ind_lod == 0 &&
        stage->ind_alpha == 0 && stage->reserved[0] == 0 &&
        stage->reserved[1] == 0;
}

static int sink_canonical_texture_replace_is_valid(
    const AcgcMetalPacketConsumerOutput* output,
    size_t* texture_bytes_per_row,
    size_t* texture_byte_count
) {
    const AcgcMetalPacketConsumerCanonicalResourceStage* resource_stage;
    const AcgcMetalPacketConsumerCanonicalTextureBinding* binding;
    const AcgcRendererFixtureTextureDescription* description;
    const AcgcRendererFixtureSamplerDescription* sampler;
    AcgcRendererFixtureSamplerState sampler_state;
    uint32_t map;
    uint32_t tlut;
    uint32_t selected_map;
    uint32_t selected_texcoord;
    uint32_t expected_source_bytes;
    uint64_t expected_decoded_bytes;
    uint32_t referenced_tlut_mask = 0;
    size_t selected_row_bytes = 0;
    size_t selected_byte_count = 0;
    size_t row_bytes;
    size_t byte_count;

    if (output == NULL ||
        output->source_kind !=
            ACGC_METAL_PACKET_CONSUMER_SOURCE_CANONICAL_PLAN ||
        !sink_canonical_texture_replace_tev_is_valid(output)) {
        return 0;
    }
    resource_stage = &output->canonical_resource_stage;
    binding = &output->canonical_texture_binding;
    selected_map = binding->selected_map;
    selected_texcoord = binding->selected_texcoord;
    if (selected_map >= PC_GX_TEXTURE_RAW_MAP_COUNT ||
        selected_texcoord != ACGC_GX_CANONICAL_TEV_TEXCOORD_MIN ||
        selected_map != output->canonical_tev.stages[0].tex_map ||
        selected_texcoord != output->canonical_tev.stages[0].tex_coord ||
        binding->vertex_count == 0 ||
        binding->vertex_count > ACGC_RENDERER_GEOMETRY_MAX_VERTICES ||
        (uintmax_t)binding->vertex_count >
            (uintmax_t)NSUIntegerMax / sizeof(AcgcMetalSinkTextureVertex) ||
        binding->vertex_count != output->geometry.vertex_count ||
        resource_stage->valid != 1 || resource_stage->attempt_id == 0) {
        return 0;
    }

    if ((resource_stage->image_mask &
            ~((UINT32_C(1) << PC_GX_TEXTURE_RAW_MAP_COUNT) - 1)) != 0 ||
        (resource_stage->decoded_image_mask &
            ~((UINT32_C(1) << PC_GX_TEXTURE_RAW_MAP_COUNT) - 1)) != 0 ||
        (resource_stage->tlut_mask &
            ~((UINT32_C(1) << PC_GX_TEXTURE_RAW_TLUT_COUNT) - 1)) != 0 ||
        resource_stage->image_mask != resource_stage->decoded_image_mask ||
        (resource_stage->image_mask & (UINT32_C(1) << selected_map)) == 0 ||
        (resource_stage->decoded_image_mask &
            (UINT32_C(1) << selected_map)) == 0) {
        return 0;
    }

    for (tlut = 0; tlut < PC_GX_TEXTURE_RAW_TLUT_COUNT; tlut++) {
        const uint32_t mask = UINT32_C(1) << tlut;
        if ((resource_stage->tlut_mask & mask) == 0) {
            if (resource_stage->tlut_byte_sizes[tlut] != 0 ||
                !sink_bytes_are_zero(
                    resource_stage->tlut_bytes[tlut],
                    sizeof(resource_stage->tlut_bytes[tlut]))) {
                return 0;
            }
        } else if (resource_stage->tlut_byte_sizes[tlut] == 0 ||
                   resource_stage->tlut_byte_sizes[tlut] >
                       ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_TLUT_BYTES ||
                   !sink_bytes_are_zero(
                       &resource_stage->tlut_bytes[tlut][
                           resource_stage->tlut_byte_sizes[tlut]],
                       ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_TLUT_BYTES -
                           resource_stage->tlut_byte_sizes[tlut])) {
            return 0;
        }
    }

    for (map = 0; map < PC_GX_TEXTURE_RAW_MAP_COUNT; map++) {
        const uint32_t mask = UINT32_C(1) << map;
        const uint32_t image_size = resource_stage->image_byte_sizes[map];
        const uint32_t decoded_size =
            resource_stage->decoded_rgba_byte_sizes[map];

        if ((resource_stage->image_mask & mask) == 0) {
            if (image_size != 0 || decoded_size != 0 ||
                !sink_bytes_are_zero(
                    resource_stage->image_bytes[map],
                    sizeof(resource_stage->image_bytes[map])) ||
                !sink_bytes_are_zero(
                    resource_stage->decoded_rgba[map],
                    sizeof(resource_stage->decoded_rgba[map])) ||
                !sink_bytes_are_zero(
                    &resource_stage->descriptions[map],
                    sizeof(resource_stage->descriptions[map])) ||
                !sink_bytes_are_zero(
                    &resource_stage->samplers[map],
                    sizeof(resource_stage->samplers[map]))) {
                return 0;
            }
            continue;
        }

        description = &resource_stage->descriptions[map];
        sampler = &resource_stage->samplers[map];
        if (description->version != ACGC_RENDERER_FIXTURE_VERSION ||
            description->width == 0 || description->height == 0 ||
            description->data_byte_order > ACGC_RENDERER_FIXTURE_LITTLE_ENDIAN ||
            description->tlut_format > ACGC_RENDERER_FIXTURE_TL_RGB5A3 ||
            description->tlut_entries > ACGC_RENDERER_FIXTURE_MAX_TLUT_ENTRIES ||
            description->tlut_byte_order > ACGC_RENDERER_FIXTURE_LITTLE_ENDIAN ||
            image_size == 0 ||
            image_size > ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_IMAGE_BYTES ||
            !acgc_renderer_fixture_resolve_sampler(sampler, &sampler_state) ||
            !sink_rgba8_readback_sizes_are_valid(
                description->width,
                description->height,
                &row_bytes,
                &byte_count
            ) ||
            (uintmax_t)description->width *
                    (uintmax_t)description->height >
                (uintmax_t)UINT32_MAX / 4) {
            return 0;
        }
        expected_source_bytes = acgc_renderer_fixture_texture_bytes(
            description->width, description->height, description->format);
        expected_decoded_bytes = (uint64_t)description->width *
            (uint64_t)description->height * 4;
        if (expected_source_bytes == 0 ||
            description->data_size != expected_source_bytes ||
            image_size != description->data_size ||
            decoded_size != expected_decoded_bytes ||
            decoded_size != byte_count ||
            byte_count >
                ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DECODED_RGBA_BYTES ||
            !sink_bytes_are_zero(
                &resource_stage->image_bytes[map][image_size],
                ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_IMAGE_BYTES -
                    image_size) ||
            !sink_bytes_are_zero(
                &resource_stage->decoded_rgba[map][decoded_size],
                ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_DECODED_RGBA_BYTES -
                    decoded_size)) {
            return 0;
        }
        if (map == selected_map) {
            selected_row_bytes = row_bytes;
            selected_byte_count = byte_count;
        }
        if (description->tlut_entries == 0) {
            if (description->tlut_data_size != 0) {
                return 0;
            }
        } else {
            int matching_tlut = 0;
            if (description->tlut_data_size == 0 ||
                description->tlut_data_size >
                    ACGC_METAL_PACKET_CONSUMER_CANONICAL_RESOURCE_TLUT_BYTES) {
                return 0;
            }
            for (tlut = 0; tlut < PC_GX_TEXTURE_RAW_TLUT_COUNT; tlut++) {
                if ((resource_stage->tlut_mask & (UINT32_C(1) << tlut)) != 0 &&
                    resource_stage->tlut_byte_sizes[tlut] ==
                        description->tlut_data_size) {
                    matching_tlut = 1;
                    referenced_tlut_mask |= UINT32_C(1) << tlut;
                    break;
                }
            }
            if (!matching_tlut) {
                return 0;
            }
        }
    }

    if (resource_stage->tlut_mask != referenced_tlut_mask) {
        return 0;
    }

    for (map = 0; map < binding->vertex_count; map++) {
        if ((binding->texcoord_words[map][0] & UINT32_C(0x7F800000)) ==
                UINT32_C(0x7F800000) ||
            (binding->texcoord_words[map][1] & UINT32_C(0x7F800000)) ==
                UINT32_C(0x7F800000)) {
            return 0;
        }
    }
    for (; map < ACGC_RENDERER_GEOMETRY_MAX_VERTICES; map++) {
        if (binding->texcoord_words[map][0] != 0 ||
            binding->texcoord_words[map][1] != 0) {
            return 0;
        }
    }

    if (texture_bytes_per_row != NULL) {
        *texture_bytes_per_row = selected_row_bytes;
    }
    if (texture_byte_count != NULL) {
        *texture_byte_count = selected_byte_count;
    }
    return 1;
}

static MTLCompareFunction metal_compare_function(uint32_t value) {
    switch (value) {
        case ACGC_METAL_DEPTH_NEVER: return MTLCompareFunctionNever;
        case ACGC_METAL_DEPTH_LESS: return MTLCompareFunctionLess;
        case ACGC_METAL_DEPTH_EQUAL: return MTLCompareFunctionEqual;
        case ACGC_METAL_DEPTH_LESS_EQUAL: return MTLCompareFunctionLessEqual;
        case ACGC_METAL_DEPTH_GREATER: return MTLCompareFunctionGreater;
        case ACGC_METAL_DEPTH_NOT_EQUAL: return MTLCompareFunctionNotEqual;
        case ACGC_METAL_DEPTH_GREATER_EQUAL: return MTLCompareFunctionGreaterEqual;
        case ACGC_METAL_DEPTH_ALWAYS: return MTLCompareFunctionAlways;
    }
    return MTLCompareFunctionNever;
}

static MTLBlendFactor metal_blend_factor(uint32_t value) {
    switch (value) {
        case ACGC_METAL_BLEND_ZERO: return MTLBlendFactorZero;
        case ACGC_METAL_BLEND_ONE: return MTLBlendFactorOne;
        case ACGC_METAL_BLEND_SOURCE_ALPHA: return MTLBlendFactorSourceAlpha;
        case ACGC_METAL_BLEND_ONE_MINUS_SOURCE_ALPHA:
            return MTLBlendFactorOneMinusSourceAlpha;
        case ACGC_METAL_BLEND_DESTINATION_COLOR:
            return MTLBlendFactorDestinationColor;
        case ACGC_METAL_BLEND_ONE_MINUS_DESTINATION_COLOR:
            return MTLBlendFactorOneMinusDestinationColor;
        case ACGC_METAL_BLEND_SOURCE_COLOR:
            return MTLBlendFactorSourceColor;
        case ACGC_METAL_BLEND_ONE_MINUS_SOURCE_COLOR:
            return MTLBlendFactorOneMinusSourceColor;
        case ACGC_METAL_BLEND_DESTINATION_ALPHA:
            return MTLBlendFactorDestinationAlpha;
        case ACGC_METAL_BLEND_ONE_MINUS_DESTINATION_ALPHA:
            return MTLBlendFactorOneMinusDestinationAlpha;
    }
    return MTLBlendFactorZero;
}

static MTLBlendOperation metal_blend_operation(uint32_t value) {
    switch (value) {
        case ACGC_METAL_BLEND_ADD: return MTLBlendOperationAdd;
        case ACGC_METAL_BLEND_REVERSE_SUBTRACT:
            return MTLBlendOperationReverseSubtract;
    }
    return MTLBlendOperationAdd;
}

static int sink_canonical_blend_factor_to_metal(
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
        /* GX's numeric color aliases are interpreted by their field. */
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
    }
    return 0;
}

static int sink_canonical_blend_matches_state(
    const AcgcMetalPacketConsumerOutput* output
) {
    const AcgcGxCanonicalBlendState* blend;
    uint32_t source_factor;
    uint32_t destination_factor;
    uint32_t operation;

    if (output == NULL ||
        output->canonical_blend_disposition !=
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_DISPOSITION_MAPPED) {
        return 0;
    }
    blend = &output->canonical_blend;
    if (!acgc_gx_canonical_blend_state_validate(blend) ||
        blend->mode == ACGC_GX_SEMANTIC_V3_BLEND_MODE_LOGIC ||
        !sink_canonical_blend_factor_to_metal(
            blend->source_factor, 1, &source_factor) ||
        !sink_canonical_blend_factor_to_metal(
            blend->destination_factor, 0, &destination_factor)) {
        return 0;
    }
    operation = blend->mode == ACGC_GX_SEMANTIC_V3_BLEND_MODE_SUBTRACT
        ? ACGC_METAL_BLEND_REVERSE_SUBTRACT
        : ACGC_METAL_BLEND_ADD;
    return output->state.blend.enabled ==
            (blend->mode != ACGC_GX_SEMANTIC_V3_BLEND_MODE_NONE) &&
        output->state.blend.source_rgb_factor == source_factor &&
        output->state.blend.destination_rgb_factor == destination_factor &&
        output->state.blend.source_alpha_factor == source_factor &&
        output->state.blend.destination_alpha_factor == destination_factor &&
        output->state.blend.rgb_operation == operation &&
        output->state.blend.alpha_operation == operation;
}

static int sink_canonical_alpha_compare_is_true(
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

static int sink_canonical_alpha_predicate_is_tautology(
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
        const int first = sink_canonical_alpha_compare_is_true(
            alpha->comp0, fragment_alpha, alpha->ref0);
        const int second = sink_canonical_alpha_compare_is_true(
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

static int sink_canonical_alpha_matches_state(
    const AcgcMetalPacketConsumerOutput* output
) {
    const AcgcGxCanonicalAlphaState* alpha;

    if (output == NULL ||
        output->canonical_alpha_disposition !=
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_DISPOSITION_PASSTHROUGH) {
        return 0;
    }
    alpha = &output->canonical_alpha;
    return sink_canonical_alpha_predicate_is_tautology(alpha) &&
        output->alpha_write_enabled == alpha->alpha_update_enable;
}

static int sink_canonical_raster_cull_mode_to_metal(
    uint32_t cull_mode,
    uint32_t* output
) {
    if (output == NULL) {
        return 0;
    }
    switch (cull_mode) {
        case ACGC_GX_CANONICAL_RASTER_CULL_MODE_NONE:
            *output = ACGC_METAL_CULL_NONE;
            return 1;
        case ACGC_GX_CANONICAL_RASTER_CULL_MODE_FRONT:
            *output = ACGC_METAL_CULL_FRONT;
            return 1;
        case ACGC_GX_CANONICAL_RASTER_CULL_MODE_BACK:
            *output = ACGC_METAL_CULL_BACK;
            return 1;
    }
    return 0;
}

static int sink_canonical_raster_optional_words_are_supported(
    const AcgcGxCanonicalRasterState* raster
) {
    const int legacy_fixture_shape =
        raster->line_width == 0 &&
        raster->line_tex_offsets == 0 &&
        raster->point_size == 0 &&
        raster->point_tex_offsets == 0 &&
        raster->line_texcoord_mask == 0 &&
        raster->point_texcoord_mask == 0 &&
        raster->dither == 0 &&
        raster->field_mode == 0 &&
        raster->half_aspect_ratio == 0 &&
        raster->field_odd_mask == 0 &&
        raster->field_even_mask == 0;
    const int decomp_initialization_shape =
        raster->line_width == 5 &&
        raster->line_tex_offsets == 0 &&
        raster->point_size == 6 &&
        raster->point_tex_offsets == 0 &&
        raster->line_texcoord_mask == 0 &&
        raster->point_texcoord_mask == 0 &&
        raster->dither == 1 &&
        raster->field_mode == 0 &&
        raster->half_aspect_ratio == 0 &&
        raster->field_odd_mask == 1 &&
        raster->field_even_mask == 1;

    /* The one sink draw is a triangle, so line/point words do not alter this
     * geometry; nevertheless, only the exact source shapes are admitted. */
    return legacy_fixture_shape || decomp_initialization_shape;
}

static int sink_canonical_raster_matches_state(
    const AcgcMetalPacketConsumerOutput* output
) {
    const AcgcGxCanonicalRasterState* raster;
    uint32_t cull_mode;
    uint32_t viewport_width;
    uint32_t viewport_height;

    if (output == NULL ||
        output->canonical_raster_disposition !=
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_DISPOSITION_MAPPED) {
        return 0;
    }
    raster = &output->canonical_raster;
    if (!acgc_gx_canonical_raster_state_validate(raster) ||
        raster->viewport_bits[0] != ACGC_METAL_FLOAT_ZERO ||
        raster->viewport_bits[1] != ACGC_METAL_FLOAT_ZERO ||
        raster->viewport_bits[4] != ACGC_METAL_FLOAT_ZERO ||
        raster->viewport_bits[5] != ACGC_METAL_FLOAT_ONE ||
        raster->scissor[0] != 0 || raster->scissor[1] != 0 ||
        !sink_dimension_from_bits(
            raster->viewport_bits[2], &viewport_width) ||
        !sink_dimension_from_bits(
            raster->viewport_bits[3], &viewport_height) ||
        raster->scissor[2] != viewport_width ||
        raster->scissor[3] != viewport_height ||
        raster->scissor_offset[0] != 0 || raster->scissor_offset[1] != 0 ||
        raster->clip_mode != ACGC_GX_CANONICAL_RASTER_CLIP_MODE_ENABLE ||
        raster->co_planar_enable != 0 ||
        raster->dst_alpha_enable != 0 || raster->dst_alpha != 0 ||
        !sink_canonical_raster_optional_words_are_supported(raster) ||
        !sink_canonical_raster_cull_mode_to_metal(
            raster->cull_mode, &cull_mode)) {
        return 0;
    }
    return output->state.viewport.origin_x == raster->viewport_bits[0] &&
        output->state.viewport.origin_y == raster->viewport_bits[1] &&
        output->state.viewport.width == raster->viewport_bits[2] &&
        output->state.viewport.height == raster->viewport_bits[3] &&
        output->state.viewport.znear == raster->viewport_bits[4] &&
        output->state.viewport.zfar == raster->viewport_bits[5] &&
        output->state.raster.cull_mode == cull_mode &&
        output->state.raster.front_facing_winding ==
            ACGC_METAL_WINDING_COUNTER_CLOCKWISE &&
        output->state.raster.triangle_fill_mode == ACGC_METAL_TRIANGLE_FILL;
}

static int sink_canonical_fog_matches_state(
    const AcgcMetalPacketConsumerOutput* output
) {
    const AcgcGxCanonicalFogState* fog;

    if (output == NULL ||
        output->canonical_fog_disposition !=
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_FOG_DISPOSITION_INACTIVE) {
        return 0;
    }
    fog = &output->canonical_fog;
    return acgc_gx_canonical_fog_state_validate(fog) &&
        fog->fog_type == ACGC_GX_CANONICAL_FOG_TYPE_NONE &&
        fog->range_adjust_enable == 0;
}

static MTLWinding metal_winding(uint32_t value) {
    return value == ACGC_METAL_WINDING_COUNTER_CLOCKWISE
        ? MTLWindingCounterClockwise
        : MTLWindingClockwise;
}

static MTLCullMode metal_cull_mode(uint32_t value) {
    switch (value) {
        case ACGC_METAL_CULL_NONE: return MTLCullModeNone;
        case ACGC_METAL_CULL_FRONT: return MTLCullModeFront;
        case ACGC_METAL_CULL_BACK: return MTLCullModeBack;
    }
    return MTLCullModeNone;
}

static MTLTriangleFillMode metal_fill_mode(uint32_t value) {
    return value == ACGC_METAL_TRIANGLE_LINES
        ? MTLTriangleFillModeLines
        : MTLTriangleFillModeFill;
}

static int sink_output_is_valid(
    const AcgcMetalPacketConsumerOutput* output
) {
    const AcgcRendererDraw* draw;
    const AcgcMetalStateFixture* state;
    uint32_t viewport_width;
    uint32_t viewport_height;

    if (output == NULL ||
        (output->source_kind != ACGC_METAL_PACKET_CONSUMER_SOURCE_SEMANTIC &&
         output->source_kind != ACGC_METAL_PACKET_CONSUMER_SOURCE_CANONICAL_PLAN) ||
        !acgc_metal_state_fixture_validate(&output->state) ||
        !acgc_renderer_geometry_validate(&output->geometry)) {
        return 0;
    }
    if (output->source_kind ==
            ACGC_METAL_PACKET_CONSUMER_SOURCE_CANONICAL_PLAN) {
        if (output->canonical_tev_disposition ==
                ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_VERTEX_COLOR_PASSTHROUGH) {
            /* Keep the existing semantic/vertex-color path unchanged. */
        } else if (output->canonical_tev_disposition ==
                ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_TEXTURE_REPLACE) {
            if (!sink_canonical_texture_replace_is_valid(output, NULL, NULL)) {
                return 0;
            }
        } else {
            /* A canonical staged TEV must not silently reuse either shader. */
            return 0;
        }
        if (!sink_canonical_blend_matches_state(output) ||
            !sink_canonical_alpha_matches_state(output) ||
            !sink_canonical_raster_matches_state(output) ||
            !sink_canonical_fog_matches_state(output)) {
            return 0;
        }
    }

    state = &output->state;
    if (state->viewport.origin_x != ACGC_METAL_FLOAT_ZERO ||
        state->viewport.origin_y != ACGC_METAL_FLOAT_ZERO ||
        state->viewport.znear != ACGC_METAL_FLOAT_ZERO ||
        state->viewport.zfar != ACGC_METAL_FLOAT_ONE ||
        !sink_dimension_from_bits(
            state->viewport.width, &viewport_width) ||
        !sink_dimension_from_bits(
            state->viewport.height, &viewport_height) ||
        !sink_rgba8_readback_sizes_are_valid(
            viewport_width,
            viewport_height,
            NULL,
            NULL) ||
        output->geometry.vertex_count == 0 ||
        output->geometry.draw_count != ACGC_RENDERER_GEOMETRY_MAX_DRAWS) {
        return 0;
    }

    draw = &output->geometry.draws[0];
    return draw->primitive == ACGC_RENDERER_PRIMITIVE_TRIANGLES &&
        draw->first_vertex == 0 &&
        draw->vertex_count == output->geometry.vertex_count;
}

static void clear_resources(void) {
    s_library = nil;
    s_texture_library = nil;
    s_command_queue = nil;
    s_device = nil;
}

AcgcMetalSinkStatus acgc_metal_sink_init(void) {
    AcgcMetalSinkStatus status;

    if (atomic_load_explicit(
            &s_sink_state.initialized,
            memory_order_acquire
        ) != 0) {
        return (AcgcMetalSinkStatus)atomic_load_explicit(
            &s_sink_state.last_status,
            memory_order_acquire
        );
    }

    @autoreleasepool {
        NSError* error = nil;
        NSError* texture_error = nil;
        id<MTLFunction> vertex_function = nil;
        id<MTLFunction> fragment_function = nil;
        id<MTLFunction> texture_vertex_function = nil;
        id<MTLFunction> texture_fragment_function = nil;

        atomic_store_explicit(&s_sink_state.available, 0, memory_order_relaxed);
        atomic_store_explicit(&s_sink_state.submit_count, 0, memory_order_relaxed);
        atomic_store_explicit(&s_sink_state.completed_count, 0, memory_order_relaxed);
        atomic_store_explicit(&s_sink_state.readback_count, 0, memory_order_relaxed);
        atomic_store_explicit(&s_sink_state.last_pixel_rgba8, 0, memory_order_relaxed);
        atomic_store_explicit(&s_sink_state.last_checksum, 0, memory_order_relaxed);
        atomic_store_explicit(
            &s_sink_state.unavailable_status,
            ACGC_METAL_SINK_RESOURCE_FAILURE,
            memory_order_relaxed
        );

        s_device = MTLCreateSystemDefaultDevice();
        if (s_device == nil) {
            status = ACGC_METAL_SINK_NO_DEVICE;
            atomic_store_explicit(
                &s_sink_state.unavailable_status,
                status,
                memory_order_relaxed
            );
        } else {
            s_library = [s_device newLibraryWithSource:
                [NSString stringWithUTF8String:ACGC_METAL_SINK_SHADER]
                options:nil
                error:&error];
            s_texture_library = [s_device newLibraryWithSource:
                [NSString stringWithUTF8String:ACGC_METAL_SINK_TEXTURE_SHADER]
                options:nil
                error:&texture_error];
            vertex_function = [s_library newFunctionWithName:
                @"acgc_metal_sink_vertex"];
            fragment_function = [s_library newFunctionWithName:
                @"acgc_metal_sink_fragment"];
            texture_vertex_function = [s_texture_library newFunctionWithName:
                @"acgc_metal_sink_texture_vertex"];
            texture_fragment_function = [s_texture_library newFunctionWithName:
                @"acgc_metal_sink_texture_fragment"];

            if (s_library == nil || vertex_function == nil ||
                fragment_function == nil || s_texture_library == nil ||
                texture_vertex_function == nil || texture_fragment_function == nil) {
                fprintf(stderr, "ACGC Metal sink shader compile failed: %s; texture: %s\n",
                        error_description(error), error_description(texture_error));
                status = ACGC_METAL_SINK_RESOURCE_FAILURE;
            } else {
                s_command_queue = [s_device newCommandQueue];

                if (s_command_queue == nil) {
                    fprintf(stderr,
                            "ACGC Metal sink resource creation failed: %s\n",
                            error_description(error));
                    clear_resources();
                    status = ACGC_METAL_SINK_RESOURCE_FAILURE;
                } else {
                    status = ACGC_METAL_SINK_OK;
                }
            }

            if (status != ACGC_METAL_SINK_OK) {
                clear_resources();
            }
            atomic_store_explicit(
                &s_sink_state.unavailable_status,
                status,
                memory_order_relaxed
            );
        }

        atomic_store_explicit(
            &s_sink_state.available,
            status == ACGC_METAL_SINK_OK ? 1 : 0,
            memory_order_release
        );
        set_last_status(status);
        atomic_store_explicit(&s_sink_state.initialized, 1, memory_order_release);
    }

    return status;
}

void acgc_metal_sink_shutdown(void) {
    if (atomic_load_explicit(
            &s_sink_state.initialized,
            memory_order_acquire
        ) == 0) {
        return;
    }

    atomic_store_explicit(&s_sink_state.available, 0, memory_order_release);
    clear_resources();
    atomic_store_explicit(
        &s_sink_state.unavailable_status,
        ACGC_METAL_SINK_NOT_INITIALIZED,
        memory_order_relaxed
    );
    set_last_status(ACGC_METAL_SINK_NOT_INITIALIZED);
    atomic_store_explicit(&s_sink_state.initialized, 0, memory_order_release);
}

AcgcMetalSinkStatus acgc_metal_sink_submit(
    const AcgcMetalPacketConsumerOutput* output
) {
    AcgcMetalSinkStatus status = ACGC_METAL_SINK_NOT_INITIALIZED;

    @autoreleasepool {
        if (atomic_load_explicit(
                &s_sink_state.initialized,
                memory_order_acquire
            ) == 0) {
            set_last_status(status);
        } else {
            NSError* error = nil;
            id<MTLBuffer> vertex_buffer = nil;
            id<MTLBuffer> texture_vertex_buffer = nil;
            id<MTLBuffer> transform_buffer = nil;
            id<MTLTexture> color_texture = nil;
            id<MTLTexture> depth_texture = nil;
            id<MTLTexture> source_texture = nil;
            MTLTextureDescriptor* color_descriptor = nil;
            MTLTextureDescriptor* depth_texture_descriptor = nil;
            MTLTextureDescriptor* source_texture_descriptor = nil;
            MTLSamplerDescriptor* sampler_descriptor = nil;
            id<MTLSamplerState> sampler_state = nil;
            MTLRenderPipelineDescriptor* pipeline_descriptor = nil;
            MTLRenderPipelineColorAttachmentDescriptor* color_attachment = nil;
            id<MTLRenderPipelineState> pipeline = nil;
            MTLDepthStencilDescriptor* depth_descriptor = nil;
            id<MTLDepthStencilState> depth_state = nil;
            MTLRenderPassDescriptor* render_pass = nil;
            id<MTLCommandBuffer> command_buffer = nil;
            id<MTLRenderCommandEncoder> encoder = nil;
            MTLViewport viewport;
            MTLScissorRect scissor;
            uint8_t* readback = NULL;
            uint32_t checksum = UINT32_C(2166136261);
            uint32_t pixel;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t texture_width = 0;
            uint32_t texture_height = 0;
            int texture_replace = 0;
            size_t bytes_per_row = 0;
            size_t readback_byte_count = 0;
            size_t texture_bytes_per_row = 0;
            AcgcRendererFixtureSamplerState texture_sampler_state;
            MTLSamplerAddressMode texture_address_s;
            MTLSamplerAddressMode texture_address_t;
            MTLSamplerMinMagFilter texture_min_filter;
            MTLSamplerMinMagFilter texture_mag_filter;
            AcgcMetalSinkTextureVertex texture_vertices[
                ACGC_RENDERER_GEOMETRY_MAX_VERTICES];
            size_t byte_index;

            increment_counter(&s_sink_state.submit_count);
            if (!sink_output_is_valid(output)) {
                status = ACGC_METAL_SINK_INVALID_OUTPUT;
            } else if (!sink_dimension_from_bits(
                           output->state.viewport.width, &width) ||
                       !sink_dimension_from_bits(
                           output->state.viewport.height, &height) ||
                       !sink_rgba8_readback_sizes_are_valid(
                           width,
                           height,
                           &bytes_per_row,
                           &readback_byte_count)) {
                status = ACGC_METAL_SINK_INVALID_OUTPUT;
            } else if (output->source_kind ==
                           ACGC_METAL_PACKET_CONSUMER_SOURCE_CANONICAL_PLAN &&
                       output->canonical_tev_disposition ==
                           ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_TEXTURE_REPLACE &&
                       (!sink_canonical_texture_replace_is_valid(
                           output, &texture_bytes_per_row, NULL
                       ) ||
                        !acgc_renderer_fixture_resolve_sampler(
                            &output->canonical_resource_stage.samplers[
                                output->canonical_texture_binding.selected_map],
                            &texture_sampler_state
                        ) ||
                        !sink_texture_address_mode(
                            texture_sampler_state.address_s,
                            &texture_address_s
                        ) ||
                        !sink_texture_filter(
                            texture_sampler_state.min_filter,
                            &texture_min_filter
                        ) ||
                        !sink_texture_address_mode(
                            texture_sampler_state.address_t,
                            &texture_address_t
                        ) ||
                        !sink_texture_filter(
                            texture_sampler_state.mag_filter,
                            &texture_mag_filter
                        ))) {
                status = ACGC_METAL_SINK_INVALID_OUTPUT;
            } else if (atomic_load_explicit(
                           &s_sink_state.available,
                           memory_order_acquire
                       ) == 0) {
                status = (AcgcMetalSinkStatus)atomic_load_explicit(
                    &s_sink_state.unavailable_status,
                    memory_order_acquire
                );
            } else {
                readback = malloc(readback_byte_count);
                if (readback == NULL) {
                    status = ACGC_METAL_SINK_RESOURCE_FAILURE;
                } else {
                    texture_replace = output->source_kind ==
                            ACGC_METAL_PACKET_CONSUMER_SOURCE_CANONICAL_PLAN &&
                        output->canonical_tev_disposition ==
                            ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_TEXTURE_REPLACE;
                    if (texture_replace) {
                        const AcgcMetalPacketConsumerCanonicalTextureBinding* binding =
                            &output->canonical_texture_binding;
                        const AcgcRendererFixtureTextureDescription* description =
                            &output->canonical_resource_stage.descriptions[
                                binding->selected_map];
                        uint32_t vertex;

                        texture_width = description->width;
                        texture_height = description->height;
                        for (vertex = 0; vertex < binding->vertex_count; vertex++) {
                            texture_vertices[vertex].position_x =
                                output->geometry.vertices[vertex].position_x;
                            texture_vertices[vertex].position_y =
                                output->geometry.vertices[vertex].position_y;
                            texture_vertices[vertex].position_z =
                                output->geometry.vertices[vertex].position_z;
                            texture_vertices[vertex].texcoord_s =
                                binding->texcoord_words[vertex][0];
                            texture_vertices[vertex].texcoord_t =
                                binding->texcoord_words[vertex][1];
                        }
                    }
                    color_descriptor = [MTLTextureDescriptor
                        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                     width:(NSUInteger)width
                                                    height:(NSUInteger)height
                                                 mipmapped:NO];
                    depth_texture_descriptor = [MTLTextureDescriptor
                        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                     width:(NSUInteger)width
                                                    height:(NSUInteger)height
                                                 mipmapped:NO];
                    if (color_descriptor == nil ||
                        depth_texture_descriptor == nil) {
                        status = ACGC_METAL_SINK_RESOURCE_FAILURE;
                    } else {
                        color_descriptor.usage = MTLTextureUsageRenderTarget;
                        color_descriptor.storageMode = MTLStorageModeShared;
                        color_texture = [s_device
                            newTextureWithDescriptor:color_descriptor];
                        depth_texture_descriptor.usage = MTLTextureUsageRenderTarget;
                        depth_texture_descriptor.storageMode = MTLStorageModeShared;
                        depth_texture = [s_device
                            newTextureWithDescriptor:depth_texture_descriptor];
                        if (texture_replace) {
                            const AcgcMetalPacketConsumerCanonicalTextureBinding* binding =
                                &output->canonical_texture_binding;
                            const AcgcMetalPacketConsumerCanonicalResourceStage* resource_stage =
                                &output->canonical_resource_stage;

                            source_texture_descriptor = [MTLTextureDescriptor
                                texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                             width:(NSUInteger)texture_width
                                                            height:(NSUInteger)texture_height
                                                         mipmapped:NO];
                            sampler_descriptor = [[MTLSamplerDescriptor alloc] init];
                            if (source_texture_descriptor != nil) {
                                source_texture_descriptor.usage =
                                    MTLTextureUsageShaderRead;
                                source_texture_descriptor.storageMode =
                                    MTLStorageModeShared;
                                source_texture = [s_device
                                    newTextureWithDescriptor:source_texture_descriptor];
                            }
                            sampler_descriptor.sAddressMode = texture_address_s;
                            sampler_descriptor.tAddressMode = texture_address_t;
                            sampler_descriptor.minFilter = texture_min_filter;
                            sampler_descriptor.magFilter = texture_mag_filter;
                            sampler_descriptor.mipFilter = MTLSamplerMipFilterNotMipmapped;
                            sampler_descriptor.normalizedCoordinates = YES;
                            sampler_state = [s_device
                                newSamplerStateWithDescriptor:sampler_descriptor];
                            texture_vertex_buffer = [s_device
                                newBufferWithBytes:texture_vertices
                                             length:(NSUInteger)binding->vertex_count *
                                                 sizeof(texture_vertices[0])
                                            options:MTLResourceStorageModeShared];
                            if (source_texture != nil) {
                                [source_texture
                                    replaceRegion:MTLRegionMake2D(
                                        0,
                                        0,
                                        (NSUInteger)texture_width,
                                        (NSUInteger)texture_height
                                    )
                                    mipmapLevel:0
                                    withBytes:resource_stage->decoded_rgba[
                                        binding->selected_map]
                                    bytesPerRow:(NSUInteger)texture_bytes_per_row];
                            }
                        } else {
                            vertex_buffer = [s_device
                                newBufferWithBytes:output->geometry.vertices
                                             length:(NSUInteger)output->geometry.vertex_count *
                                                 sizeof(output->geometry.vertices[0])
                                            options:MTLResourceStorageModeShared];
                        }
                        transform_buffer = [s_device
                            newBufferWithBytes:&output->state.transform
                                         length:sizeof(output->state.transform)
                                        options:MTLResourceStorageModeShared];
                        render_pass = [MTLRenderPassDescriptor renderPassDescriptor];
                        render_pass.colorAttachments[0].texture = color_texture;
                        render_pass.colorAttachments[0].loadAction = MTLLoadActionClear;
                        render_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                        render_pass.colorAttachments[0].clearColor =
                            MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
                        render_pass.depthAttachment.texture = depth_texture;
                        render_pass.depthAttachment.loadAction = MTLLoadActionClear;
                        render_pass.depthAttachment.storeAction = MTLStoreActionStore;
                        render_pass.depthAttachment.clearDepth = 1.0;
                        if (output->source_kind ==
                                ACGC_METAL_PACKET_CONSUMER_SOURCE_CANONICAL_PLAN) {
                            viewport = (MTLViewport){
                                float_from_bits(output->canonical_raster.viewport_bits[0]),
                                float_from_bits(output->canonical_raster.viewport_bits[1]),
                                float_from_bits(output->canonical_raster.viewport_bits[2]),
                                float_from_bits(output->canonical_raster.viewport_bits[3]),
                                float_from_bits(output->canonical_raster.viewport_bits[4]),
                                float_from_bits(output->canonical_raster.viewport_bits[5])
                            };
                            scissor = (MTLScissorRect){
                                (NSUInteger)output->canonical_raster.scissor[0],
                                (NSUInteger)output->canonical_raster.scissor[1],
                                (NSUInteger)output->canonical_raster.scissor[2],
                                (NSUInteger)output->canonical_raster.scissor[3]
                            };
                        } else {
                            viewport = (MTLViewport){
                                float_from_bits(output->state.viewport.origin_x),
                                float_from_bits(output->state.viewport.origin_y),
                                float_from_bits(output->state.viewport.width),
                                float_from_bits(output->state.viewport.height),
                                float_from_bits(output->state.viewport.znear),
                                float_from_bits(output->state.viewport.zfar)
                            };
                            scissor = (MTLScissorRect){
                                0,
                                0,
                                (NSUInteger)width,
                                (NSUInteger)height
                            };
                        }

                        pipeline_descriptor = [[MTLRenderPipelineDescriptor alloc] init];
                        if (texture_replace) {
                            pipeline_descriptor.vertexFunction = [s_texture_library
                                newFunctionWithName:@"acgc_metal_sink_texture_vertex"];
                            pipeline_descriptor.fragmentFunction = [s_texture_library
                                newFunctionWithName:@"acgc_metal_sink_texture_fragment"];
                        } else {
                            pipeline_descriptor.vertexFunction = [s_library
                                newFunctionWithName:@"acgc_metal_sink_vertex"];
                            pipeline_descriptor.fragmentFunction = [s_library
                                newFunctionWithName:@"acgc_metal_sink_fragment"];
                        }
                        pipeline_descriptor.depthAttachmentPixelFormat =
                            MTLPixelFormatDepth32Float;
                        color_attachment = pipeline_descriptor.colorAttachments[0];
                        color_attachment.pixelFormat = MTLPixelFormatRGBA8Unorm;
                        color_attachment.blendingEnabled =
                            output->state.blend.enabled != 0;
                        color_attachment.sourceRGBBlendFactor = metal_blend_factor(
                            output->state.blend.source_rgb_factor
                        );
                        color_attachment.destinationRGBBlendFactor = metal_blend_factor(
                            output->state.blend.destination_rgb_factor
                        );
                        color_attachment.sourceAlphaBlendFactor = metal_blend_factor(
                            output->state.blend.source_alpha_factor
                        );
                        color_attachment.destinationAlphaBlendFactor = metal_blend_factor(
                            output->state.blend.destination_alpha_factor
                        );
                        color_attachment.writeMask = output->alpha_write_enabled
                            ? (MTLColorWriteMaskRed |
                               MTLColorWriteMaskGreen |
                               MTLColorWriteMaskBlue |
                               MTLColorWriteMaskAlpha)
                            : (MTLColorWriteMaskRed |
                               MTLColorWriteMaskGreen |
                               MTLColorWriteMaskBlue);
                        color_attachment.rgbBlendOperation = metal_blend_operation(
                            output->state.blend.rgb_operation
                        );
                        color_attachment.alphaBlendOperation = metal_blend_operation(
                            output->state.blend.alpha_operation
                        );
                        pipeline = [s_device
                            newRenderPipelineStateWithDescriptor:pipeline_descriptor
                                                           error:&error];
                        depth_descriptor = [[MTLDepthStencilDescriptor alloc] init];
                        depth_descriptor.depthCompareFunction = metal_compare_function(
                            output->state.depth.compare_function
                        );
                        depth_descriptor.depthWriteEnabled =
                            output->state.depth.write_enabled != 0;
                        depth_state = [s_device
                            newDepthStencilStateWithDescriptor:depth_descriptor];

                        if (color_texture == nil || depth_texture == nil ||
                            (texture_replace
                                ? (texture_vertex_buffer == nil ||
                                   source_texture == nil || sampler_state == nil)
                                : (vertex_buffer == nil)) ||
                            transform_buffer == nil || pipeline == nil ||
                            depth_state == nil) {
                            status = ACGC_METAL_SINK_RESOURCE_FAILURE;
                        } else {
                            command_buffer = [s_command_queue commandBuffer];
                            encoder = [command_buffer
                                renderCommandEncoderWithDescriptor:render_pass];
                            if (command_buffer == nil || encoder == nil) {
                                status = ACGC_METAL_SINK_RESOURCE_FAILURE;
                            } else {
                                [encoder setViewport:viewport];
                                [encoder setScissorRect:scissor];
                                [encoder setDepthStencilState:depth_state];
                                [encoder setCullMode:metal_cull_mode(
                                    output->state.raster.cull_mode
                                )];
                                [encoder setFrontFacingWinding:metal_winding(
                                    output->state.raster.front_facing_winding
                                )];
                                [encoder setTriangleFillMode:metal_fill_mode(
                                    output->state.raster.triangle_fill_mode
                                )];
                                [encoder setRenderPipelineState:pipeline];
                                if (texture_replace) {
                                    [encoder setVertexBuffer:texture_vertex_buffer
                                                       offset:0
                                                      atIndex:0];
                                    [encoder setFragmentTexture:source_texture
                                                        atIndex:0];
                                    [encoder setFragmentSamplerState:sampler_state
                                                              atIndex:0];
                                } else {
                                    [encoder setVertexBuffer:vertex_buffer
                                                       offset:0
                                                      atIndex:0];
                                }
                                [encoder setVertexBuffer:transform_buffer
                                                   offset:0
                                                  atIndex:1];
                                [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                            vertexStart:(NSUInteger)output->geometry.draws[0].first_vertex
                                            vertexCount:(NSUInteger)output->geometry.draws[0].vertex_count];
                                [encoder endEncoding];
                                [command_buffer commit];
                                [command_buffer waitUntilCompleted];
                                if (command_buffer.status !=
                                    MTLCommandBufferStatusCompleted) {
                                    fprintf(stderr,
                                            "ACGC Metal sink command buffer failed: %s\n",
                                            error_description(command_buffer.error));
                                    status = ACGC_METAL_SINK_COMMAND_BUFFER_FAILURE;
                                } else {
                                    increment_counter(&s_sink_state.completed_count);
                                    [color_texture
                                        getBytes:readback
                                        bytesPerRow:(NSUInteger)bytes_per_row
                                        fromRegion:MTLRegionMake2D(
                                            0,
                                            0,
                                            (NSUInteger)width,
                                            (NSUInteger)height
                                        )
                                        mipmapLevel:0];
                                    for (byte_index = 0;
                                         byte_index < readback_byte_count;
                                         byte_index++) {
                                        checksum =
                                            (checksum ^ readback[byte_index]) *
                                            UINT32_C(16777619);
                                    }
                                    byte_index =
                                        (((size_t)height / 2) * (size_t)width +
                                         ((size_t)width / 2)) *
                                        (size_t)ACGC_METAL_SINK_RGBA8_BYTES_PER_PIXEL;
                                    pixel = ((uint32_t)readback[byte_index] << 24) |
                                        ((uint32_t)readback[byte_index + 1] << 16) |
                                        ((uint32_t)readback[byte_index + 2] << 8) |
                                        readback[byte_index + 3];
                                    atomic_store_explicit(
                                        &s_sink_state.last_pixel_rgba8,
                                        pixel,
                                        memory_order_relaxed
                                    );
                                    atomic_store_explicit(
                                        &s_sink_state.last_checksum,
                                        checksum,
                                        memory_order_relaxed
                                    );
                                    increment_counter(&s_sink_state.readback_count);
                                    status = ACGC_METAL_SINK_OK;
                                }
                            }
                        }
                    }
                }
            }
            free(readback);
            set_last_status(status);
        }
    }

    return status;
}

void acgc_metal_sink_get_snapshot(AcgcMetalSinkSnapshot* snapshot) {
    if (snapshot == NULL) {
        return;
    }

    snapshot->initialized = atomic_load_explicit(
        &s_sink_state.initialized,
        memory_order_acquire
    );
    snapshot->available = atomic_load_explicit(
        &s_sink_state.available,
        memory_order_acquire
    );
    snapshot->submit_count = atomic_load_explicit(
        &s_sink_state.submit_count,
        memory_order_relaxed
    );
    snapshot->completed_count = atomic_load_explicit(
        &s_sink_state.completed_count,
        memory_order_relaxed
    );
    snapshot->readback_count = atomic_load_explicit(
        &s_sink_state.readback_count,
        memory_order_relaxed
    );
    snapshot->last_status = atomic_load_explicit(
        &s_sink_state.last_status,
        memory_order_acquire
    );
    snapshot->last_pixel_rgba8 = atomic_load_explicit(
        &s_sink_state.last_pixel_rgba8,
        memory_order_relaxed
    );
    snapshot->last_checksum = atomic_load_explicit(
        &s_sink_state.last_checksum,
        memory_order_relaxed
    );
}
