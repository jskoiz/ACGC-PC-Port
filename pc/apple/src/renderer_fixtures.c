#include "acgc/renderer_fixtures.h"

#include <string.h>

typedef struct TextureLayout {
    uint32_t block_width;
    uint32_t block_height;
    uint32_t bits_per_pixel;
} TextureLayout;

static int texture_layout(uint32_t format, TextureLayout* layout) {
    if (layout == NULL) {
        return 0;
    }

    switch (format) {
        case ACGC_RENDERER_FIXTURE_TF_I4:
        case ACGC_RENDERER_FIXTURE_TF_C4:
            layout->block_width = 8;
            layout->block_height = 8;
            layout->bits_per_pixel = 4;
            return 1;
        case ACGC_RENDERER_FIXTURE_TF_I8:
        case ACGC_RENDERER_FIXTURE_TF_IA4:
        case ACGC_RENDERER_FIXTURE_TF_C8:
            layout->block_width = 8;
            layout->block_height = 4;
            layout->bits_per_pixel = 8;
            return 1;
        case ACGC_RENDERER_FIXTURE_TF_IA8:
        case ACGC_RENDERER_FIXTURE_TF_RGB565:
        case ACGC_RENDERER_FIXTURE_TF_RGB5A3:
        case ACGC_RENDERER_FIXTURE_TF_C14X2:
            layout->block_width = 4;
            layout->block_height = 4;
            layout->bits_per_pixel = 16;
            return 1;
        case ACGC_RENDERER_FIXTURE_TF_RGBA8:
            layout->block_width = 4;
            layout->block_height = 4;
            layout->bits_per_pixel = 32;
            return 1;
        case ACGC_RENDERER_FIXTURE_TF_CMPR:
            layout->block_width = 8;
            layout->block_height = 8;
            layout->bits_per_pixel = 4;
            return 1;
        default:
            return 0;
    }
}

static int texture_dimensions_are_valid(uint32_t width, uint32_t height) {
    /* The fixture deliberately keeps allocation arithmetic bounded. */
    return width != 0 && height != 0 && width <= 1024 && height <= 1024;
}

uint32_t acgc_renderer_fixture_texture_bytes(
    uint32_t width,
    uint32_t height,
    uint32_t format
) {
    TextureLayout layout;
    uint64_t blocks_x;
    uint64_t blocks_y;
    uint64_t bytes_per_block;
    uint64_t total;

    if (!texture_dimensions_are_valid(width, height) ||
        !texture_layout(format, &layout)) {
        return 0;
    }

    blocks_x = ((uint64_t)width + layout.block_width - 1) / layout.block_width;
    blocks_y = ((uint64_t)height + layout.block_height - 1) / layout.block_height;
    bytes_per_block =
        ((uint64_t)layout.block_width * layout.block_height * layout.bits_per_pixel) / 8;
    total = blocks_x * blocks_y * bytes_per_block;
    if (total > UINT32_MAX) {
        return 0;
    }
    return (uint32_t)total;
}

static uint32_t texture_output_bytes(uint32_t width, uint32_t height) {
    uint64_t total = (uint64_t)width * height * 4;

    return total > UINT32_MAX ? 0 : (uint32_t)total;
}

static int format_is_indexed(uint32_t format) {
    return format == ACGC_RENDERER_FIXTURE_TF_C4 ||
           format == ACGC_RENDERER_FIXTURE_TF_C8 ||
           format == ACGC_RENDERER_FIXTURE_TF_C14X2;
}

static uint32_t required_tlut_entries(uint32_t format) {
    switch (format) {
        case ACGC_RENDERER_FIXTURE_TF_C4:
            return 16;
        case ACGC_RENDERER_FIXTURE_TF_C8:
            return 256;
        case ACGC_RENDERER_FIXTURE_TF_C14X2:
            return ACGC_RENDERER_FIXTURE_MAX_TLUT_ENTRIES;
        default:
            return 0;
    }
}

static uint16_t read_u16(const uint8_t* bytes, uint32_t byte_order) {
    if (byte_order == ACGC_RENDERER_FIXTURE_BIG_ENDIAN) {
        return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
    }
    return (uint16_t)(((uint16_t)bytes[1] << 8) | bytes[0]);
}

static uint8_t expand_nibble(uint32_t value) {
    return (uint8_t)(value * 17);
}

static uint8_t expand_bits(uint32_t value, uint32_t max_value) {
    return (uint8_t)((value * 255) / max_value);
}

static void decode_rgb565(uint16_t value, AcgcRendererFixtureColor* color) {
    color->r = expand_bits((value >> 11) & 0x1F, 31);
    color->g = expand_bits((value >> 5) & 0x3F, 63);
    color->b = expand_bits(value & 0x1F, 31);
    color->a = 255;
}

