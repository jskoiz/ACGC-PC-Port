#include "pc_gx_geometry_dependencies.h"
#include "pc_gx_geometry_producer.h"

#include <dolphin/gx/GXEnum.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static uint32_t float_bits(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void init_batch(PCGXRawGeometryBatch* batch) {
    uint32_t slot;

    memset(batch, 0, sizeof(*batch));
    batch->primitive = ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES;
    batch->vertex_count = 3;
    batch->vtxfmt = 0;
    batch->expected_vertex_count = 3;
    batch->known = 1;
    for (slot = 0; slot < PC_GX_MAX_ATTR; slot++) {
        batch->attr[slot].vcd_type = GX_NONE;
        batch->attr[slot].descriptor_known = 1;
    }
}

static void set_direct_attribute(
    PCGXRawGeometryBatch* batch,
    uint32_t slot,
    uint32_t count,
    uint32_t type,
    uint32_t fraction,
    uint32_t word_count,
    const uint32_t (*values)[PC_GX_GEOMETRY_MAX_VALUE_WORDS]
) {
    PCGXRawGeometryAttribute* attribute = &batch->attr[slot];
    uint32_t record;

    attribute->vcd_type = GX_DIRECT;
    attribute->vat_count = count;
    attribute->vat_type = type;
    attribute->vat_fraction = fraction;
    attribute->descriptor_known = 3;
    attribute->value_word_count = word_count;
    attribute->value_count = batch->vertex_count;
    for (record = 0; record < batch->vertex_count; record++) {
        memcpy(attribute->value_words[record], values[record],
               sizeof(attribute->value_words[record]));
        attribute->value_known[record] = 1;
    }
}

static void fill_transform(AcgcGxCanonicalTransformState* state) {
    memset(state, 0, sizeof(*state));
    state->projection_type =
        ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE;
    state->projection[0] = float_bits(1.0f);
    state->known_mask =
        ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_KNOWN_MASK |
        ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_KNOWN_MASK |
        ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(0) |
        ACGC_GX_CANONICAL_TRANSFORM_NORMAL_KNOWN_MASK(0);
    state->current_position_id = 0;
    state->position[0][0] = float_bits(1.0f);
    state->position[0][5] = float_bits(1.0f);
    state->position[0][10] = float_bits(1.0f);
    state->normal[0][0] = float_bits(1.0f);
    state->normal[0][4] = float_bits(1.0f);
    state->normal[0][8] = float_bits(1.0f);
}

static void fill_texgen(AcgcGxCanonicalTexgenState* state) {
    uint32_t index;

    memset(state, 0, sizeof(*state));
    state->header.active_texgen_count = 1;
    state->header.texgen_capacity = ACGC_GX_CANONICAL_TEXGEN_CAPACITY;
    state->header.known_texgen_count = 1;
    state->header.ordinary_matrix_count =
        ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_COUNT;
    state->header.ordinary_matrix_capacity =
        ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_CAPACITY;
    state->header.post_matrix_count =
        ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_COUNT;
    state->header.post_matrix_capacity =
        ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_CAPACITY;
    state->header.su_capacity = ACGC_GX_CANONICAL_TEXGEN_SU_CAPACITY;
    state->header.texgen_known_mask = 1;
    state->header.ordinary_matrix_known_mask =
        ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_KNOWN_MASK;
    state->header.post_matrix_known_mask =
        ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_KNOWN_MASK;
    state->header.component_known_summary =
        ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_ORDINARY_MATRIX |
        ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_POST_MATRIX;
    state->texgen[0].function =
        ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX3X4;
    state->texgen[0].source = ACGC_GX_CANONICAL_TEXGEN_SOURCE_POS;
    state->texgen[0].ordinary_matrix_id =
        ACGC_GX_CANONICAL_TEXGEN_ORDINARY_LOGICAL_ID(0);
    state->texgen[0].post_matrix_id =
        ACGC_GX_CANONICAL_TEXGEN_POST_LOGICAL_ID(0);
    state->texgen[0].component_known =
        ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL;

    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_COUNT;
         index++) {
        state->ordinary_matrix[index].logical_id =
            ACGC_GX_CANONICAL_TEXGEN_ORDINARY_LOGICAL_ID(index);
        state->ordinary_matrix[index].last_load_type =
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX3X4;
        state->ordinary_matrix[index].last_written_word_count =
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_3X4;
        state->ordinary_matrix[index].known_word_mask =
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4;
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_COUNT;
         index++) {
        state->post_matrix[index].logical_id =
            ACGC_GX_CANONICAL_TEXGEN_POST_LOGICAL_ID(index);
        state->post_matrix[index].last_load_type =
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX3X4;
        state->post_matrix[index].last_written_word_count =
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_3X4;
        state->post_matrix[index].known_word_mask =
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4;
    }
}

