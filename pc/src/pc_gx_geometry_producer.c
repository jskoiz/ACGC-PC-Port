#include "pc_gx_geometry_producer.h"

#include <dolphin/gx/GXEnum.h>

#include <stdint.h>
#include <string.h>

enum {
    PC_GX_GEOMETRY_PRODUCER_DESCRIPTOR_VCD_KNOWN = 1,
    PC_GX_GEOMETRY_PRODUCER_DESCRIPTOR_VAT_KNOWN = 2,
    PC_GX_GEOMETRY_PRODUCER_DESCRIPTOR_KNOWN = 3
};

typedef struct {
    uint32_t present;
    uint32_t indexed;
    uint32_t canonical_word_count;
    uint32_t value_offset;
    uint32_t value_bytes;
    uint32_t value_stride;
    uint32_t value_count;
    uint32_t index_offset;
    uint32_t index_bytes;
    uint32_t index_stride;
    uint32_t index_count;
} PCGXGeometryProducerAttribute;

static int producer_u64_add(uint64_t left, uint64_t right, uint64_t* result) {
    if (result == NULL || left > UINT64_MAX - right) return 0;
    *result = left + right;
    return 1;
}

static int producer_u64_mul(uint64_t left, uint64_t right, uint64_t* result) {
    if (result == NULL || (right != 0 && left > UINT64_MAX / right)) return 0;
    *result = left * right;
    return 1;
}

static int producer_u64_align4(uint64_t value, uint64_t* result) {
    if (result == NULL || value > UINT64_MAX - UINT64_C(3)) return 0;
    *result = (value + UINT64_C(3)) & ~UINT64_C(3);
    return 1;
}

static int producer_u64_to_u32(uint64_t value, uint32_t* result) {
    if (result == NULL || value > UINT32_MAX) return 0;
    *result = (uint32_t)value;
    return 1;
}

static int producer_u64_to_size(uint64_t value, size_t* result) {
    if (result == NULL || value > (uint64_t)SIZE_MAX) return 0;
    *result = (size_t)value;
    return 1;
}

static int producer_buffer_range_is_valid(
    const void* pointer,
    size_t capacity,
    uintptr_t* begin,
    uintptr_t* end
) {
    uintptr_t start;

    if (pointer == NULL || capacity == 0 || begin == NULL || end == NULL) {
        return 0;
    }
    start = (uintptr_t)pointer;
    if (capacity > UINTPTR_MAX - start) return 0;
    *begin = start;
    *end = start + capacity;
    return 1;
}

static int producer_buffers_are_disjoint(
    const uint8_t* output,
    size_t output_capacity,
    const uint8_t* scratch,
    size_t scratch_capacity
) {
    uintptr_t output_begin;
    uintptr_t output_end;
    uintptr_t scratch_begin;
    uintptr_t scratch_end;

    if (!producer_buffer_range_is_valid(
            output, output_capacity, &output_begin, &output_end) ||
        !producer_buffer_range_is_valid(
            scratch, scratch_capacity, &scratch_begin, &scratch_end)) {
        return 0;
    }
    return output_end <= scratch_begin || scratch_end <= output_begin;
}

static int producer_output_size_is_disjoint(
    const size_t* output_size,
    const uint8_t* output,
    size_t output_capacity,
    const uint8_t* scratch,
    size_t scratch_capacity
) {
    uintptr_t size_begin;
    uintptr_t size_end;
    uintptr_t output_begin;
    uintptr_t output_end;
    uintptr_t scratch_begin;
    uintptr_t scratch_end;

    if (!producer_buffer_range_is_valid(
            output_size, sizeof(*output_size), &size_begin, &size_end) ||
        !producer_buffer_range_is_valid(
            output, output_capacity, &output_begin, &output_end) ||
        !producer_buffer_range_is_valid(
            scratch, scratch_capacity, &scratch_begin, &scratch_end)) {
        return 0;
    }
    return (size_end <= output_begin || output_end <= size_begin) &&
        (size_end <= scratch_begin || scratch_end <= size_begin);
}

static int producer_slot_is_supported(uint32_t slot) {
    return slot == GX_VA_POS || slot == GX_VA_NRM ||
        slot == GX_VA_CLR0 || slot == GX_VA_TEX0;
}