static void decode_rgb5a3(uint16_t value, AcgcRendererFixtureColor* color) {
    if ((value & UINT16_C(0x8000)) != 0) {
        color->r = expand_bits((value >> 10) & 0x1F, 31);
        color->g = expand_bits((value >> 5) & 0x1F, 31);
        color->b = expand_bits(value & 0x1F, 31);
        color->a = 255;
    } else {
        color->a = expand_bits((value >> 12) & 0x07, 7);
        color->r = expand_nibble((value >> 8) & 0x0F);
        color->g = expand_nibble((value >> 4) & 0x0F);
        color->b = expand_nibble(value & 0x0F);
    }
}

static void set_pixel(
    uint8_t* output,
    uint32_t width,
    uint32_t height,
    uint32_t x,
    uint32_t y,
    AcgcRendererFixtureColor color
) {
    uint32_t offset;

    if (x >= width || y >= height) {
        return;
    }
    offset = (y * width + x) * 4;
    output[offset + 0] = color.r;
    output[offset + 1] = color.g;
    output[offset + 2] = color.b;
    output[offset + 3] = color.a;
}

static int decode_tlut_color(
    const AcgcRendererFixtureTextureDescription* description,
    const uint8_t* tlut_data,
    uint32_t index,
    AcgcRendererFixtureColor* color
) {
    uint16_t value;

    if (index >= description->tlut_entries || color == NULL) {
        return 0;
    }
    value = read_u16(
        tlut_data + (index * 2),
        description->tlut_byte_order
    );
    switch (description->tlut_format) {
        case ACGC_RENDERER_FIXTURE_TL_IA8:
            if (description->tlut_byte_order == ACGC_RENDERER_FIXTURE_BIG_ENDIAN) {
                color->r = color->g = color->b = (uint8_t)(value >> 8);
                color->a = (uint8_t)value;
            } else {
                color->r = color->g = color->b = (uint8_t)value;
                color->a = (uint8_t)(value >> 8);
            }
            return 1;
        case ACGC_RENDERER_FIXTURE_TL_RGB565:
            decode_rgb565(value, color);
            return 1;
        case ACGC_RENDERER_FIXTURE_TL_RGB5A3:
            decode_rgb5a3(value, color);
            return 1;
        default:
            return 0;
    }
}

static int decode_i4(
    const AcgcRendererFixtureTextureDescription* description,
    const uint8_t* data,
    uint8_t* output,
    int indexed,
    const uint8_t* tlut_data
) {
    uint32_t blocks_x = (description->width + 7) / 8;
    uint32_t blocks_y = (description->height + 7) / 8;
    uint32_t by;
    uint32_t bx;

    for (by = 0; by < blocks_y; by++) {
        for (bx = 0; bx < blocks_x; bx++) {
            const uint8_t* block = data + ((by * blocks_x + bx) * 32);
            uint32_t y;
            uint32_t x;

            for (y = 0; y < 8; y++) {
                for (x = 0; x < 8; x += 2) {
                    uint8_t packed = block[y * 4 + x / 2];
                    uint32_t indices[2] = { packed >> 4, packed & 0x0F };
                    uint32_t n;

                    for (n = 0; n < 2; n++) {
                        AcgcRendererFixtureColor color;
                        uint32_t px = bx * 8 + x + n;
                        uint32_t py = by * 8 + y;

                        if (indexed) {
                            if (!decode_tlut_color(
                                    description,
                                    tlut_data,
                                    indices[n],
                                    &color
                                )) {
                                return 0;
                            }
                        } else {
                            color.r = color.g = color.b =
                                expand_nibble(indices[n]);
                            color.a = color.r;
                        }
                        set_pixel(output, description->width, description->height,
                                  px, py, color);
                    }
                }
            }
        }
    }
    return 1;
}

static int decode_i8(
    const AcgcRendererFixtureTextureDescription* description,
    const uint8_t* data,
    uint8_t* output,
    int indexed,
    const uint8_t* tlut_data
) {
    uint32_t blocks_x = (description->width + 7) / 8;
    uint32_t blocks_y = (description->height + 3) / 4;
    uint32_t by;
    uint32_t bx;

    for (by = 0; by < blocks_y; by++) {
        for (bx = 0; bx < blocks_x; bx++) {
            const uint8_t* block = data + ((by * blocks_x + bx) * 32);
            uint32_t y;
            uint32_t x;

            for (y = 0; y < 4; y++) {
                for (x = 0; x < 8; x++) {
                    AcgcRendererFixtureColor color;
                    uint32_t index = block[y * 8 + x];
                    uint32_t px = bx * 8 + x;
                    uint32_t py = by * 4 + y;

                    if (indexed) {
                        if (!decode_tlut_color(description, tlut_data, index, &color)) {
                            return 0;
                        }
                    } else {
                        color.r = color.g = color.b = (uint8_t)index;
                        color.a = (uint8_t)index;
                    }
                    set_pixel(output, description->width, description->height,
                              px, py, color);
                }
            }
        }
    }
    return 1;
}

