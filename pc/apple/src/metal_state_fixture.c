#include "acgc/metal_state_fixture.h"

#include <math.h>
#include <string.h>

static float float_from_bits(uint32_t bits) {
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int finite_float_bits(uint32_t bits) {
    return isfinite(float_from_bits(bits)) != 0;
}

static int blend_factor_is_valid(uint32_t factor) {
    switch (factor) {
        case ACGC_METAL_BLEND_ZERO:
        case ACGC_METAL_BLEND_ONE:
        case ACGC_METAL_BLEND_SOURCE_ALPHA:
        case ACGC_METAL_BLEND_ONE_MINUS_SOURCE_ALPHA:
        case ACGC_METAL_BLEND_DESTINATION_COLOR:
        case ACGC_METAL_BLEND_ONE_MINUS_DESTINATION_COLOR:
        case ACGC_METAL_BLEND_SOURCE_COLOR:
        case ACGC_METAL_BLEND_ONE_MINUS_SOURCE_COLOR:
        case ACGC_METAL_BLEND_DESTINATION_ALPHA:
        case ACGC_METAL_BLEND_ONE_MINUS_DESTINATION_ALPHA:
            return 1;
    }
    return 0;
}

static int blend_operation_is_valid(uint32_t operation) {
    return operation == ACGC_METAL_BLEND_ADD ||
           operation == ACGC_METAL_BLEND_REVERSE_SUBTRACT;
}

int acgc_metal_state_fixture_validate(const AcgcMetalStateFixture* fixture) {
    uint32_t matrix_index;

    if (fixture == NULL ||
        fixture->version != ACGC_METAL_STATE_FIXTURE_VERSION ||
        fixture->reserved != 0) {
        return 0;
    }

    for (matrix_index = 0;
         matrix_index < ACGC_METAL_FIXED_TRANSFORM_WORDS;
         matrix_index++) {
        if (!finite_float_bits(fixture->transform.matrix[matrix_index])) {
            return 0;
        }
    }

    if (!finite_float_bits(fixture->viewport.origin_x) ||
        !finite_float_bits(fixture->viewport.origin_y) ||
        !finite_float_bits(fixture->viewport.width) ||
        !finite_float_bits(fixture->viewport.height) ||
        !finite_float_bits(fixture->viewport.znear) ||
        !finite_float_bits(fixture->viewport.zfar) ||
        float_from_bits(fixture->viewport.origin_x) < 0.0f ||
        float_from_bits(fixture->viewport.origin_y) < 0.0f ||
        float_from_bits(fixture->viewport.width) <= 0.0f ||
        float_from_bits(fixture->viewport.height) <= 0.0f ||
        float_from_bits(fixture->viewport.znear) < 0.0f ||
        float_from_bits(fixture->viewport.zfar) > 1.0f ||
        float_from_bits(fixture->viewport.znear) >=
            float_from_bits(fixture->viewport.zfar)) {
        return 0;
    }

    if (fixture->depth.compare_function > ACGC_METAL_DEPTH_ALWAYS ||
        fixture->depth.write_enabled > 1) {
        return 0;
    }

    if (fixture->blend.enabled > 1 ||
        !blend_factor_is_valid(fixture->blend.source_rgb_factor) ||
        !blend_factor_is_valid(fixture->blend.destination_rgb_factor) ||
        !blend_factor_is_valid(fixture->blend.source_alpha_factor) ||
        !blend_factor_is_valid(fixture->blend.destination_alpha_factor) ||
        !blend_operation_is_valid(fixture->blend.rgb_operation) ||
        !blend_operation_is_valid(fixture->blend.alpha_operation)) {
        return 0;
    }

    if (fixture->raster.cull_mode > ACGC_METAL_CULL_BACK ||
        fixture->raster.front_facing_winding >
            ACGC_METAL_WINDING_COUNTER_CLOCKWISE ||
        fixture->raster.triangle_fill_mode > ACGC_METAL_TRIANGLE_LINES) {
        return 0;
    }
    return 1;
}

int acgc_metal_state_fixture_make(AcgcMetalStateFixture* fixture) {
    if (fixture == NULL) {
        return 0;
    }
    memset(fixture, 0, sizeof(*fixture));
    fixture->version = ACGC_METAL_STATE_FIXTURE_VERSION;

    /* Identity in column-major form, translated by (+1/8, -1/4, 0). */
    fixture->transform.matrix[0] = ACGC_METAL_FLOAT_ONE;
    fixture->transform.matrix[5] = ACGC_METAL_FLOAT_ONE;
    fixture->transform.matrix[10] = ACGC_METAL_FLOAT_ONE;
    fixture->transform.matrix[12] = ACGC_METAL_FLOAT_EIGHTH;
    fixture->transform.matrix[13] = ACGC_METAL_FLOAT_NEGATIVE_QUARTER;
    fixture->transform.matrix[15] = ACGC_METAL_FLOAT_ONE;

    fixture->viewport.origin_x = ACGC_METAL_FLOAT_ZERO;
    fixture->viewport.origin_y = ACGC_METAL_FLOAT_ZERO;
    fixture->viewport.width = ACGC_METAL_FLOAT_SIXTY_FOUR;
    fixture->viewport.height = ACGC_METAL_FLOAT_SIXTY_FOUR;
    fixture->viewport.znear = ACGC_METAL_FLOAT_ZERO;
    fixture->viewport.zfar = ACGC_METAL_FLOAT_ONE;

    fixture->depth.compare_function = ACGC_METAL_DEPTH_LESS_EQUAL;
    fixture->depth.write_enabled = 1;

    fixture->blend.enabled = 1;
    fixture->blend.source_rgb_factor = ACGC_METAL_BLEND_SOURCE_ALPHA;
    fixture->blend.destination_rgb_factor =
        ACGC_METAL_BLEND_ONE_MINUS_SOURCE_ALPHA;
    fixture->blend.source_alpha_factor = ACGC_METAL_BLEND_ONE;
    fixture->blend.destination_alpha_factor =
        ACGC_METAL_BLEND_ONE_MINUS_SOURCE_ALPHA;
    fixture->blend.rgb_operation = ACGC_METAL_BLEND_ADD;
    fixture->blend.alpha_operation = ACGC_METAL_BLEND_ADD;

    fixture->raster.cull_mode = ACGC_METAL_CULL_BACK;
    fixture->raster.front_facing_winding =
        ACGC_METAL_WINDING_COUNTER_CLOCKWISE;
    fixture->raster.triangle_fill_mode = ACGC_METAL_TRIANGLE_FILL;

    return acgc_metal_state_fixture_validate(fixture);
}