static void fill_bump_texgen(AcgcGxCanonicalTexgenState* state) {
    fill_texgen(state);
    state->header.active_texgen_count = 2;
    state->header.known_texgen_count = 2;
    state->header.texgen_known_mask = UINT32_C(0x03);
    state->texgen[1].function = ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP0;
    state->texgen[1].source =
        ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD0;
    state->texgen[1].ordinary_matrix_id =
        ACGC_GX_CANONICAL_TEXGEN_ORDINARY_LOGICAL_ID(1);
    state->texgen[1].post_matrix_id =
        ACGC_GX_CANONICAL_TEXGEN_POST_LOGICAL_ID(1);
    state->texgen[1].component_known =
        ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL;
}

static void fill_channels(AcgcGxCanonicalChannelState* state) {
    memset(state, 0, sizeof(*state));
    state->active_count = 1;
    state->record_valid_mask = 1;
    state->records[0].channel_index = 0;
    state->records[0].color.enable = 1;
    state->records[0].color.ambient_source =
        ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX;
    state->records[0].color.material_source =
        ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX;
    state->records[0].color.light_mask = 1;
    state->records[0].color.diffuse_function =
        ACGC_GX_CANONICAL_CHANNEL_DIFFUSE_NONE;
    state->records[0].color.attenuation_function =
        ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_NONE;
    state->records[0].alpha.enable = 1;
    state->records[0].alpha.ambient_source =
        ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX;
    state->records[0].alpha.material_source =
        ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX;
    state->records[0].alpha.light_mask = 1;
    state->records[0].alpha.diffuse_function =
        ACGC_GX_CANONICAL_CHANNEL_DIFFUSE_NONE;
    state->records[0].alpha.attenuation_function =
        ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_NONE;
}

static void fill_lighting(AcgcGxCanonicalLightingState* state) {
    memset(state, 0, sizeof(*state));
    state->loaded_mask = 1;
}

static void add_attributes(PCGXRawGeometryBatch* batch) {
    static const uint32_t position[3][PC_GX_GEOMETRY_MAX_VALUE_WORDS] = {
        {UINT32_C(0x00000000), UINT32_C(0x00000000), UINT32_C(0x00000000)},
        {UINT32_C(0x3F800000), UINT32_C(0x00000000), UINT32_C(0x00000000)},
        {UINT32_C(0x00000000), UINT32_C(0x3F800000), UINT32_C(0x00000000)}
    };
    static const uint32_t normal[3][PC_GX_GEOMETRY_MAX_VALUE_WORDS] = {
        {UINT32_C(0x00000000), UINT32_C(0x00000000), UINT32_C(0x3F800000)},
        {UINT32_C(0x00000000), UINT32_C(0x00000000), UINT32_C(0x3F800000)},
        {UINT32_C(0x00000000), UINT32_C(0x00000000), UINT32_C(0x3F800000)}
    };
    static const uint32_t color[3][PC_GX_GEOMETRY_MAX_VALUE_WORDS] = {
        {UINT32_C(0xFF0000FF)},
        {UINT32_C(0x00FF00FF)},
        {UINT32_C(0x0000FFFF)}
    };
    static const uint32_t texcoord[3][PC_GX_GEOMETRY_MAX_VALUE_WORDS] = {
        {UINT32_C(0x00000000), UINT32_C(0x00000000)},
        {UINT32_C(0x3F800000), UINT32_C(0x00000000)},
        {UINT32_C(0x00000000), UINT32_C(0x3F800000)}
    };

    set_direct_attribute(batch, GX_VA_POS, GX_POS_XYZ, GX_F32, 0, 3,
                         position);
    set_direct_attribute(batch, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0, 3,
                         normal);
    set_direct_attribute(batch, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0, 1,
                         color);
    set_direct_attribute(batch, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0, 2,
                         texcoord);
}

static void add_texcoord_one(PCGXRawGeometryBatch* batch) {
    static const uint32_t texcoord[3][PC_GX_GEOMETRY_MAX_VALUE_WORDS] = {
        {UINT32_C(0x00000000), UINT32_C(0x00000000)},
        {UINT32_C(0x3F800000), UINT32_C(0x00000000)},
        {UINT32_C(0x00000000), UINT32_C(0x3F800000)}
    };

    set_direct_attribute(batch, GX_VA_TEX1, GX_TEX_ST, GX_F32, 0, 2,
                         texcoord);
}