static int decode_ia4(
    const AcgcRendererFixtureTextureDescription* description,
    const uint8_t* data,
    uint8_t* output
) {
    uint32_t blocks_x = (description->width + 7) / 8;
    uint32_t blocks_y = (description->height + 3) / 4;
    uint32_t by;
    uint32_t bx;

    for (by = 0; by < blocks_y; by++) {
        for (bx = 0; bx < blocks_x; bx++) {
            const uint8_t* block = data + ((by * blocks_x + bx) * 32);
            uint32_t y;
            uint32_t x;

            for (y = 0; y < 4; y++) {
                for (x = 0; x < 8; x++) {
                    uint8_t value = block[y * 8 + x];
                    AcgcRendererFixtureColor color;

                    color.r = color.g = color.b = expand_nibble(value & 0x0F);
                    color.a = expand_nibble(value >> 4);
                    set_pixel(output, description->width, description->height,
                              bx * 8 + x, by * 4 + y, color);
                }
            }
        }
    }
    return 1;
}

static int decode_ia8(
    const AcgcRendererFixtureTextureDescription* description,
    const uint8_t* data,
    uint8_t* output
) {
    uint32_t blocks_x = (description->width + 3) / 4;
    uint32_t blocks_y = (description->height + 3) / 4;
    uint32_t by;
    uint32_t bx;

    for (by = 0; by < blocks_y; by++) {
        for (bx = 0; bx < blocks_x; bx++) {
            const uint8_t* block = data + ((by * blocks_x + bx) * 32);
            uint32_t i;

            for (i = 0; i < 16; i++) {
                AcgcRendererFixtureColor color;
                uint16_t value = read_u16(
                    block + i * 2,
                    description->data_byte_order
                );

                if (description->data_byte_order == ACGC_RENDERER_FIXTURE_BIG_ENDIAN) {
                    color.a = (uint8_t)(value >> 8);
                    color.r = color.g = color.b = (uint8_t)value;
                } else {
                    color.a = (uint8_t)value;
                    color.r = color.g = color.b = (uint8_t)(value >> 8);
                }
                set_pixel(output, description->width, description->height,
                          bx * 4 + (i % 4), by * 4 + (i / 4), color);
            }
        }
    }
    return 1;
}

static int decode_rgb16(
    const AcgcRendererFixtureTextureDescription* description,
    const uint8_t* data,
    uint8_t* output,
    uint32_t format
) {
    uint32_t blocks_x = (description->width + 3) / 4;
    uint32_t blocks_y = (description->height + 3) / 4;
    uint32_t by;
    uint32_t bx;

    for (by = 0; by < blocks_y; by++) {
        for (bx = 0; bx < blocks_x; bx++) {
            const uint8_t* block = data + ((by * blocks_x + bx) * 32);
            uint32_t i;

            for (i = 0; i < 16; i++) {
                AcgcRendererFixtureColor color;
                uint16_t value = read_u16(
                    block + i * 2,
                    description->data_byte_order
                );

                if (format == ACGC_RENDERER_FIXTURE_TF_RGB565) {
                    decode_rgb565(value, &color);
                } else {
                    decode_rgb5a3(value, &color);
                }
                set_pixel(output, description->width, description->height,
                          bx * 4 + (i % 4), by * 4 + (i / 4), color);
            }
        }
    }
    return 1;
}

static int decode_rgba8(
    const AcgcRendererFixtureTextureDescription* description,
    const uint8_t* data,
    uint8_t* output
) {
    uint32_t blocks_x = (description->width + 3) / 4;
    uint32_t blocks_y = (description->height + 3) / 4;
    uint32_t by;
    uint32_t bx;

    for (by = 0; by < blocks_y; by++) {
        for (bx = 0; bx < blocks_x; bx++) {
            const uint8_t* block = data + ((by * blocks_x + bx) * 64);
            uint32_t i;

            /* GX RGBA8 stores the AR plane followed by the GB plane. */
            for (i = 0; i < 16; i++) {
                AcgcRendererFixtureColor color;
                color.a = block[i * 2 + 0];
                color.r = block[i * 2 + 1];
                color.g = block[32 + i * 2 + 0];
                color.b = block[32 + i * 2 + 1];
                set_pixel(output, description->width, description->height,
                          bx * 4 + (i % 4), by * 4 + (i / 4), color);
            }
        }
    }
    return 1;
}

