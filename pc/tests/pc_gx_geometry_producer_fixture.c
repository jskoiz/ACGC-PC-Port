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

_Alignas(size_t) static uint8_t
    g_output[PC_GX_GEOMETRY_PRODUCER_MAX_SECTION_BYTES];
_Alignas(size_t) static uint8_t
    g_scratch[PC_GX_GEOMETRY_PRODUCER_MAX_SECTION_BYTES];

static uint32_t float_bits(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint32_t read_le32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static void init_dependencies(
    AcgcGxCanonicalGeometryDependencyResults* dependencies
) {
    memset(dependencies, 0, sizeof(*dependencies));
    dependencies->transform_valid = 1;
    dependencies->texgens_valid = 1;
    dependencies->transform_position_known_mask = UINT32_C(0x3FF);
    dependencies->transform_normal_known_mask = UINT32_C(0x3FF);
    dependencies->transform_current_position_known = 1;
    dependencies->transform_current_position_id = 0;
    dependencies->texgen_present_mask = 1;
    dependencies->texgen_ordinary_known_mask = UINT32_C(0x7FF);
    dependencies->texgen_selector[0] = 30;
}

static void init_batch(
    PCGXRawGeometryBatch* batch,
    uint32_t primitive,
    uint32_t vertex_count
) {
    uint32_t slot;

    memset(batch, 0, sizeof(*batch));
    batch->primitive = primitive;
    batch->vertex_count = vertex_count;
    batch->vtxfmt = 0;
    batch->expected_vertex_count = vertex_count;
    batch->active = 0;
    batch->known = 1;
    for (slot = 0; slot < PC_GX_MAX_ATTR; slot++) {
        batch->attr[slot].vcd_type = GX_NONE;
        batch->attr[slot].descriptor_known = 1;
    }
}

static void set_attribute(
    PCGXRawGeometryBatch* batch,
    uint32_t slot,
    uint32_t vcd_type,
    uint32_t vat_count,
    uint32_t vat_type,
    uint32_t vat_fraction,
    uint32_t word_count
) {
    PCGXRawGeometryAttribute* attribute = &batch->attr[slot];

    attribute->vcd_type = vcd_type;
    attribute->vat_count = vat_count;
    attribute->vat_type = vat_type;
    attribute->vat_fraction = vat_fraction;
    attribute->descriptor_known = 3;
    attribute->value_word_count = word_count;
    if (vcd_type == GX_INDEX8 || vcd_type == GX_INDEX16) {
        attribute->array_known =
            PC_GX_GEOMETRY_ARRAY_KNOWN |
            PC_GX_GEOMETRY_ARRAY_DATA_KNOWN;
        attribute->array_generation = 1;
        attribute->array_byte_size = 16384;
        attribute->array_stride = 64;
        attribute->index_stride = vcd_type == GX_INDEX8 ? 1 : 2;
    }
}

static void set_value(
    PCGXRawGeometryAttribute* attribute,
    uint32_t record,
    const uint32_t* words,
    uint32_t source_index
) {
    memcpy(attribute->value_words[record], words,
           attribute->value_word_count * sizeof(uint32_t));
    attribute->value_source_index[record] = source_index;
    attribute->value_known[record] = 1;
}

static void set_direct_values(
    PCGXRawGeometryAttribute* attribute,
    uint32_t value_count,
    const uint32_t (*words)[PC_GX_GEOMETRY_MAX_VALUE_WORDS]
) {
    uint32_t record;

    attribute->value_count = value_count;
    for (record = 0; record < value_count; record++) {
        set_value(attribute, record, words[record], 0);
    }
}

static void set_indexed_values(
    PCGXRawGeometryAttribute* attribute,
    uint32_t value_count,
    const uint32_t (*words)[PC_GX_GEOMETRY_MAX_VALUE_WORDS],
    const uint32_t* source_indices,
    uint32_t index_count,
    const uint32_t* indices
) {
    uint32_t record;
    uint32_t vertex;

    attribute->value_count = value_count;
    attribute->index_count = index_count;
    for (record = 0; record < value_count; record++) {
        set_value(attribute, record, words[record], source_indices[record]);
    }
    for (vertex = 0; vertex < index_count; vertex++) {
        attribute->index_values[vertex] = indices[vertex];
        attribute->source_indices[vertex] = source_indices[indices[vertex]];
        attribute->index_known[vertex] = 1;
    }
}

static int build(
    const PCGXRawGeometryBatch* batch,
    const AcgcGxCanonicalGeometryDependencyResults* dependencies,
    size_t* output_size
) {
    memset(g_output, 0xA5, sizeof(g_output));
    memset(g_scratch, 0x5A, sizeof(g_scratch));
    *output_size = 0;
    return pc_gx_geometry_build_canonical(
        batch,
        dependencies,
        g_output,
        sizeof(g_output),
        output_size,
        g_scratch,
        sizeof(g_scratch)
    );
}

static int test_direct_geometry(void) {
    PCGXRawGeometryBatch batch;
    AcgcGxCanonicalGeometryDependencyResults dependencies;
    uint32_t position[3][PC_GX_GEOMETRY_MAX_VALUE_WORDS] = {
        {float_bits(1.0f), float_bits(2.0f), float_bits(3.0f)},
        {float_bits(-1.0f), float_bits(0.5f), float_bits(4.0f)},
        {float_bits(0.0f), float_bits(8.0f), float_bits(-2.0f)}
    };
    uint32_t normal[3][PC_GX_GEOMETRY_MAX_VALUE_WORDS] = {
        {UINT32_C(0x7FFF), 0, 0},
        {0, UINT32_C(0x7FFF), 0},
        {0, 0, UINT32_C(0x7FFF)}
    };
    uint32_t color[3][PC_GX_GEOMETRY_MAX_VALUE_WORDS] = {
        {UINT32_C(0x12345678)},
        {UINT32_C(0x90ABCDEF)},
        {UINT32_C(0xFFEEDDCC)}
    };
    uint32_t texcoord[3][PC_GX_GEOMETRY_MAX_VALUE_WORDS] = {
        {UINT32_C(0x0002), UINT32_C(0xFFFE)},
        {UINT32_C(0x0004), UINT32_C(0x0006)},
        {UINT32_C(0xFFFC), UINT32_C(0x0008)}
    };
    size_t output_size;
    uint32_t descriptor_offset;

    init_batch(
        &batch,
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES,
        3
    );
    set_attribute(&batch, GX_VA_POS, GX_DIRECT, GX_POS_XYZ, GX_F32, 0, 3);
    set_attribute(&batch, GX_VA_NRM, GX_DIRECT, GX_NRM_XYZ, GX_S16, 0, 3);
    set_attribute(&batch, GX_VA_CLR0, GX_DIRECT, GX_CLR_RGBA, GX_RGBA8, 0, 1);
    set_attribute(&batch, GX_VA_TEX0, GX_DIRECT, GX_TEX_ST, GX_S16, 0, 2);
    set_direct_values(&batch.attr[GX_VA_POS], 3, position);
    set_direct_values(&batch.attr[GX_VA_NRM], 3, normal);
    set_direct_values(&batch.attr[GX_VA_CLR0], 3, color);
    set_direct_values(&batch.attr[GX_VA_TEX0], 3, texcoord);
    init_dependencies(&dependencies);

    CHECK(build(&batch, &dependencies, &output_size));
    CHECK(acgc_gx_canonical_geometry_state_validate(
        g_output, output_size));
    CHECK(acgc_gx_canonical_geometry_state_validate_dependencies(
        g_output, output_size, &dependencies));
    CHECK(read_le32(g_output + 0x10) ==
        ((UINT32_C(1) << GX_VA_POS) |
         (UINT32_C(1) << GX_VA_NRM) |
         (UINT32_C(1) << GX_VA_CLR0) |
         (UINT32_C(1) << GX_VA_TEX0)));
    descriptor_offset = read_le32(g_output + 0x18) +
        GX_VA_CLR0 * ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_SIZE;
    CHECK(read_le32(g_output + descriptor_offset + 0x20) == 4);
    return 0;
}

static int test_direct_quads_geometry(void) {
    PCGXRawGeometryBatch batch;
    AcgcGxCanonicalGeometryDependencyResults dependencies;
    uint32_t position[4][PC_GX_GEOMETRY_MAX_VALUE_WORDS] = {
        {float_bits(0.0f), float_bits(0.0f), float_bits(0.0f)},
        {float_bits(1.0f), float_bits(0.0f), float_bits(0.0f)},
        {float_bits(1.0f), float_bits(1.0f), float_bits(0.0f)},
        {float_bits(0.0f), float_bits(1.0f), float_bits(0.0f)}
    };
    size_t output_size;

    init_batch(
        &batch,
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS,
        4
    );
    set_attribute(&batch, GX_VA_POS, GX_DIRECT, GX_POS_XYZ, GX_F32, 0, 3);
    /* GX_NONE retains copied VAT/array sideband in a raw batch. */
    batch.attr[GX_VA_NRM].descriptor_known = 3;
    batch.attr[GX_VA_NRM].vat_count = GX_NRM_NBT;
    batch.attr[GX_VA_NRM].vat_type = GX_F32;
    batch.attr[GX_VA_NRM].vat_fraction = 7;
    batch.attr[GX_VA_NRM].array_known =
        PC_GX_GEOMETRY_ARRAY_KNOWN | PC_GX_GEOMETRY_ARRAY_DATA_KNOWN;
    batch.attr[GX_VA_NRM].array_generation = 9;
    batch.attr[GX_VA_NRM].array_byte_size = 128;
    batch.attr[GX_VA_NRM].array_stride = 16;
    set_direct_values(&batch.attr[GX_VA_POS], 4, position);
    init_dependencies(&dependencies);

    CHECK(build(&batch, &dependencies, &output_size));
    CHECK(read_le32(g_output +
        ACGC_GX_CANONICAL_GEOMETRY_HEADER_PRIMITIVE_OFFSET) ==
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS);
    CHECK(read_le32(g_output +
        ACGC_GX_CANONICAL_GEOMETRY_HEADER_VERTEX_COUNT_OFFSET) == 4);
    CHECK(acgc_gx_canonical_geometry_state_validate(
        g_output, output_size));
    CHECK(acgc_gx_canonical_geometry_state_validate_dependencies(
        g_output, output_size, &dependencies));
    return 0;
}

static int test_scalar_forms(void) {
    static const uint32_t types[] = {
        GX_U8, GX_S8, GX_U16, GX_S16, GX_F32
    };
    uint32_t type_index;

    for (type_index = 0;
         type_index < sizeof(types) / sizeof(types[0]);
         type_index++) {
        PCGXRawGeometryBatch batch;
        AcgcGxCanonicalGeometryDependencyResults dependencies;
        uint32_t values[3][PC_GX_GEOMETRY_MAX_VALUE_WORDS];
        size_t output_size;
        uint32_t type = types[type_index];
        uint32_t fraction = type == GX_F32 ? 0 : 1;

        memset(values, 0, sizeof(values));
        if (type == GX_U8) {
            values[0][0] = 2; values[0][1] = 4; values[0][2] = 6;
            values[1][0] = 8; values[1][1] = 10; values[1][2] = 12;
            values[2][0] = 14; values[2][1] = 16; values[2][2] = 18;
        } else if (type == GX_S8) {
            values[0][0] = UINT32_C(0xFE);
            values[0][1] = UINT32_C(0x02);
            values[0][2] = UINT32_C(0x04);
            values[1][0] = UINT32_C(0xFC);
            values[1][1] = UINT32_C(0x06);
            values[1][2] = UINT32_C(0x08);
            values[2][0] = UINT32_C(0xFA);
            values[2][1] = UINT32_C(0x0A);
            values[2][2] = UINT32_C(0x0C);
        } else if (type == GX_U16) {
            values[0][0] = 200; values[0][1] = 400; values[0][2] = 600;
            values[1][0] = 800; values[1][1] = 1000; values[1][2] = 1200;
            values[2][0] = 1400; values[2][1] = 1600; values[2][2] = 1800;
        } else if (type == GX_S16) {
            values[0][0] = UINT32_C(0xFF38);
            values[0][1] = UINT32_C(0x0064);
            values[0][2] = UINT32_C(0x00C8);
            values[1][0] = UINT32_C(0xFED0);
            values[1][1] = UINT32_C(0x012C);
            values[1][2] = UINT32_C(0x0190);
            values[2][0] = UINT32_C(0xFE68);
            values[2][1] = UINT32_C(0x01F4);
            values[2][2] = UINT32_C(0x0258);
        } else {
            values[0][0] = float_bits(1.25f);
            values[0][1] = float_bits(-2.5f);
            values[0][2] = float_bits(3.0f);
            values[1][0] = float_bits(4.0f);
            values[1][1] = float_bits(5.0f);
            values[1][2] = float_bits(6.0f);
            values[2][0] = float_bits(-7.0f);
            values[2][1] = float_bits(8.0f);
            values[2][2] = float_bits(9.0f);
        }
        init_batch(
            &batch,
            ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES,
            3
        );
        set_attribute(
            &batch, GX_VA_POS, GX_DIRECT, GX_POS_XYZ, type, fraction, 3);
        set_direct_values(&batch.attr[GX_VA_POS], 3, values);
        init_dependencies(&dependencies);
        CHECK(build(&batch, &dependencies, &output_size));
        CHECK(acgc_gx_canonical_geometry_state_validate(
            g_output, output_size));
    }
    return 0;
}

static int test_packed_colors(void) {
    static const uint32_t types[] = {
        GX_RGB565, GX_RGB8, GX_RGBX8, GX_RGBA4, GX_RGBA6, GX_RGBA8
    };
    static const uint32_t counts[] = {
        GX_CLR_RGB, GX_CLR_RGB, GX_CLR_RGB,
        GX_CLR_RGBA, GX_CLR_RGBA, GX_CLR_RGBA
    };
    static const uint32_t values[] = {
        UINT32_C(0xF81F), UINT32_C(0x123456), UINT32_C(0x123456AA),
        UINT32_C(0xF123), UINT32_C(0x00FFFFFF), UINT32_C(0x12345678)
    };
    uint32_t type_index;

    for (type_index = 0; type_index < 6; type_index++) {
        PCGXRawGeometryBatch batch;
        AcgcGxCanonicalGeometryDependencyResults dependencies;
        uint32_t position[3][PC_GX_GEOMETRY_MAX_VALUE_WORDS] = {
            {0, 0, 0}, {float_bits(1.0f), 0, 0},
            {0, float_bits(1.0f), 0}
        };
        uint32_t color[3][PC_GX_GEOMETRY_MAX_VALUE_WORDS];
        size_t output_size;
        uint32_t record;

        memset(color, 0, sizeof(color));
        for (record = 0; record < 3; record++) color[record][0] = values[type_index];
        init_batch(
            &batch,
            ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES,
            3
        );
        set_attribute(&batch, GX_VA_POS, GX_DIRECT, GX_POS_XYZ, GX_F32, 0, 3);
        set_attribute(
            &batch,
            GX_VA_CLR0,
            GX_DIRECT,
            counts[type_index],
            types[type_index],
            0,
            1
        );
        set_direct_values(&batch.attr[GX_VA_POS], 3, position);
        set_direct_values(&batch.attr[GX_VA_CLR0], 3, color);
        init_dependencies(&dependencies);
        CHECK(build(&batch, &dependencies, &output_size));
        CHECK(acgc_gx_canonical_geometry_state_validate(
            g_output, output_size));
    }
    return 0;
}

static int test_indexed_geometry(void) {
    PCGXRawGeometryBatch batch;
    AcgcGxCanonicalGeometryDependencyResults dependencies;
    uint32_t position[3][PC_GX_GEOMETRY_MAX_VALUE_WORDS] = {
        {100, 200, 300}, {400, 500, 600}, {700, 800, 900}
    };
    uint32_t normal[3][PC_GX_GEOMETRY_MAX_VALUE_WORDS] = {
        {UINT32_C(0x7F), 0, 0}, {0, UINT32_C(0x7F), 0},
        {0, 0, UINT32_C(0x7F)}
    };
    uint32_t color[3][PC_GX_GEOMETRY_MAX_VALUE_WORDS] = {
        {UINT32_C(0xF123)}, {UINT32_C(0x0F21)}, {UINT32_C(0x8ABC)}
    };
    uint32_t texcoord[3][PC_GX_GEOMETRY_MAX_VALUE_WORDS] = {
        {float_bits(0.0f), float_bits(1.0f)},
        {float_bits(2.0f), float_bits(3.0f)},
        {float_bits(4.0f), float_bits(5.0f)}
    };
    const uint32_t position_sources[] = {10, 20, 30};
    const uint32_t normal_sources[] = {40, 50, 60};
    const uint32_t color_sources[] = {70, 80, 90};
    const uint32_t texcoord_sources[] = {100, 110, 120};
    const uint32_t indices[] = {0, 1, 0, 2};
    size_t output_size;
    uint32_t indexed_mask;
    uint32_t descriptor_offset;
    uint32_t index_offset;
    uint32_t normal_descriptor_offset;
    uint32_t normal_index_offset;

    init_batch(&batch, ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS, 4);
    set_attribute(&batch, GX_VA_POS, GX_INDEX8, GX_POS_XYZ, GX_U16, 0, 3);
    set_attribute(&batch, GX_VA_NRM, GX_INDEX16, GX_NRM_XYZ, GX_S8, 0, 3);
    set_attribute(&batch, GX_VA_CLR0, GX_INDEX8, GX_CLR_RGBA, GX_RGBA4, 0, 1);
    set_attribute(&batch, GX_VA_TEX0, GX_INDEX16, GX_TEX_ST, GX_F32, 0, 2);
    set_indexed_values(
        &batch.attr[GX_VA_POS], 3, position, position_sources, 4, indices);
    set_indexed_values(
        &batch.attr[GX_VA_NRM], 3, normal, normal_sources, 4, indices);
    set_indexed_values(
        &batch.attr[GX_VA_CLR0], 3, color, color_sources, 4, indices);
    set_indexed_values(
        &batch.attr[GX_VA_TEX0], 3, texcoord, texcoord_sources, 4, indices);
    init_dependencies(&dependencies);

    CHECK(build(&batch, &dependencies, &output_size));
    CHECK(acgc_gx_canonical_geometry_state_validate(
        g_output, output_size));
    CHECK(acgc_gx_canonical_geometry_state_validate_dependencies(
        g_output, output_size, &dependencies));
    indexed_mask = read_le32(g_output + 0x14);
    CHECK(indexed_mask ==
        ((UINT32_C(1) << GX_VA_POS) |
         (UINT32_C(1) << GX_VA_NRM) |
         (UINT32_C(1) << GX_VA_CLR0) |
         (UINT32_C(1) << GX_VA_TEX0)));
    descriptor_offset = read_le32(g_output + 0x18) +
        GX_VA_POS * ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_SIZE;
    index_offset = read_le32(g_output + descriptor_offset + 0x28);
    CHECK(g_output[index_offset + 0] == 0);
    CHECK(g_output[index_offset + 1] == 1);
    CHECK(g_output[index_offset + 2] == 0);
    CHECK(g_output[index_offset + 3] == 2);
    normal_descriptor_offset = read_le32(g_output + 0x18) +
        GX_VA_NRM * ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_SIZE;
    normal_index_offset = read_le32(
        g_output + normal_descriptor_offset +
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_OFFSET);
    CHECK(g_output[normal_index_offset + 0] == 0);
    CHECK(g_output[normal_index_offset + 1] == 0);
    CHECK(g_output[normal_index_offset + 2] == 1);
    CHECK(g_output[normal_index_offset + 3] == 0);
    CHECK(g_output[normal_index_offset + 4] == 0);
    CHECK(g_output[normal_index_offset + 5] == 0);
    CHECK(g_output[normal_index_offset + 6] == 2);
    CHECK(g_output[normal_index_offset + 7] == 0);
    return 0;
}

static int expect_failure(
    const PCGXRawGeometryBatch* batch,
    const AcgcGxCanonicalGeometryDependencyResults* dependencies,
    size_t output_capacity,
    size_t scratch_capacity
) {
    size_t output_size = SIZE_MAX - 17;
    size_t index;

    memset(g_output, 0xA5, sizeof(g_output));
    memset(g_scratch, 0x5A, sizeof(g_scratch));
    CHECK(!pc_gx_geometry_build_canonical(
        batch,
        dependencies,
        g_output,
        output_capacity,
        &output_size,
        g_scratch,
        scratch_capacity
    ));
    CHECK(output_size == SIZE_MAX - 17);
    for (index = 0; index < sizeof(g_output); index++) {
        CHECK(g_output[index] == 0xA5);
    }
    return 0;
}

static void make_minimal_batch(PCGXRawGeometryBatch* batch);

static int expect_failure_with_overlapping_buffers(size_t scratch_offset) {
    PCGXRawGeometryBatch batch;
    AcgcGxCanonicalGeometryDependencyResults dependencies;
    size_t output_size = SIZE_MAX - 17;
    size_t index;

    CHECK(scratch_offset < sizeof(g_output));
    make_minimal_batch(&batch);
    init_dependencies(&dependencies);
    memset(g_output, 0xA5, sizeof(g_output));
    CHECK(!pc_gx_geometry_build_canonical(
        &batch,
        &dependencies,
        g_output,
        sizeof(g_output),
        &output_size,
        g_output + scratch_offset,
        sizeof(g_output) - scratch_offset
    ));
    CHECK(output_size == SIZE_MAX - 17);
    for (index = 0; index < sizeof(g_output); index++) {
        CHECK(g_output[index] == 0xA5);
    }
    return 0;
}

static int expect_failure_with_size_alias(size_t* aliased_output_size) {
    size_t before;
    size_t index;
    PCGXRawGeometryBatch batch;
    AcgcGxCanonicalGeometryDependencyResults dependencies;

    make_minimal_batch(&batch);
    init_dependencies(&dependencies);
    memset(g_output, 0xA5, sizeof(g_output));
    memset(g_scratch, 0x5A, sizeof(g_scratch));
    before = *aliased_output_size;
    CHECK(!pc_gx_geometry_build_canonical(
        &batch,
        &dependencies,
        g_output,
        sizeof(g_output),
        aliased_output_size,
        g_scratch,
        sizeof(g_scratch)
    ));
    CHECK(*aliased_output_size == before);
    for (index = 0; index < sizeof(g_output); index++) {
        CHECK(g_output[index] == 0xA5);
    }
    return 0;
}

static void make_minimal_batch(PCGXRawGeometryBatch* batch) {
    uint32_t position[3][PC_GX_GEOMETRY_MAX_VALUE_WORDS] = {
        {0, 0, 0}, {1, 1, 1}, {2, 2, 2}
    };

    init_batch(
        batch,
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES,
        3
    );
    set_attribute(batch, GX_VA_POS, GX_DIRECT, GX_POS_XYZ, GX_U8, 0, 3);
    set_direct_values(&batch->attr[GX_VA_POS], 3, position);
}

static void make_minimal_indexed_batch(PCGXRawGeometryBatch* batch) {
    uint32_t position[2][PC_GX_GEOMETRY_MAX_VALUE_WORDS] = {
        {0, 0, 0}, {1, 2, 3}
    };
    const uint32_t source_indices[] = {1, 2};
    const uint32_t indices[] = {0, 1, 0};

    init_batch(
        batch,
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES,
        3
    );
    set_attribute(batch, GX_VA_POS, GX_INDEX8, GX_POS_XYZ, GX_U8, 0, 3);
    set_indexed_values(
        &batch->attr[GX_VA_POS],
        2,
        position,
        source_indices,
        3,
        indices
    );
}

static int test_indexed_metadata_failures(void) {
    PCGXRawGeometryBatch batch;
    AcgcGxCanonicalGeometryDependencyResults dependencies;

    init_dependencies(&dependencies);
    make_minimal_indexed_batch(&batch);
    batch.attr[GX_VA_POS].index_known[0] = 2;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);

    make_minimal_indexed_batch(&batch);
    batch.attr[GX_VA_POS].index_known[3] = 2;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);

    make_minimal_indexed_batch(&batch);
    batch.attr[GX_VA_POS].array_generation = 0;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);

    make_minimal_indexed_batch(&batch);
    batch.attr[GX_VA_POS].array_byte_size = 64;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);

    make_minimal_indexed_batch(&batch);
    batch.attr[GX_VA_POS].array_stride = 2;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);

    make_minimal_indexed_batch(&batch);
    batch.attr[GX_VA_POS].value_source_index[0] = UINT32_MAX;
    batch.attr[GX_VA_POS].source_indices[0] = UINT32_MAX;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);

    make_minimal_indexed_batch(&batch);
    batch.attr[GX_VA_POS].index_values[0] = 2;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);

    make_minimal_indexed_batch(&batch);
    batch.attr[GX_VA_POS].source_indices[1] = 999;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);
    return 0;
}