static int test_valid_result(void) {
    PCGXRawGeometryBatch batch;
    AcgcGxCanonicalTransformState transform;
    AcgcGxCanonicalTexgenState texgens;
    AcgcGxCanonicalChannelState channels;
    AcgcGxCanonicalLightingState lighting;
    AcgcGxCanonicalGeometryDependencyResults result;
    static uint8_t geometry_output[
        PC_GX_GEOMETRY_PRODUCER_MAX_SECTION_BYTES];
    static uint8_t geometry_scratch[
        PC_GX_GEOMETRY_PRODUCER_MAX_SECTION_BYTES];
    size_t geometry_size = 0;
    const uint32_t present_mask =
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0);

    init_batch(&batch);
    add_attributes(&batch);
    fill_transform(&transform);
    fill_texgen(&texgens);
    fill_channels(&channels);
    fill_lighting(&lighting);
    memset(&result, 0xA5, sizeof(result));

    CHECK(pc_gx_geometry_build_dependency_results(
        &batch, &transform, &texgens, &channels, &lighting, &result));
    CHECK(result.transform_valid == 1);
    CHECK(result.texgens_valid == 1);
    CHECK(result.channels_valid == 1);
    CHECK(result.lighting_valid == 1);
    CHECK(result.bump_valid == 0);
    CHECK(result.required_geometry_present_mask == present_mask);
    CHECK(result.required_channel_mask == 1);
    CHECK(result.required_lighting_mask == 1);
    CHECK(result.required_bump_mask == 0);
    CHECK(result.transform_position_known_mask == 1);
    CHECK(result.transform_normal_known_mask == 1);
    CHECK(result.transform_current_position_known == 1);
    CHECK(result.transform_current_position_id == 0);
    CHECK(result.texgen_present_mask == 1);
    CHECK(result.texgen_ordinary_known_mask ==
        ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_KNOWN_MASK);
    CHECK(result.texgen_post_known_mask ==
        ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_KNOWN_MASK);
    CHECK(result.texgen_selector[0] ==
        ACGC_GX_CANONICAL_TEXGEN_ORDINARY_LOGICAL_ID(0));
    CHECK(result.lighting_loaded_mask == 1);
    CHECK(result.reserved0 == 0);
    CHECK(result.reserved[0] == 0 && result.reserved[1] == 0 &&
          result.reserved[2] == 0 && result.reserved[3] == 0);
    CHECK(pc_gx_geometry_build_canonical(
        &batch,
        &result,
        geometry_output,
        sizeof(geometry_output),
        &geometry_size,
        geometry_scratch,
        sizeof(geometry_scratch)));
    CHECK(acgc_gx_canonical_geometry_state_validate(
        geometry_output, geometry_size));
    CHECK(acgc_gx_canonical_geometry_state_validate_dependencies(
        geometry_output, geometry_size, &result));
    return 0;
}

static int test_failure_is_atomic(void) {
    PCGXRawGeometryBatch batch;
    AcgcGxCanonicalTransformState transform;
    AcgcGxCanonicalTexgenState texgens;
    AcgcGxCanonicalChannelState channels;
    AcgcGxCanonicalLightingState lighting;
    AcgcGxCanonicalGeometryDependencyResults result;
    AcgcGxCanonicalGeometryDependencyResults before;

    init_batch(&batch);
    add_attributes(&batch);
    fill_transform(&transform);
    fill_texgen(&texgens);
    fill_channels(&channels);
    fill_lighting(&lighting);

    memset(&result, 0xC3, sizeof(result));
    before = result;
    transform.known_mask &=
        ~ACGC_GX_CANONICAL_TRANSFORM_NORMAL_KNOWN_MASK(0);
    memset(transform.normal[0], 0, sizeof(transform.normal[0]));
    CHECK(!pc_gx_geometry_build_dependency_results(
        &batch, &transform, &texgens, &channels, &lighting, &result));
    CHECK(memcmp(&result, &before, sizeof(result)) == 0);

    return 0;
}

static int test_bump_fails_closed(void) {
    PCGXRawGeometryBatch batch;
    AcgcGxCanonicalTransformState transform;
    AcgcGxCanonicalTexgenState texgens;
    AcgcGxCanonicalChannelState channels;
    AcgcGxCanonicalLightingState lighting;
    AcgcGxCanonicalGeometryDependencyResults result;
    AcgcGxCanonicalGeometryDependencyResults before;

    init_batch(&batch);
    add_attributes(&batch);
    add_texcoord_one(&batch);
    fill_transform(&transform);
    fill_bump_texgen(&texgens);
    fill_channels(&channels);
    fill_lighting(&lighting);
    CHECK(acgc_gx_canonical_texgen_state_validate(&texgens));

    memset(&result, 0x7E, sizeof(result));
    before = result;
    CHECK(!pc_gx_geometry_build_dependency_results(
        &batch, &transform, &texgens, &channels, &lighting, &result));
    CHECK(memcmp(&result, &before, sizeof(result)) == 0);
    return 0;
}

int main(void) {
    CHECK(test_valid_result() == 0);
    CHECK(test_failure_is_atomic() == 0);
    CHECK(test_bump_fails_closed() == 0);
    puts("pc_gx_geometry_dependencies_fixture: PASS");
    return 0;
}