static int decode_cmpr(
    const AcgcRendererFixtureTextureDescription* description,
    const uint8_t* data,
    uint8_t* output
) {
    uint32_t blocks_x = (description->width + 7) / 8;
    uint32_t blocks_y = (description->height + 7) / 8;
    uint32_t by;
    uint32_t bx;

    for (by = 0; by < blocks_y; by++) {
        for (bx = 0; bx < blocks_x; bx++) {
            const uint8_t* block = data + ((by * blocks_x + bx) * 32);
            uint32_t sub;

            for (sub = 0; sub < 4; sub++) {
                AcgcRendererFixtureColor palette[4];
                const uint8_t* sub_block = block + sub * 8;
                uint16_t c0 = read_u16(sub_block, description->data_byte_order);
                uint16_t c1 = read_u16(sub_block + 2, description->data_byte_order);
                uint32_t sx = (sub & 1) * 4;
                uint32_t sy = (sub >> 1) * 4;
                uint32_t y;

                decode_rgb565(c0, &palette[0]);
                decode_rgb565(c1, &palette[1]);
                if (c0 > c1) {
                    palette[2].r = (uint8_t)((2 * palette[0].r + palette[1].r) / 3);
                    palette[2].g = (uint8_t)((2 * palette[0].g + palette[1].g) / 3);
                    palette[2].b = (uint8_t)((2 * palette[0].b + palette[1].b) / 3);
                    palette[2].a = 255;
                    palette[3].r = (uint8_t)((palette[0].r + 2 * palette[1].r) / 3);
                    palette[3].g = (uint8_t)((palette[0].g + 2 * palette[1].g) / 3);
                    palette[3].b = (uint8_t)((palette[0].b + 2 * palette[1].b) / 3);
                    palette[3].a = 255;
                } else {
                    palette[2].r = (uint8_t)((palette[0].r + palette[1].r) / 2);
                    palette[2].g = (uint8_t)((palette[0].g + palette[1].g) / 2);
                    palette[2].b = (uint8_t)((palette[0].b + palette[1].b) / 2);
                    palette[2].a = 255;
                    palette[3].r = palette[3].g = palette[3].b = 0;
                    palette[3].a = 0;
                }

                for (y = 0; y < 4; y++) {
                    uint8_t row = sub_block[4 + y];
                    uint32_t x;

                    for (x = 0; x < 4; x++) {
                        uint32_t index = (row >> (6 - x * 2)) & 3;
                        set_pixel(output, description->width, description->height,
                                  bx * 8 + sx + x, by * 8 + sy + y,
                                  palette[index]);
                    }
                }
            }
        }
    }
    return 1;
}

static int decode_c14x2(
    const AcgcRendererFixtureTextureDescription* description,
    const uint8_t* data,
    const uint8_t* tlut_data,
    uint8_t* output
) {
    uint32_t blocks_x = (description->width + 3) / 4;
    uint32_t blocks_y = (description->height + 3) / 4;
    uint32_t by;
    uint32_t bx;

    for (by = 0; by < blocks_y; by++) {
        for (bx = 0; bx < blocks_x; bx++) {
            const uint8_t* block = data + ((by * blocks_x + bx) * 32);
            uint32_t i;

            for (i = 0; i < 16; i++) {
                AcgcRendererFixtureColor color;
                uint32_t index = read_u16(
                    block + i * 2,
                    description->data_byte_order
                ) & UINT32_C(0x3FFF);

                if (!decode_tlut_color(description, tlut_data, index, &color)) {
                    return 0;
                }
                set_pixel(output, description->width, description->height,
                          bx * 4 + (i % 4), by * 4 + (i / 4), color);
            }
        }
    }
    return 1;
}