static int producer_vcd_is_indexed(uint32_t vcd_type) {
    return vcd_type == GX_INDEX8 || vcd_type == GX_INDEX16;
}

static int producer_descriptor_format_is_valid(
    uint32_t slot,
    const PCGXRawGeometryAttribute* attribute
) {
    uint32_t type = attribute->vat_type;
    uint32_t count = attribute->vat_count;

    if (slot == GX_VA_POS) {
        return (count == GX_POS_XY || count == GX_POS_XYZ) &&
            type <= GX_F32 &&
            (type == GX_F32 ? attribute->vat_fraction == 0 :
             attribute->vat_fraction <= 31);
    }
    if (slot == GX_VA_NRM) {
        return count == GX_NRM_XYZ &&
            (type == GX_S8 || type == GX_S16 || type == GX_F32) &&
            attribute->vat_fraction == 0;
    }
    if (slot == GX_VA_CLR0) {
        if (attribute->vat_fraction != 0) return 0;
        if (count == GX_CLR_RGB) {
            return type == GX_RGB565 || type == GX_RGB8 || type == GX_RGBX8;
        }
        if (count == GX_CLR_RGBA) {
            return type == GX_RGBA4 || type == GX_RGBA6 || type == GX_RGBA8;
        }
        return 0;
    }
    if (slot == GX_VA_TEX0) {
        return (count == GX_TEX_S || count == GX_TEX_ST) &&
            type <= GX_F32 &&
            (type == GX_F32 ? attribute->vat_fraction == 0 :
             attribute->vat_fraction <= 31);
    }
    return 0;
}

static uint32_t producer_expected_word_count(uint32_t slot) {
    if (slot == GX_VA_POS || slot == GX_VA_NRM) return 3;
    if (slot == GX_VA_CLR0) return 1;
    if (slot == GX_VA_TEX0) return 2;
    return 0;
}

static uint32_t producer_scalar_bytes(uint32_t vat_type) {
    if (vat_type == GX_U8 || vat_type == GX_S8) return 1;
    if (vat_type == GX_U16 || vat_type == GX_S16) return 2;
    if (vat_type == GX_F32) return 4;
    return 0;
}

static uint32_t producer_array_value_bytes(
    uint32_t slot,
    const PCGXRawGeometryAttribute* attribute
) {
    uint32_t components;
    uint32_t scalar_bytes;

    if (slot == GX_VA_CLR0) {
        if (attribute->vat_type == GX_RGB565 ||
            attribute->vat_type == GX_RGBA4) return 2;
        if (attribute->vat_type == GX_RGB8 ||
            attribute->vat_type == GX_RGBA6) return 3;
        if (attribute->vat_type == GX_RGBX8 ||
            attribute->vat_type == GX_RGBA8) return 4;
        return 0;
    }
    scalar_bytes = producer_scalar_bytes(attribute->vat_type);
    if (slot == GX_VA_POS) {
        components = attribute->vat_count == GX_POS_XY ? 2 : 3;
    } else if (slot == GX_VA_NRM) {
        components = 3;
    } else if (slot == GX_VA_TEX0) {
        components = attribute->vat_count == GX_TEX_S ? 1 : 2;
    } else {
        return 0;
    }
    if (scalar_bytes == 0 || components > UINT32_MAX / scalar_bytes) {
        return 0;
    }
    return components * scalar_bytes;
}