static int test_fail_closed_and_atomic(void) {
    PCGXRawGeometryBatch batch;
    AcgcGxCanonicalGeometryDependencyResults dependencies;
    size_t output_size;

    make_minimal_batch(&batch);
    init_dependencies(&dependencies);

    batch.known = 0;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);
    make_minimal_batch(&batch);

    batch.attr[GX_VA_POS].value_known[0] = 2;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);
    make_minimal_batch(&batch);

    batch.attr[GX_VA_POS].value_source_index[0] = 1;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);
    make_minimal_batch(&batch);

    batch.attr[GX_VA_POS].value_known[3] = 2;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);
    make_minimal_batch(&batch);

    batch.attr[GX_VA_POS].value_words[3][0] = 1;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);
    make_minimal_batch(&batch);

    batch.attr[GX_VA_POS].index_values[3] = 1;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);
    make_minimal_batch(&batch);

    batch.attr[GX_VA_NRM].value_source_index[0] = 1;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);
    make_minimal_batch(&batch);

    batch.attr[GX_VA_NRM].value_words[0][0] = 1;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);
    make_minimal_batch(&batch);

    batch.attr[GX_VA_NRM].index_values[0] = 1;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);
    make_minimal_batch(&batch);

    batch.attr[GX_VA_NRM].source_indices[0] = 1;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);
    make_minimal_batch(&batch);

    batch.attr[GX_VA_NRM].value_known[0] = 1;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);
    make_minimal_batch(&batch);

    batch.attr[GX_VA_NRM].index_known[0] = 1;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);
    make_minimal_batch(&batch);

    batch.attr[GX_VA_NRM].reserved[0] = 1;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);
    make_minimal_batch(&batch);

    set_attribute(&batch, GX_VA_CLR1, GX_DIRECT, GX_CLR_RGBA, GX_RGBA8, 0, 1);
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);
    make_minimal_batch(&batch);

    batch.attr[GX_VA_POS].value_known[0] = 0;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);
    make_minimal_batch(&batch);

    batch.attr[GX_VA_POS].value_count = UINT32_MAX;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);
    make_minimal_batch(&batch);

    dependencies.transform_valid = 0;
    CHECK(expect_failure(
        &batch, &dependencies, sizeof(g_output), sizeof(g_scratch)) == 0);
    init_dependencies(&dependencies);

    CHECK(expect_failure(
        &batch, &dependencies,
        ACGC_GX_CANONICAL_GEOMETRY_MIN_SECTION_SIZE - 1,
        sizeof(g_scratch)) == 0);
    CHECK(expect_failure(
        &batch, &dependencies,
        sizeof(g_output),
        ACGC_GX_CANONICAL_GEOMETRY_MIN_SECTION_SIZE - 1) == 0);
    CHECK(expect_failure_with_overlapping_buffers(0) == 0);
    CHECK(expect_failure_with_overlapping_buffers(1) == 0);
    CHECK(expect_failure_with_size_alias(
        (size_t*)(void*)g_output) == 0);
    CHECK(expect_failure_with_size_alias(
        (size_t*)(void*)g_scratch) == 0);

    CHECK(build(&batch, &dependencies, &output_size));
    CHECK(acgc_gx_canonical_geometry_state_validate(
        g_output, output_size));
    g_output[ACGC_GX_CANONICAL_GEOMETRY_HEADER_VERTEX_COUNT_OFFSET] ^= 1;
    CHECK(!acgc_gx_canonical_geometry_state_validate(
        g_output, output_size));
    g_output[ACGC_GX_CANONICAL_GEOMETRY_HEADER_VERTEX_COUNT_OFFSET] ^= 1;
    CHECK(acgc_gx_canonical_geometry_state_validate(
        g_output, output_size));
    return 0;
}

int main(void) {
    CHECK(test_direct_geometry() == 0);
    CHECK(test_direct_quads_geometry() == 0);
    CHECK(test_scalar_forms() == 0);
    CHECK(test_packed_colors() == 0);
    CHECK(test_indexed_geometry() == 0);
    CHECK(test_indexed_metadata_failures() == 0);
    CHECK(test_fail_closed_and_atomic() == 0);
    puts("pc_gx_geometry_producer_fixture: PASS");
    return 0;
}