int acgc_renderer_fixture_decode_texture(
    const AcgcRendererFixtureTextureDescription* description,
    const uint8_t* data,
    const uint8_t* tlut_data,
    uint8_t* rgba_output,
    uint32_t rgba_capacity
) {
    TextureLayout layout;
    uint32_t source_bytes;
    uint32_t output_bytes;
    uint32_t palette_entries;

    if (description == NULL || data == NULL || rgba_output == NULL ||
        description->version != ACGC_RENDERER_FIXTURE_VERSION ||
        !texture_dimensions_are_valid(description->width, description->height) ||
        (description->data_byte_order != ACGC_RENDERER_FIXTURE_BIG_ENDIAN &&
         description->data_byte_order != ACGC_RENDERER_FIXTURE_LITTLE_ENDIAN) ||
        !texture_layout(description->format, &layout)) {
        return 0;
    }

    source_bytes = acgc_renderer_fixture_texture_bytes(
        description->width,
        description->height,
        description->format
    );
    output_bytes = texture_output_bytes(description->width, description->height);
    if (source_bytes == 0 || output_bytes == 0 ||
        description->data_size < source_bytes || rgba_capacity < output_bytes) {
        return 0;
    }

    if (format_is_indexed(description->format)) {
        palette_entries = required_tlut_entries(description->format);
        if (description->tlut_format > ACGC_RENDERER_FIXTURE_TL_RGB5A3 ||
            description->tlut_byte_order > ACGC_RENDERER_FIXTURE_LITTLE_ENDIAN ||
            description->tlut_entries < palette_entries ||
            description->tlut_entries > ACGC_RENDERER_FIXTURE_MAX_TLUT_ENTRIES ||
            description->tlut_data_size < description->tlut_entries * 2 ||
            tlut_data == NULL) {
            return 0;
        }
    }

    memset(rgba_output, 0, output_bytes);
    switch (description->format) {
        case ACGC_RENDERER_FIXTURE_TF_I4:
            return decode_i4(description, data, rgba_output, 0, NULL);
        case ACGC_RENDERER_FIXTURE_TF_I8:
            return decode_i8(description, data, rgba_output, 0, NULL);
        case ACGC_RENDERER_FIXTURE_TF_IA4:
            return decode_ia4(description, data, rgba_output);
        case ACGC_RENDERER_FIXTURE_TF_IA8:
            return decode_ia8(description, data, rgba_output);
        case ACGC_RENDERER_FIXTURE_TF_RGB565:
        case ACGC_RENDERER_FIXTURE_TF_RGB5A3:
            return decode_rgb16(description, data, rgba_output, description->format);
        case ACGC_RENDERER_FIXTURE_TF_RGBA8:
            return decode_rgba8(description, data, rgba_output);
        case ACGC_RENDERER_FIXTURE_TF_C4:
            return decode_i4(description, data, rgba_output, 1, tlut_data);
        case ACGC_RENDERER_FIXTURE_TF_C8:
            return decode_i8(description, data, rgba_output, 1, tlut_data);
        case ACGC_RENDERER_FIXTURE_TF_C14X2:
            return decode_c14x2(description, data, tlut_data, rgba_output);
        case ACGC_RENDERER_FIXTURE_TF_CMPR:
            return decode_cmpr(description, data, rgba_output);
        default:
            return 0;
    }
}

int acgc_renderer_fixture_resolve_sampler(
    const AcgcRendererFixtureSamplerDescription* description,
    AcgcRendererFixtureSamplerState* state
) {
    if (description == NULL || state == NULL ||
        description->version != ACGC_RENDERER_FIXTURE_VERSION ||
        description->wrap_s > ACGC_RENDERER_FIXTURE_WRAP_MIRROR ||
        description->wrap_t > ACGC_RENDERER_FIXTURE_WRAP_MIRROR ||
        description->min_filter > ACGC_RENDERER_FIXTURE_FILTER_LINEAR_MIP_LINEAR ||
        description->mag_filter > ACGC_RENDERER_FIXTURE_FILTER_LINEAR_MIP_LINEAR ||
        description->filtering_enabled > 1) {
        return 0;
    }

    state->address_s = description->wrap_s;
    state->address_t = description->wrap_t;
    if (description->filtering_enabled == 0) {
        /* Match the existing PC setting override without needing mip data. */
        state->min_filter = ACGC_RENDERER_FIXTURE_FILTER_NEAREST;
        state->mag_filter = ACGC_RENDERER_FIXTURE_FILTER_NEAREST;
        return 1;
    }

    if (description->min_filter > ACGC_RENDERER_FIXTURE_FILTER_LINEAR ||
        description->mag_filter > ACGC_RENDERER_FIXTURE_FILTER_LINEAR) {
        /* This fixture owns base-level filters only; mip chains are separate. */
        return 0;
    }
    state->min_filter = description->min_filter;
    state->mag_filter = description->mag_filter;
    return 1;
}

#define TEV_Q_ONE 256
#define TEV_Q_HALF 128

typedef struct TevQColor {
    int32_t r;
    int32_t g;
    int32_t b;
    int32_t a;
} TevQColor;

static int32_t byte_to_q(uint8_t value) {
    return ((int32_t)value * TEV_Q_ONE + 127) / 255;
}

static uint8_t q_to_byte(int32_t value) {
    if (value <= 0) {
        return 0;
    }
    if (value >= TEV_Q_ONE) {
        return 255;
    }
    return (uint8_t)((value * 255 + 128) / TEV_Q_ONE);
}

static int32_t round_q_div(int64_t value) {
    if (value >= 0) {
        return (int32_t)((value + TEV_Q_HALF) / TEV_Q_ONE);
    }
    return -(int32_t)((-value + TEV_Q_HALF) / TEV_Q_ONE);
}

static TevQColor q_color_from_color(AcgcRendererFixtureColor color) {
    TevQColor result;
    result.r = byte_to_q(color.r);
    result.g = byte_to_q(color.g);
    result.b = byte_to_q(color.b);
    result.a = byte_to_q(color.a);
    return result;
}

static AcgcRendererFixtureColor color_from_q_color(TevQColor color) {
    AcgcRendererFixtureColor result;
    result.r = q_to_byte(color.r);
    result.g = q_to_byte(color.g);
    result.b = q_to_byte(color.b);
    result.a = q_to_byte(color.a);
    return result;
}

static int32_t q_lerp(int32_t a, int32_t b, int32_t factor) {
    return a + round_q_div((int64_t)(b - a) * factor);
}