static int producer_value_is_canonicalizable(
    uint32_t slot,
    const PCGXRawGeometryAttribute* attribute,
    const uint32_t* raw_words,
    uint32_t* canonical_words
) {
    uint32_t word;
    uint32_t component_count;

    if (raw_words == NULL || canonical_words == NULL ||
        attribute->value_word_count != producer_expected_word_count(slot)) {
        return 0;
    }
    for (word = attribute->value_word_count;
         word < PC_GX_GEOMETRY_MAX_VALUE_WORDS;
         word++) {
        if (raw_words[word] != 0) return 0;
    }
    memset(canonical_words, 0,
           PC_GX_GEOMETRY_MAX_VALUE_WORDS * sizeof(uint32_t));

    if (slot == GX_VA_CLR0) {
        return acgc_gx_canonical_geometry_decode_color_word(
            attribute->vat_count,
            attribute->vat_type,
            raw_words[0],
            &canonical_words[0]
        );
    }
    if (slot == GX_VA_NRM) {
        component_count = 3;
        for (word = 0; word < component_count; word++) {
            if (!acgc_gx_canonical_geometry_decode_normal_word(
                    attribute->vat_type,
                    raw_words[word],
                    &canonical_words[word])) {
                return 0;
            }
        }
        return 1;
    }

    component_count = slot == GX_VA_POS && attribute->vat_count == GX_POS_XY
        ? 2
        : slot == GX_VA_TEX0 && attribute->vat_count == GX_TEX_S ? 1 : 2;
    if (slot == GX_VA_POS) component_count =
        attribute->vat_count == GX_POS_XY ? 2 : 3;
    for (word = 0; word < component_count; word++) {
        if (!acgc_gx_canonical_geometry_decode_scalar_word(
                attribute->vat_type,
                attribute->vat_fraction,
                raw_words[word],
                &canonical_words[word])) {
            return 0;
        }
    }
    if (slot == GX_VA_POS && attribute->vat_count == GX_POS_XY) {
        if (raw_words[2] != 0) return 0;
        canonical_words[2] = 0;
    }
    if (slot == GX_VA_TEX0 && attribute->vat_count == GX_TEX_S) {
        if (raw_words[1] != 0) return 0;
        canonical_words[1] = 0;
    }
    return 1;
}

static int producer_attribute_record_domains_are_valid(
    const PCGXRawGeometryAttribute* attribute,
    uint32_t value_count,
    uint32_t index_count,
    int direct
) {
    uint32_t record;
    uint32_t word;
    uint32_t vertex;

    if (attribute == NULL || value_count > PC_GX_GEOMETRY_MAX_VERTICES ||
        index_count > PC_GX_GEOMETRY_MAX_VERTICES) {
        return 0;
    }
    for (record = 0; record < PC_GX_GEOMETRY_MAX_VERTICES; record++) {
        if (record < value_count) {
            if (attribute->value_known[record] != 1 ||
                (direct && attribute->value_source_index[record] != 0)) {
                return 0;
            }
        } else {
            if (attribute->value_known[record] != 0 ||
                attribute->value_source_index[record] != 0) {
                return 0;
            }
            for (word = 0; word < PC_GX_GEOMETRY_MAX_VALUE_WORDS; word++) {
                if (attribute->value_words[record][word] != 0) return 0;
            }
        }
    }
    for (vertex = 0; vertex < PC_GX_GEOMETRY_MAX_VERTICES; vertex++) {
        if (vertex < index_count) {
            if (attribute->index_known[vertex] != 1) return 0;
        } else if (attribute->index_known[vertex] != 0 ||
                   attribute->index_values[vertex] != 0 ||
                   attribute->source_indices[vertex] != 0) {
            return 0;
        }
    }
    return 1;
}