static int32_t q_clamp(int32_t value) {
    if (value < 0) {
        return 0;
    }
    if (value > TEV_Q_ONE) {
        return TEV_Q_ONE;
    }
    return value;
}

static int32_t q_apply_bias_scale(
    int32_t value,
    uint32_t bias,
    uint32_t scale
) {
    switch (bias) {
        case ACGC_RENDERER_FIXTURE_TEV_BIAS_ADD_HALF:
            value += TEV_Q_HALF;
            break;
        case ACGC_RENDERER_FIXTURE_TEV_BIAS_SUB_HALF:
            value -= TEV_Q_HALF;
            break;
        default:
            break;
    }
    switch (scale) {
        case ACGC_RENDERER_FIXTURE_TEV_SCALE_TWO:
            value *= 2;
            break;
        case ACGC_RENDERER_FIXTURE_TEV_SCALE_FOUR:
            value *= 4;
            break;
        case ACGC_RENDERER_FIXTURE_TEV_SCALE_HALF:
            if (value >= 0) {
                value = (value + 1) / 2;
            } else {
                value = -((-value + 1) / 2);
            }
            break;
        default:
            break;
    }
    return value;
}

static int validate_konst_selection(uint32_t selection) {
    return selection <= 31;
}

static int32_t q_konst_scalar(
    uint32_t selection,
    const TevQColor* konst
) {
    uint32_t color_index;
    uint32_t channel;

    if (selection <= 7) {
        return (8 - (int32_t)selection) * 32;
    }
    if (selection <= 15) {
        return 0;
    }
    if (selection <= 15) {
        color_index = selection - 12;
        return konst[color_index].r;
    }
    color_index = (selection - 16) & 3;
    channel = (selection - 16) >> 2;
    switch (channel) {
        case 0:
            return konst[color_index].r;
        case 1:
            return konst[color_index].g;
        case 2:
            return konst[color_index].b;
        default:
            return konst[color_index].a;
    }
}

static TevQColor q_konst_color(
    uint32_t selection,
    const TevQColor* konst
) {
    TevQColor result;

    if (selection <= 7) {
        result.r = result.g = result.b = (8 - (int32_t)selection) * 32;
    } else if (selection <= 11) {
        result.r = result.g = result.b = 0;
    } else if (selection <= 15) {
        result = konst[selection - 12];
    } else {
        result.r = result.g = result.b = q_konst_scalar(selection, konst);
    }
    result.a = 0;
    return result;
}

static int select_color(
    uint32_t selection,
    const TevQColor* prev,
    const TevQColor* reg0,
    const TevQColor* reg1,
    const TevQColor* reg2,
    const TevQColor* texture,
    const TevQColor* raster,
    const TevQColor* konst,
    TevQColor* result
) {
    if (selection > ACGC_RENDERER_FIXTURE_CC_ZERO || result == NULL) {
        return 0;
    }
    switch (selection) {
        case ACGC_RENDERER_FIXTURE_CC_CPREV:
            result->r = prev->r; result->g = prev->g; result->b = prev->b;
            break;
        case ACGC_RENDERER_FIXTURE_CC_APREV:
            result->r = result->g = result->b = prev->a;
            break;
        case ACGC_RENDERER_FIXTURE_CC_C0:
            result->r = reg0->r; result->g = reg0->g; result->b = reg0->b;
            break;
        case ACGC_RENDERER_FIXTURE_CC_A0:
            result->r = result->g = result->b = reg0->a;
            break;
        case ACGC_RENDERER_FIXTURE_CC_C1:
            result->r = reg1->r; result->g = reg1->g; result->b = reg1->b;
            break;
        case ACGC_RENDERER_FIXTURE_CC_A1:
            result->r = result->g = result->b = reg1->a;
            break;
        case ACGC_RENDERER_FIXTURE_CC_C2:
            result->r = reg2->r; result->g = reg2->g; result->b = reg2->b;
            break;
        case ACGC_RENDERER_FIXTURE_CC_A2:
            result->r = result->g = result->b = reg2->a;
            break;
        case ACGC_RENDERER_FIXTURE_CC_TEXC:
            result->r = texture->r; result->g = texture->g; result->b = texture->b;
            break;
        case ACGC_RENDERER_FIXTURE_CC_TEXA:
            result->r = result->g = result->b = texture->a;
            break;
        case ACGC_RENDERER_FIXTURE_CC_RASC:
            result->r = raster->r; result->g = raster->g; result->b = raster->b;
            break;
        case ACGC_RENDERER_FIXTURE_CC_RASA:
            result->r = result->g = result->b = raster->a;
            break;
        case ACGC_RENDERER_FIXTURE_CC_ONE:
            result->r = result->g = result->b = TEV_Q_ONE;
            break;
        case ACGC_RENDERER_FIXTURE_CC_HALF:
            result->r = result->g = result->b = TEV_Q_HALF;
            break;
        case ACGC_RENDERER_FIXTURE_CC_KONST:
            *result = *konst;
            break;
        default:
            result->r = result->g = result->b = 0;
            break;
    }
    return 1;
}