static int producer_attribute_records_are_valid(
    uint32_t slot,
    const PCGXRawGeometryAttribute* attribute,
    uint32_t vertex_count
) {
    uint32_t record;
    uint32_t vertex;
    uint32_t next_new;

    if (attribute->reserved[0] != 0 || attribute->reserved[1] != 0 ||
        (attribute->descriptor_known &
        ~(uint32_t)PC_GX_GEOMETRY_PRODUCER_DESCRIPTOR_KNOWN ||
         attribute->array_known &
            ~(uint32_t)(PC_GX_GEOMETRY_ARRAY_KNOWN |
                        PC_GX_GEOMETRY_ARRAY_DATA_KNOWN)) ||
        !producer_descriptor_format_is_valid(slot, attribute)) {
        return 0;
    }
    if (attribute->value_count == 0 ||
        attribute->value_count > vertex_count) {
        return 0;
    }
    if (!producer_attribute_record_domains_are_valid(
            attribute,
            attribute->value_count,
            attribute->index_count,
            attribute->vcd_type == GX_DIRECT)) {
        return 0;
    }
    for (record = 0; record < attribute->value_count; record++) {
        uint32_t canonical_words[PC_GX_GEOMETRY_MAX_VALUE_WORDS];

        if (!producer_value_is_canonicalizable(
                slot,
                attribute,
                attribute->value_words[record],
                canonical_words)) {
            return 0;
        }
    }

    if (attribute->vcd_type == GX_DIRECT) {
        return attribute->value_count == vertex_count &&
            attribute->index_count == 0 && attribute->index_stride == 0;
    }

    if (!producer_vcd_is_indexed(attribute->vcd_type) ||
        attribute->array_known !=
            (PC_GX_GEOMETRY_ARRAY_KNOWN |
             PC_GX_GEOMETRY_ARRAY_DATA_KNOWN) ||
        attribute->index_count != vertex_count ||
        attribute->index_stride !=
            (attribute->vcd_type == GX_INDEX8 ? 1u : 2u)) {
        return 0;
    }
    if (attribute->array_generation == 0) return 0;
    {
        uint32_t value_bytes = producer_array_value_bytes(slot, attribute);

        if (value_bytes == 0 || attribute->array_stride < value_bytes) {
            return 0;
        }
        for (record = 0; record < attribute->value_count; record++) {
            uint64_t offset;
            uint64_t end;
            uint32_t vertex;

            for (vertex = record + 1;
                 vertex < attribute->value_count;
                 vertex++) {
                if (attribute->value_source_index[record] ==
                    attribute->value_source_index[vertex]) {
                    return 0;
                }
            }
            if (!producer_u64_mul(
                    attribute->value_source_index[record],
                    attribute->array_stride,
                    &offset) ||
                !producer_u64_add(offset, value_bytes, &end) ||
                end > attribute->array_byte_size) {
                return 0;
            }
        }
    }
    next_new = 0;
    for (vertex = 0; vertex < vertex_count; vertex++) {
        uint32_t index = attribute->index_values[vertex];
        uint32_t prior_vertex;

        if (attribute->index_known[vertex] == 0 ||
            index >= attribute->value_count ||
            attribute->source_indices[vertex] !=
                attribute->value_source_index[index]) {
            return 0;
        }
        for (prior_vertex = 0;
             prior_vertex < vertex &&
                 attribute->index_values[prior_vertex] != index;
             prior_vertex++) {
        }
        if (prior_vertex == vertex) {
            if (index != next_new) return 0;
            next_new++;
        }
    }
    return next_new == attribute->value_count;
}