static int select_alpha(
    uint32_t selection,
    const TevQColor* prev,
    const TevQColor* reg0,
    const TevQColor* reg1,
    const TevQColor* reg2,
    const TevQColor* texture,
    const TevQColor* raster,
    int32_t konst,
    int32_t* result
) {
    if (selection > ACGC_RENDERER_FIXTURE_CA_ZERO || result == NULL) {
        return 0;
    }
    switch (selection) {
        case ACGC_RENDERER_FIXTURE_CA_APREV:
            *result = prev->a;
            break;
        case ACGC_RENDERER_FIXTURE_CA_A0:
            *result = reg0->a;
            break;
        case ACGC_RENDERER_FIXTURE_CA_A1:
            *result = reg1->a;
            break;
        case ACGC_RENDERER_FIXTURE_CA_A2:
            *result = reg2->a;
            break;
        case ACGC_RENDERER_FIXTURE_CA_TEXA:
            *result = texture->a;
            break;
        case ACGC_RENDERER_FIXTURE_CA_RASA:
            *result = raster->a;
            break;
        case ACGC_RENDERER_FIXTURE_CA_KONST:
            *result = konst;
            break;
        default:
            *result = 0;
            break;
    }
    return 1;
}

static int stage_field_values_are_valid(const AcgcRendererFixtureTevStage* stage) {
    return stage->color_op <= ACGC_RENDERER_FIXTURE_TEV_SUB &&
           stage->color_bias <= ACGC_RENDERER_FIXTURE_TEV_BIAS_SUB_HALF &&
           stage->color_scale <= ACGC_RENDERER_FIXTURE_TEV_SCALE_HALF &&
           stage->color_clamp <= 1 &&
           stage->color_out <= ACGC_RENDERER_FIXTURE_TEV_REG2 &&
           stage->alpha_op <= ACGC_RENDERER_FIXTURE_TEV_SUB &&
           stage->alpha_bias <= ACGC_RENDERER_FIXTURE_TEV_BIAS_SUB_HALF &&
           stage->alpha_scale <= ACGC_RENDERER_FIXTURE_TEV_SCALE_HALF &&
           stage->alpha_clamp <= 1 &&
           stage->alpha_out <= ACGC_RENDERER_FIXTURE_TEV_REG2 &&
           stage->color_a <= ACGC_RENDERER_FIXTURE_CC_ZERO &&
           stage->color_b <= ACGC_RENDERER_FIXTURE_CC_ZERO &&
           stage->color_c <= ACGC_RENDERER_FIXTURE_CC_ZERO &&
           stage->color_d <= ACGC_RENDERER_FIXTURE_CC_ZERO &&
           stage->alpha_a <= ACGC_RENDERER_FIXTURE_CA_ZERO &&
           stage->alpha_b <= ACGC_RENDERER_FIXTURE_CA_ZERO &&
           stage->alpha_c <= ACGC_RENDERER_FIXTURE_CA_ZERO &&
           stage->alpha_d <= ACGC_RENDERER_FIXTURE_CA_ZERO &&
           validate_konst_selection(stage->konst_color_sel) &&
           validate_konst_selection(stage->konst_alpha_sel);
}

static void write_tev_register(
    uint32_t output_register,
    TevQColor value,
    TevQColor* prev,
    TevQColor* reg0,
    TevQColor* reg1,
    TevQColor* reg2,
    int write_color,
    int write_alpha
) {
    TevQColor* destination;

    switch (output_register) {
        case ACGC_RENDERER_FIXTURE_TEV_PREV:
            destination = prev;
            break;
        case ACGC_RENDERER_FIXTURE_TEV_REG0:
            destination = reg0;
            break;
        case ACGC_RENDERER_FIXTURE_TEV_REG1:
            destination = reg1;
            break;
        default:
            destination = reg2;
            break;
    }
    if (write_color) {
        destination->r = value.r;
        destination->g = value.g;
        destination->b = value.b;
    }
    if (write_alpha) {
        destination->a = value.a;
    }
}

int acgc_renderer_fixture_tev_evaluate(
    const AcgcRendererFixtureTevState* state,
    AcgcRendererFixtureColor* output
) {
    TevQColor prev;
    TevQColor reg0;
    TevQColor reg1;
    TevQColor reg2;
    TevQColor texture[ACGC_RENDERER_FIXTURE_MAX_TEV_STAGES];
    TevQColor raster;
    TevQColor konst[4];
    uint32_t stage_index;

    if (state == NULL || output == NULL ||
        state->version != ACGC_RENDERER_FIXTURE_VERSION ||
        state->stage_count > ACGC_RENDERER_FIXTURE_MAX_TEV_STAGES) {
        return 0;
    }
    for (stage_index = 0; stage_index < state->stage_count; stage_index++) {
        if (!stage_field_values_are_valid(&state->stages[stage_index])) {
            return 0;
        }
    }

    prev = q_color_from_color(state->prev);
    reg0 = q_color_from_color(state->reg0);
    reg1 = q_color_from_color(state->reg1);
    reg2 = q_color_from_color(state->reg2);
    raster = q_color_from_color(state->raster);
    for (stage_index = 0; stage_index < ACGC_RENDERER_FIXTURE_MAX_TEV_STAGES; stage_index++) {
        texture[stage_index] = q_color_from_color(state->texture[stage_index]);
    }
    for (stage_index = 0; stage_index < 4; stage_index++) {
        konst[stage_index] = q_color_from_color(state->konst[stage_index]);
    }

    for (stage_index = 0; stage_index < state->stage_count; stage_index++) {
        const AcgcRendererFixtureTevStage* stage = &state->stages[stage_index];
        TevQColor konst_color = q_konst_color(stage->konst_color_sel, konst);
        int32_t konst_alpha = q_konst_scalar(stage->konst_alpha_sel, konst);
        TevQColor ca;
        TevQColor cb;
        TevQColor cc;
        TevQColor cd;
        TevQColor color_result;
        int32_t aa;
        int32_t ab;
        int32_t ac;
        int32_t ad;
        int32_t alpha_result;

        if (!select_color(stage->color_a, &prev, &reg0, &reg1, &reg2,
                          &texture[stage_index], &raster, &konst_color, &ca) ||
            !select_color(stage->color_b, &prev, &reg0, &reg1, &reg2,
                          &texture[stage_index], &raster, &konst_color, &cb) ||
            !select_color(stage->color_c, &prev, &reg0, &reg1, &reg2,
                          &texture[stage_index], &raster, &konst_color, &cc) ||
            !select_color(stage->color_d, &prev, &reg0, &reg1, &reg2,
                          &texture[stage_index], &raster, &konst_color, &cd) ||
            !select_alpha(stage->alpha_a, &prev, &reg0, &reg1, &reg2,
                          &texture[stage_index], &raster, konst_alpha, &aa) ||
            !select_alpha(stage->alpha_b, &prev, &reg0, &reg1, &reg2,
                          &texture[stage_index], &raster, konst_alpha, &ab) ||
            !select_alpha(stage->alpha_c, &prev, &reg0, &reg1, &reg2,
                          &texture[stage_index], &raster, konst_alpha, &ac) ||
            !select_alpha(stage->alpha_d, &prev, &reg0, &reg1, &reg2,
                          &texture[stage_index], &raster, konst_alpha, &ad)) {
            return 0;
        }

        color_result.r = q_lerp(ca.r, cb.r, cc.r);
        color_result.g = q_lerp(ca.g, cb.g, cc.g);
        color_result.b = q_lerp(ca.b, cb.b, cc.b);
        if (stage->color_op == ACGC_RENDERER_FIXTURE_TEV_SUB) {
            color_result.r = cd.r - color_result.r;
            color_result.g = cd.g - color_result.g;
            color_result.b = cd.b - color_result.b;
        } else {
            color_result.r += cd.r;
            color_result.g += cd.g;
            color_result.b += cd.b;
        }
        color_result.r = q_apply_bias_scale(
            color_result.r, stage->color_bias, stage->color_scale
        );
        color_result.g = q_apply_bias_scale(
            color_result.g, stage->color_bias, stage->color_scale
        );
        color_result.b = q_apply_bias_scale(
            color_result.b, stage->color_bias, stage->color_scale
        );

        alpha_result = q_lerp(aa, ab, ac);
        if (stage->alpha_op == ACGC_RENDERER_FIXTURE_TEV_SUB) {
            alpha_result = ad - alpha_result;
        } else {
            alpha_result += ad;
        }
        alpha_result = q_apply_bias_scale(
            alpha_result, stage->alpha_bias, stage->alpha_scale
        );
        color_result.a = alpha_result;

        if (stage->color_clamp != 0) {
            color_result.r = q_clamp(color_result.r);
            color_result.g = q_clamp(color_result.g);
            color_result.b = q_clamp(color_result.b);
        }
        if (stage->alpha_clamp != 0) {
            color_result.a = q_clamp(color_result.a);
        }
        write_tev_register(
            stage->color_out,
            color_result,
            &prev,
            &reg0,
            &reg1,
            &reg2,
            1,
            0
        );
        write_tev_register(
            stage->alpha_out,
            color_result,
            &prev,
            &reg0,
            &reg1,
            &reg2,
            0,
            1
        );
    }

    *output = color_from_q_color(prev);
    return 1;
}

#undef TEV_Q_ONE
#undef TEV_Q_HALF