static int producer_layout_is_valid(
    const PCGXRawGeometryBatch* batch,
    PCGXGeometryProducerAttribute* attributes,
    uint32_t* present_mask,
    uint32_t* indexed_mask,
    size_t* section_size
) {
    uint32_t slot;
    uint64_t cursor = ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET;
    uint64_t bytes;

    if (batch == NULL || attributes == NULL || present_mask == NULL ||
        indexed_mask == NULL || section_size == NULL ||
        batch->active != 0 || batch->known != 1 || batch->invalid != 0 ||
        batch->expected_vertex_count != batch->vertex_count ||
        batch->vertex_count == 0 ||
        batch->vertex_count > PC_GX_GEOMETRY_MAX_VERTICES ||
        batch->vtxfmt >= ACGC_GX_CANONICAL_GEOMETRY_VTXFMT_COUNT ||
        (batch->primitive !=
            ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES &&
         batch->primitive != ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS) ||
        (batch->primitive ==
            ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES &&
         batch->vertex_count % 3 != 0) ||
        (batch->primitive ==
            ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS &&
         batch->vertex_count % 4 != 0)) {
        return 0;
    }

    memset(attributes, 0,
           ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT *
               sizeof(attributes[0]));
    *present_mask = 0;
    *indexed_mask = 0;
    for (slot = 0;
         slot < ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT;
         slot++) {
        const PCGXRawGeometryAttribute* attribute = &batch->attr[slot];
        PCGXGeometryProducerAttribute* info = &attributes[slot];

        if (attribute->descriptor_known &
                ~(uint32_t)PC_GX_GEOMETRY_PRODUCER_DESCRIPTOR_KNOWN ||
            attribute->array_known &
                ~(uint32_t)(PC_GX_GEOMETRY_ARRAY_KNOWN |
                            PC_GX_GEOMETRY_ARRAY_DATA_KNOWN)) {
            return 0;
        }
        if ((attribute->descriptor_known &
                PC_GX_GEOMETRY_PRODUCER_DESCRIPTOR_VCD_KNOWN) == 0) {
            return 0;
        }
        if (attribute->vcd_type == GX_NONE) {
            if (attribute->value_word_count != 0 ||
                attribute->value_count != 0 ||
                attribute->index_count != 0 || attribute->index_stride != 0) {
                return 0;
            }
            if (!producer_attribute_record_domains_are_valid(
                    attribute, 0, 0, 0) || attribute->reserved[0] != 0 ||
                attribute->reserved[1] != 0) {
                return 0;
            }
            continue;
        }
        if ((attribute->descriptor_known &
                PC_GX_GEOMETRY_PRODUCER_DESCRIPTOR_KNOWN) !=
            PC_GX_GEOMETRY_PRODUCER_DESCRIPTOR_KNOWN ||
            !producer_slot_is_supported(slot)) {
            return 0;
        }
        if (attribute->vcd_type != GX_DIRECT &&
            !producer_vcd_is_indexed(attribute->vcd_type)) {
            return 0;
        }
        info->present = 1;
        info->indexed = producer_vcd_is_indexed(attribute->vcd_type);
        info->canonical_word_count = producer_expected_word_count(slot);
        if (!producer_attribute_records_are_valid(
                slot, attribute, batch->vertex_count)) {
            return 0;
        }
        info->value_count = attribute->value_count;
        info->index_count = attribute->index_count;
        info->index_stride = attribute->index_stride;
        *present_mask |= UINT32_C(1) << slot;
        if (info->indexed) *indexed_mask |= UINT32_C(1) << slot;
    }

    for (slot = 0;
         slot < ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT;
         slot++) {
        PCGXGeometryProducerAttribute* info = &attributes[slot];
        uint64_t aligned_cursor;
        uint64_t value_end;
        uint64_t index_end;

        if (!info->present) continue;
        if (!producer_u64_mul(
                info->canonical_word_count,
                sizeof(uint32_t),
                &bytes) || !producer_u64_to_u32(bytes, &info->value_stride) ||
            !producer_u64_mul(
                info->value_count,
                info->value_stride,
                &bytes) || !producer_u64_to_u32(bytes, &info->value_bytes) ||
            !producer_u64_align4(cursor, &aligned_cursor) ||
            !producer_u64_to_u32(aligned_cursor, &info->value_offset) ||
            !producer_u64_add(aligned_cursor, info->value_bytes, &value_end)) {
            return 0;
        }
        cursor = value_end;
        if (info->indexed) {
            if (!producer_u64_mul(
                    info->index_count,
                    info->index_stride,
                    &bytes) ||
                !producer_u64_to_u32(bytes, &info->index_bytes) ||
                !producer_u64_align4(cursor, &aligned_cursor) ||
                !producer_u64_to_u32(aligned_cursor, &info->index_offset) ||
                !producer_u64_add(
                    aligned_cursor, info->index_bytes, &index_end)) {
                return 0;
            }
            cursor = index_end;
        }
        if (cursor > ACGC_GX_CANONICAL_GEOMETRY_MAX_SECTION_SIZE) return 0;
    }
    return producer_u64_to_size(cursor, section_size) &&
        *section_size >= ACGC_GX_CANONICAL_GEOMETRY_MIN_SECTION_SIZE;
}

static void producer_write_le16(uint8_t* destination, uint16_t value) {
    destination[0] = (uint8_t)(value & UINT16_C(0xFF));
    destination[1] = (uint8_t)(value >> 8);
}

static void producer_write_le32(uint8_t* destination, uint32_t value) {
    destination[0] = (uint8_t)(value & UINT32_C(0xFF));
    destination[1] = (uint8_t)((value >> 8) & UINT32_C(0xFF));
    destination[2] = (uint8_t)((value >> 16) & UINT32_C(0xFF));
    destination[3] = (uint8_t)(value >> 24);
}

static int producer_record_offset(
    uint32_t base,
    uint32_t record,
    uint32_t stride,
    size_t* offset
) {
    uint64_t product;
    uint64_t result;

    if (!producer_u64_mul(record, stride, &product) ||
        !producer_u64_add(base, product, &result)) {
        return 0;
    }
    return producer_u64_to_size(result, offset);
}

static int producer_write_descriptor(
    uint8_t* section,
    size_t section_size,
    uint32_t slot,
    const PCGXRawGeometryAttribute* attribute,
    const PCGXGeometryProducerAttribute* info
) {
    uint64_t descriptor_offset64;
    size_t descriptor_offset;

    if (!producer_u64_mul(
            slot,
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_SIZE,
            &descriptor_offset64) ||
        !producer_u64_add(
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_OFFSET,
            descriptor_offset64,
            &descriptor_offset64) ||
        !producer_u64_to_size(descriptor_offset64, &descriptor_offset) ||
        descriptor_offset > section_size ||
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_SIZE >
            section_size - descriptor_offset) {
        return 0;
    }
    if (attribute->vcd_type == GX_NONE) return 1;
    producer_write_le32(
        section + descriptor_offset +
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VCD_TYPE_OFFSET,
        attribute->vcd_type);
    producer_write_le32(
        section + descriptor_offset +
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VAT_COUNT_OFFSET,
        attribute->vat_count);
    producer_write_le32(
        section + descriptor_offset +
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VAT_TYPE_OFFSET,
        attribute->vat_type);
    producer_write_le32(
        section + descriptor_offset +
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VAT_FRACTION_OFFSET,
        attribute->vat_fraction);
    producer_write_le32(
        section + descriptor_offset +
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_ENCODING_OFFSET,
        UINT32_C(1));
    producer_write_le32(
        section + descriptor_offset +
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_CANONICAL_WORD_COUNT_OFFSET,
        info->canonical_word_count);
    producer_write_le32(
        section + descriptor_offset +
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_OFFSET,
        info->value_offset);
    producer_write_le32(
        section + descriptor_offset +
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_BYTES_OFFSET,
        info->value_bytes);
    producer_write_le32(
        section + descriptor_offset +
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_STRIDE_OFFSET,
        info->value_stride);
    producer_write_le32(
        section + descriptor_offset +
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_COUNT_OFFSET,
        info->value_count);
    producer_write_le32(
        section + descriptor_offset +
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_OFFSET,
        info->indexed ? info->index_offset : 0);
    producer_write_le32(
        section + descriptor_offset +
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_BYTES_OFFSET,
        info->indexed ? info->index_bytes : 0);
    producer_write_le32(
        section + descriptor_offset +
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_STRIDE_OFFSET,
        info->indexed ? info->index_stride : 0);
    producer_write_le32(
        section + descriptor_offset +
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_COUNT_OFFSET,
        info->indexed ? info->index_count : 0);
    return 1;
}

static int producer_write_values(
    uint8_t* section,
    size_t section_size,
    uint32_t slot,
    const PCGXRawGeometryAttribute* attribute,
    const PCGXGeometryProducerAttribute* info
) {
    uint32_t record;

    for (record = 0; record < info->value_count; record++) {
        uint32_t canonical_words[PC_GX_GEOMETRY_MAX_VALUE_WORDS];
        size_t offset;
        uint32_t word;

        if (!producer_record_offset(
                info->value_offset,
                record,
                info->value_stride,
                &offset) || offset > section_size ||
            info->value_stride > section_size - offset ||
            !producer_value_is_canonicalizable(
                slot,
                attribute,
                attribute->value_words[record],
                canonical_words)) {
            return 0;
        }
        for (word = 0; word < info->canonical_word_count; word++) {
            producer_write_le32(section + offset + word * sizeof(uint32_t),
                                canonical_words[word]);
        }
    }
    return 1;
}

static int producer_write_indices(
    uint8_t* section,
    size_t section_size,
    const PCGXRawGeometryAttribute* attribute,
    const PCGXGeometryProducerAttribute* info
) {
    uint32_t index;

    if (!info->indexed) return 1;
    for (index = 0; index < info->index_count; index++) {
        size_t offset;
        uint32_t value = attribute->index_values[index];

        if (!producer_record_offset(
                info->index_offset,
                index,
                info->index_stride,
                &offset) || offset > section_size ||
            info->index_stride > section_size - offset ||
            value >= info->value_count) {
            return 0;
        }
        if (info->index_stride == 1) {
            section[offset] = (uint8_t)value;
        } else if (info->index_stride == 2 && value <= UINT16_MAX) {
            producer_write_le16(section + offset, (uint16_t)value);
        } else {
            return 0;
        }
    }
    return 1;
}

static int producer_build_section(
    const PCGXRawGeometryBatch* batch,
    const PCGXGeometryProducerAttribute* attributes,
    uint32_t present_mask,
    uint32_t indexed_mask,
    size_t section_size,
    uint8_t* section
) {
    uint32_t slot;

    if (section == NULL || section_size <
            ACGC_GX_CANONICAL_GEOMETRY_MIN_SECTION_SIZE ||
        section_size > ACGC_GX_CANONICAL_GEOMETRY_MAX_SECTION_SIZE) {
        return 0;
    }
    memset(section, 0, section_size);
    producer_write_le32(
        section + ACGC_GX_CANONICAL_GEOMETRY_HEADER_PRIMITIVE_OFFSET,
        batch->primitive);
    producer_write_le32(
        section + ACGC_GX_CANONICAL_GEOMETRY_HEADER_VERTEX_COUNT_OFFSET,
        batch->vertex_count);
    producer_write_le32(
        section + ACGC_GX_CANONICAL_GEOMETRY_HEADER_VTXFMT_OFFSET,
        batch->vtxfmt);
    producer_write_le32(
        section + ACGC_GX_CANONICAL_GEOMETRY_HEADER_DESCRIPTOR_COUNT_OFFSET,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT);
    producer_write_le32(
        section + ACGC_GX_CANONICAL_GEOMETRY_HEADER_PRESENT_MASK_OFFSET,
        present_mask);
    producer_write_le32(
        section + ACGC_GX_CANONICAL_GEOMETRY_HEADER_INDEXED_MASK_OFFSET,
        indexed_mask);
    producer_write_le32(
        section + ACGC_GX_CANONICAL_GEOMETRY_HEADER_DESCRIPTOR_OFFSET_OFFSET,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_OFFSET);
    producer_write_le32(
        section + ACGC_GX_CANONICAL_GEOMETRY_HEADER_DESCRIPTOR_BYTES_OFFSET,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_BYTES);
    producer_write_le32(
        section + ACGC_GX_CANONICAL_GEOMETRY_HEADER_STREAM_OFFSET_OFFSET,
        ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET);
    producer_write_le32(
        section + ACGC_GX_CANONICAL_GEOMETRY_HEADER_STREAM_BYTES_OFFSET,
        (uint32_t)(section_size -
            ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET));

    for (slot = 0;
         slot < ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT;
         slot++) {
        const PCGXRawGeometryAttribute* attribute = &batch->attr[slot];

        if (attribute->descriptor_known &
                ~(uint32_t)PC_GX_GEOMETRY_PRODUCER_DESCRIPTOR_KNOWN ||
            attribute->array_known &
                ~(uint32_t)(PC_GX_GEOMETRY_ARRAY_KNOWN |
                            PC_GX_GEOMETRY_ARRAY_DATA_KNOWN)) {
            return 0;
        }

        if (!producer_write_descriptor(
                section, section_size, slot, attribute, &attributes[slot]) ||
            !producer_write_values(
                section, section_size, slot, attribute, &attributes[slot]) ||
            !producer_write_indices(
                section, section_size, attribute, &attributes[slot])) {
            return 0;
        }
    }
    return 1;
}

int pc_gx_geometry_build_canonical(
    const PCGXRawGeometryBatch* batch,
    const AcgcGxCanonicalGeometryDependencyResults* dependencies,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_size,
    uint8_t* scratch,
    size_t scratch_capacity
) {
    PCGXGeometryProducerAttribute attributes[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT];
    uint32_t present_mask;
    uint32_t indexed_mask;
    size_t section_size;

    if (batch == NULL || dependencies == NULL || output == NULL ||
        output_size == NULL || scratch == NULL ||
        !producer_buffers_are_disjoint(
            output, output_capacity, scratch, scratch_capacity) ||
        !producer_output_size_is_disjoint(
            output_size,
            output,
            output_capacity,
            scratch,
            scratch_capacity) ||
        !producer_layout_is_valid(
            batch,
            attributes,
            &present_mask,
            &indexed_mask,
            &section_size) ||
        section_size > output_capacity || section_size > scratch_capacity) {
        return 0;
    }
    if (!producer_build_section(
            batch,
            attributes,
            present_mask,
            indexed_mask,
            section_size,
            scratch) ||
        !acgc_gx_canonical_geometry_state_validate(
            scratch, section_size) ||
        !acgc_gx_canonical_geometry_state_validate_dependencies(
            scratch, section_size, dependencies)) {
        return 0;
    }
    memcpy(output, scratch, section_size);
    *output_size = section_size;
    return 1;
}
