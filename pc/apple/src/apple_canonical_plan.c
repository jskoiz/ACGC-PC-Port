#include "acgc/apple_canonical_plan.h"

#include <limits.h>
#include <string.h>

enum {
    APPLE_CANONICAL_PLAN_FIXED_WORD_CAPACITY =
        ACGC_GX_CANONICAL_TEV_STATE_SIZE / sizeof(uint32_t),
    APPLE_CANONICAL_PLAN_GEOMETRY_DESCRIPTOR_WORD_COUNT =
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_SIZE / sizeof(uint32_t)
};

#define APPLE_CANONICAL_PLAN_SECTION_INDEX(section_id) \
    ((section_id) - UINT32_C(1))

_Static_assert(
    sizeof(AcgcGxCanonicalTevState) / sizeof(uint32_t) <=
        APPLE_CANONICAL_PLAN_FIXED_WORD_CAPACITY,
    "the fixed canonical word decoder must cover the largest state"
);
_Static_assert(
    APPLE_CANONICAL_PLAN_GEOMETRY_DESCRIPTOR_WORD_COUNT == 16,
    "the Geometry descriptor ABI must contain sixteen LE words"
);

typedef struct AppleCanonicalPlanGeometryDescriptor {
    uint32_t vcd_type;
    uint32_t vat_count;
    uint32_t vat_type;
    uint32_t vat_fraction;
    uint32_t value_encoding;
    uint32_t canonical_word_count;
    uint32_t value_offset;
    uint32_t value_bytes;
    uint32_t value_stride;
    uint32_t value_count;
    uint32_t index_offset;
    uint32_t index_bytes;
    uint32_t index_stride;
    uint32_t index_count;
    uint32_t reserved[2];
} AppleCanonicalPlanGeometryDescriptor;

static int plan_size_add(size_t left, size_t right, size_t* result) {
    if (result == NULL || right > SIZE_MAX - left) {
        return 0;
    }
    *result = left + right;
    return 1;
}

static int plan_size_mul(size_t left, size_t right, size_t* result) {
    if (result == NULL || (left != 0 && right > SIZE_MAX / left)) {
        return 0;
    }
    *result = left * right;
    return 1;
}

static int plan_byte_range_is_valid(
    size_t extent,
    size_t offset,
    size_t width
) {
    return offset <= extent && width <= extent - offset;
}

static int plan_read_le16(
    const uint8_t* bytes,
    size_t byte_size,
    size_t offset,
    uint16_t* value
) {
    if (bytes == NULL || value == NULL ||
        !plan_byte_range_is_valid(byte_size, offset, sizeof(uint16_t))) {
        return 0;
    }
    *value = (uint16_t)bytes[offset] |
        (uint16_t)((uint16_t)bytes[offset + 1] << 8);
    return 1;
}

static int plan_read_le32(
    const uint8_t* bytes,
    size_t byte_size,
    size_t offset,
    uint32_t* value
) {
    if (bytes == NULL || value == NULL ||
        !plan_byte_range_is_valid(byte_size, offset, sizeof(uint32_t))) {
        return 0;
    }
    *value = (uint32_t)bytes[offset] |
        ((uint32_t)bytes[offset + 1] << 8) |
        ((uint32_t)bytes[offset + 2] << 16) |
        ((uint32_t)bytes[offset + 3] << 24);
    return 1;
}

static int plan_pointer_range(
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

static int plan_ranges_overlap(
    uintptr_t first_begin,
    uintptr_t first_end,
    uintptr_t second_begin,
    uintptr_t second_end
) {
    return first_begin < second_end && second_begin < first_end;
}

static AcgcAppleCanonicalPlanStatus plan_validate_input_output_ranges(
    const uint8_t* envelope_bytes,
    size_t envelope_byte_size,
    const AcgcAppleCanonicalPlan* output
) {
    uintptr_t input_begin;
    uintptr_t input_end;
    uintptr_t output_begin;
    uintptr_t output_end;

    if (envelope_bytes == NULL || output == NULL || envelope_byte_size == 0) {
        return ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT;
    }
    if (((uintptr_t)output % (uintptr_t)_Alignof(AcgcAppleCanonicalPlan)) != 0) {
        return ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT;
    }
    if (!plan_pointer_range(
            envelope_bytes, envelope_byte_size, &input_begin, &input_end) ||
        !plan_pointer_range(
            output, sizeof(*output), &output_begin, &output_end)) {
        return ACGC_APPLE_CANONICAL_PLAN_OVERFLOW;
    }
    if (plan_ranges_overlap(
            input_begin, input_end, output_begin, output_end)) {
        return ACGC_APPLE_CANONICAL_PLAN_INPUT_OUTPUT_OVERLAP;
    }
    return ACGC_APPLE_CANONICAL_PLAN_OK;
}

static AcgcAppleCanonicalPlanStatus plan_map_parser_status(
    AcgcAppleCanonicalEnvelopeParserStatus status
) {
    switch (status) {
        case ACGC_APPLE_CANONICAL_ENVELOPE_OK:
            return ACGC_APPLE_CANONICAL_PLAN_OK;
        case ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_ARGUMENT:
            return ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT;
        case ACGC_APPLE_CANONICAL_ENVELOPE_OUTPUT_OVERLAP:
            return ACGC_APPLE_CANONICAL_PLAN_INPUT_OUTPUT_OVERLAP;
        case ACGC_APPLE_CANONICAL_ENVELOPE_OVERFLOW:
            return ACGC_APPLE_CANONICAL_PLAN_OVERFLOW;
        case ACGC_APPLE_CANONICAL_ENVELOPE_TRUNCATED:
        case ACGC_APPLE_CANONICAL_ENVELOPE_UNSUPPORTED_VERSION:
        case ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_HEADER:
        case ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_SIZE:
        case ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY:
        default:
            return ACGC_APPLE_CANONICAL_PLAN_STRUCTURAL;
    }
}

static int plan_section_bytes(
    const uint8_t* envelope_bytes,
    size_t envelope_byte_size,
    const AcgcAppleCanonicalEnvelopeView* view,
    uint32_t section_index,
    const uint8_t** section_bytes,
    size_t* section_byte_size
) {
    const AcgcAppleCanonicalEnvelopeSection* section;

    if (envelope_bytes == NULL || view == NULL || section_bytes == NULL ||
        section_byte_size == NULL ||
        section_index >= ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT) {
        return 0;
    }
    section = &view->sections[section_index];
    if (!plan_byte_range_is_valid(
            envelope_byte_size,
            (size_t)section->byte_offset,
            (size_t)section->byte_size)) {
        return 0;
    }
    *section_bytes = envelope_bytes + section->byte_offset;
    *section_byte_size = section->byte_size;
    return 1;
}

/*
 * All fixed canonical structs in this plan are composed solely of uint32_t
 * and int32_t words and have exact word-sized ABI assertions in their public
 * headers. Decode to native uint32_t temporaries first, then copy those
 * already-decoded values into the local value object. The wire is never
 * viewed as a native struct and no native struct is copied from wire bytes.
 */
static int plan_decode_fixed_words(
    const uint8_t* section_bytes,
    size_t section_byte_size,
    void* destination,
    size_t destination_byte_size
) {
    uint32_t words[APPLE_CANONICAL_PLAN_FIXED_WORD_CAPACITY];
    size_t word_count;
    size_t word;

    if (section_bytes == NULL || destination == NULL ||
        destination_byte_size == 0 ||
        (destination_byte_size % sizeof(uint32_t)) != 0 ||
        section_byte_size != destination_byte_size) {
        return 0;
    }
    word_count = destination_byte_size / sizeof(uint32_t);
    if (word_count > sizeof(words) / sizeof(words[0])) {
        return 0;
    }
    for (word = 0; word < word_count; word++) {
        size_t offset;

        if (!plan_size_mul(word, sizeof(uint32_t), &offset) ||
            !plan_read_le32(
                section_bytes, section_byte_size, offset, &words[word])) {
            return 0;
        }
    }
    memcpy(destination, words, destination_byte_size);
    return 1;
}

static int plan_geometry_descriptor_read(
    const uint8_t* section_bytes,
    size_t section_byte_size,
    uint32_t slot,
    AppleCanonicalPlanGeometryDescriptor* descriptor
) {
    size_t base;
    uint32_t word;
    uint32_t values[APPLE_CANONICAL_PLAN_GEOMETRY_DESCRIPTOR_WORD_COUNT];

    if (section_bytes == NULL || descriptor == NULL ||
        slot >= ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT ||
        !plan_size_mul(
            slot,
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_SIZE,
            &base) ||
        !plan_size_add(
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_OFFSET,
            base,
            &base)) {
        return 0;
    }
    for (word = 0;
         word < APPLE_CANONICAL_PLAN_GEOMETRY_DESCRIPTOR_WORD_COUNT;
         word++) {
        size_t word_offset;

        if (!plan_size_mul(word, sizeof(uint32_t), &word_offset) ||
            !plan_size_add(base, word_offset, &word_offset) ||
            !plan_read_le32(
                section_bytes, section_byte_size, word_offset, &values[word])) {
            return 0;
        }
    }
    descriptor->vcd_type = values[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VCD_TYPE_OFFSET /
            sizeof(uint32_t)];
    descriptor->vat_count = values[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VAT_COUNT_OFFSET /
            sizeof(uint32_t)];
    descriptor->vat_type = values[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VAT_TYPE_OFFSET /
            sizeof(uint32_t)];
    descriptor->vat_fraction = values[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VAT_FRACTION_OFFSET /
            sizeof(uint32_t)];
    descriptor->value_encoding = values[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_ENCODING_OFFSET /
            sizeof(uint32_t)];
    descriptor->canonical_word_count = values[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_CANONICAL_WORD_COUNT_OFFSET /
            sizeof(uint32_t)];
    descriptor->value_offset = values[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_OFFSET /
            sizeof(uint32_t)];
    descriptor->value_bytes = values[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_BYTES_OFFSET /
            sizeof(uint32_t)];
    descriptor->value_stride = values[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_STRIDE_OFFSET /
            sizeof(uint32_t)];
    descriptor->value_count = values[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_COUNT_OFFSET /
            sizeof(uint32_t)];
    descriptor->index_offset = values[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_OFFSET /
            sizeof(uint32_t)];
    descriptor->index_bytes = values[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_BYTES_OFFSET /
            sizeof(uint32_t)];
    descriptor->index_stride = values[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_STRIDE_OFFSET /
            sizeof(uint32_t)];
    descriptor->index_count = values[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_COUNT_OFFSET /
            sizeof(uint32_t)];
    descriptor->reserved[0] = values[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_RESERVED0_OFFSET /
            sizeof(uint32_t)];
    descriptor->reserved[1] = values[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_RESERVED1_OFFSET /
            sizeof(uint32_t)];
    return 1;
}

static int plan_geometry_value_record(
    const uint8_t* section_bytes,
    size_t section_byte_size,
    const AppleCanonicalPlanGeometryDescriptor* descriptor,
    uint32_t vertex,
    uint32_t component,
    uint32_t* raw_value
) {
    uint32_t record = vertex;
    size_t record_offset;
    size_t value_offset;
    size_t component_offset;

    if (section_bytes == NULL || descriptor == NULL || raw_value == NULL ||
        component >= descriptor->canonical_word_count) {
        return 0;
    }
    if ((descriptor->vcd_type == ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX8 ||
         descriptor->vcd_type == ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX16) &&
        vertex >= descriptor->index_count) {
        return 0;
    }
    if (descriptor->vcd_type != ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX8 &&
        descriptor->vcd_type != ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX16 &&
        vertex >= descriptor->value_count) {
        return 0;
    }
    if (descriptor->vcd_type == ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX8 ||
        descriptor->vcd_type == ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX16) {
        size_t index_offset;
        uint16_t index16;

        if (!plan_size_mul(
                vertex, descriptor->index_stride, &record_offset) ||
            !plan_size_add(
                descriptor->index_offset, record_offset, &index_offset)) {
            return 0;
        }
        if (descriptor->index_stride == 1) {
            if (!plan_byte_range_is_valid(
                    section_byte_size, index_offset, sizeof(uint8_t))) {
                return 0;
            }
            record = section_bytes[index_offset];
        } else if (descriptor->index_stride == 2 &&
                   plan_read_le16(
                       section_bytes,
                       section_byte_size,
                       index_offset,
                       &index16)) {
            record = index16;
        } else {
            return 0;
        }
        if (record >= descriptor->value_count) {
            return 0;
        }
    }
    if (!plan_size_mul(
            record, descriptor->value_stride, &record_offset) ||
        !plan_size_add(
            descriptor->value_offset, record_offset, &value_offset) ||
        !plan_size_mul(
            component, sizeof(uint32_t), &component_offset) ||
        !plan_size_add(value_offset, component_offset, &value_offset)) {
        return 0;
    }
    return plan_read_le32(
        section_bytes, section_byte_size, value_offset, raw_value);
}

static int plan_geometry_canonical_word_record(
    const uint8_t* section_bytes,
    size_t section_byte_size,
    const AppleCanonicalPlanGeometryDescriptor* descriptor,
    uint32_t vertex,
    uint32_t component,
    uint32_t* canonical_word
) {
    /* The canonical Geometry validator has already checked the complete
     * record.  Its value_encoding=1 contract makes these words final plan
     * values, so never apply the source VAT conversion a second time. */
    if (descriptor == NULL || descriptor->value_encoding != UINT32_C(1)) {
        return 0;
    }
    return plan_geometry_value_record(
        section_bytes,
        section_byte_size,
        descriptor,
        vertex,
        component,
        canonical_word
    );
}

static int plan_geometry_position_id_is_exact(uint32_t value) {
    uint32_t slot;

    for (slot = 0;
         slot < ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_COUNT;
         slot++) {
        if (value ==
            ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_FIRST +
                slot * ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_STRIDE) {
            return 1;
        }
    }
    return 0;
}

static int plan_geometry_ordinary_matrix_id_to_slot(
    uint32_t value,
    uint32_t* slot
) {
    uint32_t index;

    if (slot == NULL) {
        return 0;
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_COUNT;
         index++) {
        if (value ==
            ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_FIRST +
                index *
                    ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_STRIDE) {
            *slot = index;
            return 1;
        }
    }
    return 0;
}

static uint32_t plan_geometry_component_mask(
    uint32_t present_mask,
    const AppleCanonicalPlanGeometryDescriptor descriptors[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT]
) {
    uint32_t mask = ACGC_APPLE_CANONICAL_PLAN_COMPONENT_POSITION;
    uint32_t coord;

    if ((present_mask & (UINT32_C(1) <<
            ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM)) != 0 ||
        (present_mask & (UINT32_C(1) <<
            ACGC_GX_CANONICAL_GEOMETRY_ATTR_NBT)) != 0) {
        mask |= ACGC_APPLE_CANONICAL_PLAN_COMPONENT_NORMAL;
        if (descriptors[ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM].vat_count ==
                ACGC_GX_CANONICAL_GEOMETRY_NRM_NBT ||
            descriptors[ACGC_GX_CANONICAL_GEOMETRY_ATTR_NBT].vat_count ==
                ACGC_GX_CANONICAL_GEOMETRY_NRM_NBT) {
            mask |= ACGC_APPLE_CANONICAL_PLAN_COMPONENT_BINORMAL |
                ACGC_APPLE_CANONICAL_PLAN_COMPONENT_TANGENT;
        }
    }
    if ((present_mask & (UINT32_C(1) <<
            ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0)) != 0) {
        mask |= ACGC_APPLE_CANONICAL_PLAN_COMPONENT_COLOR0;
    }
    if ((present_mask & (UINT32_C(1) <<
            ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR1)) != 0) {
        mask |= ACGC_APPLE_CANONICAL_PLAN_COMPONENT_COLOR1;
    }
    for (coord = 0; coord < ACGC_GX_CANONICAL_TEXGEN_COUNT; coord++) {
        if ((present_mask & (UINT32_C(1) <<
                (ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0 + coord))) != 0) {
            mask |= ACGC_APPLE_CANONICAL_PLAN_COMPONENT_TEXCOORD0 << coord;
        }
    }
    return mask;
}

static AcgcAppleCanonicalPlanStatus plan_decode_geometry(
    const uint8_t* section_bytes,
    size_t section_byte_size,
    const AcgcGxCanonicalTransformState* transform,
    const AcgcGxCanonicalTexgenState* texgens,
    AcgcAppleCanonicalPlanGeometry* geometry
) {
    AppleCanonicalPlanGeometryDescriptor descriptors[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT];
    uint32_t primitive;
    uint32_t vertex_count;
    uint32_t vtxfmt;
    uint32_t present_mask;
    uint32_t indexed_mask;
    const uint32_t array_mask =
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS_MTX_ARRAY) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM_MTX_ARRAY) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX_MTX_ARRAY) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_LIGHT_ARRAY);
    uint32_t descriptor_index;
    uint32_t vertex;
    uint32_t coord;
    AcgcAppleCanonicalPlanGeometry candidate;

    if (section_bytes == NULL || geometry == NULL || transform == NULL ||
        texgens == NULL ||
        !acgc_gx_canonical_geometry_state_validate(
            section_bytes, section_byte_size)) {
        return ACGC_APPLE_CANONICAL_PLAN_GEOMETRY_LIMIT;
    }
    if (!plan_read_le32(
            section_bytes,
            section_byte_size,
            ACGC_GX_CANONICAL_GEOMETRY_HEADER_PRIMITIVE_OFFSET,
            &primitive) ||
        !plan_read_le32(
            section_bytes,
            section_byte_size,
            ACGC_GX_CANONICAL_GEOMETRY_HEADER_VERTEX_COUNT_OFFSET,
            &vertex_count) ||
        !plan_read_le32(
            section_bytes,
            section_byte_size,
            ACGC_GX_CANONICAL_GEOMETRY_HEADER_VTXFMT_OFFSET,
            &vtxfmt) ||
        !plan_read_le32(
            section_bytes,
            section_byte_size,
            ACGC_GX_CANONICAL_GEOMETRY_HEADER_PRESENT_MASK_OFFSET,
            &present_mask) ||
        !plan_read_le32(
            section_bytes,
            section_byte_size,
            ACGC_GX_CANONICAL_GEOMETRY_HEADER_INDEXED_MASK_OFFSET,
            &indexed_mask) ||
        vertex_count > ACGC_APPLE_CANONICAL_PLAN_MAX_VERTEX_COUNT ||
        (indexed_mask & array_mask) != 0) {
        return ACGC_APPLE_CANONICAL_PLAN_GEOMETRY_LIMIT;
    }
    memset(descriptors, 0, sizeof(descriptors));
    for (descriptor_index = 0;
         descriptor_index < ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT;
         descriptor_index++) {
        if (!plan_geometry_descriptor_read(
                section_bytes,
                section_byte_size,
                descriptor_index,
                &descriptors[descriptor_index])) {
            return ACGC_APPLE_CANONICAL_PLAN_SECTION_DECODE;
        }
    }

    memset(&candidate, 0, sizeof(candidate));
    candidate.primitive = primitive;
    candidate.vtxfmt = vtxfmt;
    candidate.vertex_count = vertex_count;
    candidate.present_mask = present_mask;
    candidate.component_mask = plan_geometry_component_mask(
        present_mask, descriptors);

    for (vertex = 0; vertex < vertex_count; vertex++) {
        AcgcAppleCanonicalPlanVertex* plan_vertex = &candidate.vertices[vertex];

        plan_vertex->present_mask = present_mask;
        plan_vertex->component_mask = candidate.component_mask;
        if ((present_mask & (UINT32_C(1) <<
                ACGC_GX_CANONICAL_GEOMETRY_ATTR_PNMTXIDX)) != 0) {
            if (!plan_geometry_value_record(
                    section_bytes,
                    section_byte_size,
                    &descriptors[ACGC_GX_CANONICAL_GEOMETRY_ATTR_PNMTXIDX],
                    vertex,
                    0,
                    &plan_vertex->position_matrix_id) ||
                !plan_geometry_position_id_is_exact(
                    plan_vertex->position_matrix_id)) {
                return ACGC_APPLE_CANONICAL_PLAN_GEOMETRY_LIMIT;
            }
        } else {
            plan_vertex->position_matrix_id = transform->current_position_id;
        }
        for (coord = 0; coord < ACGC_GX_CANONICAL_TEXGEN_COUNT; coord++) {
            const uint32_t matrix_slot =
                ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0MTXIDX + coord;
            const uint32_t ordinary_id = coord <
                texgens->header.active_texgen_count
                ? texgens->texgen[coord].ordinary_matrix_id : 0;
            uint32_t ordinary_slot;

            if ((present_mask & (UINT32_C(1) << matrix_slot)) != 0) {
                if (!plan_geometry_value_record(
                        section_bytes,
                        section_byte_size,
                        &descriptors[matrix_slot],
                        vertex,
                        0,
                        &plan_vertex->texture_matrix_id[coord]) ||
                    !plan_geometry_ordinary_matrix_id_to_slot(
                        plan_vertex->texture_matrix_id[coord],
                        &ordinary_slot)) {
                    return ACGC_APPLE_CANONICAL_PLAN_GEOMETRY_LIMIT;
                }
            } else {
                plan_vertex->texture_matrix_id[coord] = ordinary_id;
            }
        }

        if (!plan_geometry_canonical_word_record(
                section_bytes,
                section_byte_size,
                &descriptors[ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS],
                vertex,
                0,
                &plan_vertex->position[0])) {
            return ACGC_APPLE_CANONICAL_PLAN_GEOMETRY_LIMIT;
        }
        for (coord = 1; coord < 3; coord++) {
            if (!plan_geometry_canonical_word_record(
                    section_bytes,
                    section_byte_size,
                    &descriptors[ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS],
                    vertex,
                    coord,
                    &plan_vertex->position[coord])) {
                return ACGC_APPLE_CANONICAL_PLAN_GEOMETRY_LIMIT;
            }
        }

        if ((present_mask & (UINT32_C(1) <<
                ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM)) != 0 ||
            (present_mask & (UINT32_C(1) <<
                ACGC_GX_CANONICAL_GEOMETRY_ATTR_NBT)) != 0) {
            const uint32_t normal_slot =
                (present_mask & (UINT32_C(1) <<
                    ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM)) != 0
                ? ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM
                : ACGC_GX_CANONICAL_GEOMETRY_ATTR_NBT;
            const AppleCanonicalPlanGeometryDescriptor* normal_descriptor =
                &descriptors[normal_slot];
            uint32_t normal_word;

            for (normal_word = 0;
                 normal_word < normal_descriptor->canonical_word_count;
                 normal_word++) {
                uint32_t* destination_word;

                if (normal_word < 3) {
                    destination_word = &plan_vertex->normal[normal_word];
                } else if (normal_word < 6) {
                    destination_word = &plan_vertex->binormal[normal_word - 3];
                } else {
                    destination_word = &plan_vertex->tangent[normal_word - 6];
                }
                if (!plan_geometry_canonical_word_record(
                        section_bytes,
                        section_byte_size,
                        normal_descriptor,
                        vertex,
                        normal_word,
                        destination_word)) {
                    return ACGC_APPLE_CANONICAL_PLAN_GEOMETRY_LIMIT;
                }
            }
        }

        for (coord = 0; coord < 2; coord++) {
            const uint32_t color_slot =
                ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0 + coord;
            if ((present_mask & (UINT32_C(1) << color_slot)) != 0) {
                if (!plan_geometry_canonical_word_record(
                        section_bytes,
                        section_byte_size,
                        &descriptors[color_slot],
                        vertex,
                        0,
                        &plan_vertex->color_rgba8[coord])) {
                    return ACGC_APPLE_CANONICAL_PLAN_GEOMETRY_LIMIT;
                }
            }
        }
        for (coord = 0; coord < ACGC_GX_CANONICAL_TEXGEN_COUNT; coord++) {
            const uint32_t texcoord_slot =
                ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0 + coord;
            const AppleCanonicalPlanGeometryDescriptor* texcoord_descriptor =
                &descriptors[texcoord_slot];
            uint32_t component;

            if ((present_mask & (UINT32_C(1) << texcoord_slot)) == 0) {
                continue;
            }
            for (component = 0; component < 2; component++) {
                if (!plan_geometry_canonical_word_record(
                        section_bytes,
                        section_byte_size,
                        texcoord_descriptor,
                        vertex,
                        component,
                        &plan_vertex->texcoord[coord][component])) {
                    return ACGC_APPLE_CANONICAL_PLAN_GEOMETRY_LIMIT;
                }
            }
        }
    }
    *geometry = candidate;
    return ACGC_APPLE_CANONICAL_PLAN_OK;
}

static int plan_position_id_to_slot(uint32_t id, uint32_t* slot) {
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

static int plan_texgen_source_to_geometry_slot(
    uint32_t source,
    uint32_t* slot
) {
    if (slot == NULL) {
        return 0;
    }
    switch (source) {
        case ACGC_GX_CANONICAL_TEXGEN_SOURCE_POS:
            *slot = ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS;
            return 1;
        case ACGC_GX_CANONICAL_TEXGEN_SOURCE_NRM:
            *slot = ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM;
            return 1;
        case ACGC_GX_CANONICAL_TEXGEN_SOURCE_BINRM:
        case ACGC_GX_CANONICAL_TEXGEN_SOURCE_TANGENT:
            *slot = ACGC_GX_CANONICAL_GEOMETRY_ATTR_NBT;
            return 1;
        case ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX0:
        case ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX1:
        case ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX2:
        case ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX3:
        case ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX4:
        case ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX5:
        case ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX6:
        case ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX7:
            *slot = ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0 +
                source - ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX0;
            return 1;
        case ACGC_GX_CANONICAL_TEXGEN_SOURCE_COLOR0:
            *slot = ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0;
            return 1;
        case ACGC_GX_CANONICAL_TEXGEN_SOURCE_COLOR1:
            *slot = ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR1;
            return 1;
        default:
            return 0;
    }
}

static int plan_texgen_has_unsupported_bump(
    const AcgcGxCanonicalTexgenState* texgens
) {
    uint32_t index;

    for (index = 0; index < texgens->header.active_texgen_count; index++) {
        const uint32_t function = texgens->texgen[index].function;
        if (function >= ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP0 &&
            function <= ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP7) {
            return 1;
        }
    }
    return 0;
}

static int plan_channel_uses_vertex(
    const AcgcGxCanonicalChannelControl* control
) {
    return control != NULL &&
        control->enable == ACGC_GX_CANONICAL_CHANNEL_BOOLEAN_TRUE &&
        (control->ambient_source == ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX ||
         control->material_source == ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX);
}

static int plan_build_geometry_dependencies(
    const AcgcAppleCanonicalPlanGeometry* geometry,
    const AcgcGxCanonicalTransformState* transform,
    const AcgcGxCanonicalTexgenState* texgens,
    const AcgcGxCanonicalChannelState* channels,
    const AcgcGxCanonicalLightingState* lighting,
    AcgcGxCanonicalGeometryDependencyResults* dependencies
) {
    AcgcGxCanonicalGeometryDependencyResults candidate;
    uint32_t current_slot;
    uint32_t matrix_slot;
    uint32_t coord;
    uint32_t channel;

    if (geometry == NULL || transform == NULL || texgens == NULL ||
        channels == NULL || lighting == NULL || dependencies == NULL ||
        !plan_position_id_to_slot(
            transform->current_position_id, &current_slot) ||
        (transform->known_mask &
            ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_KNOWN_MASK) == 0 ||
        (transform->known_mask &
            ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(current_slot)) == 0) {
        return 0;
    }
    memset(&candidate, 0, sizeof(candidate));
    candidate.transform_valid = 1;
    candidate.texgens_valid = 1;
    candidate.channels_valid = 1;
    candidate.lighting_valid = 1;
    candidate.bump_valid = 0;
    candidate.transform_position_known_mask = 0;
    candidate.transform_normal_known_mask = 0;
    for (matrix_slot = 0;
         matrix_slot < ACGC_GX_CANONICAL_TRANSFORM_POSITION_SLOT_COUNT;
         matrix_slot++) {
        if ((transform->known_mask &
                ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(
                    matrix_slot)) != 0) {
            candidate.transform_position_known_mask |=
                UINT32_C(1) << matrix_slot;
        }
        if ((transform->known_mask &
                ACGC_GX_CANONICAL_TRANSFORM_NORMAL_KNOWN_MASK(
                    matrix_slot)) != 0) {
            candidate.transform_normal_known_mask |=
                UINT32_C(1) << matrix_slot;
        }
    }
    candidate.transform_current_position_known = 1;
    candidate.transform_current_position_id = transform->current_position_id;
    candidate.texgen_ordinary_known_mask =
        texgens->header.ordinary_matrix_known_mask;
    candidate.texgen_post_known_mask = texgens->header.post_matrix_known_mask;
    candidate.lighting_loaded_mask = lighting->loaded_mask;
    candidate.required_geometry_present_mask = geometry->present_mask;

    if ((geometry->present_mask &
            ((UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM) |
             (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NBT))) != 0 &&
        (candidate.transform_normal_known_mask &
            (UINT32_C(1) << current_slot)) == 0) {
        return 0;
    }
    for (coord = 0; coord < ACGC_GX_CANONICAL_TEXGEN_COUNT; coord++) {
        const AcgcGxCanonicalTexgenRecord* record;
        uint32_t ordinary_slot;

        if (coord >= texgens->header.active_texgen_count ||
            (texgens->header.texgen_known_mask &
                (UINT32_C(1) << coord)) == 0) {
            break;
        }
        record = &texgens->texgen[coord];
        if (record->component_known !=
                ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL ||
            !plan_geometry_ordinary_matrix_id_to_slot(
                record->ordinary_matrix_id, &ordinary_slot) ||
            (candidate.texgen_ordinary_known_mask &
                (UINT32_C(1) << ordinary_slot)) == 0) {
            return 0;
        }
        candidate.texgen_present_mask |= UINT32_C(1) << coord;
        candidate.texgen_selector[coord] = record->ordinary_matrix_id;
        if (!plan_texgen_source_to_geometry_slot(record->source, &ordinary_slot)) {
            return 0;
        }
        if ((geometry->present_mask & (UINT32_C(1) <<
                (ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0 + coord))) != 0) {
            candidate.required_geometry_present_mask |=
                UINT32_C(1) << ordinary_slot;
        }
    }
    for (coord = 0; coord < ACGC_GX_CANONICAL_TEXGEN_COUNT; coord++) {
        const uint32_t texcoord_bit = UINT32_C(1) <<
            (ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0 + coord);
        if ((geometry->present_mask & texcoord_bit) != 0 &&
            (candidate.texgen_present_mask & (UINT32_C(1) << coord)) == 0) {
            return 0;
        }
    }

    for (channel = 0; channel < channels->active_count; channel++) {
        const AcgcGxCanonicalChannelRecord* record = &channels->records[channel];
        const AcgcGxCanonicalChannelControl* color = &record->color;
        const AcgcGxCanonicalChannelControl* alpha = &record->alpha;

        if (plan_channel_uses_vertex(color) || plan_channel_uses_vertex(alpha)) {
            const uint32_t color_slot =
                ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0 + channel;
            if ((geometry->present_mask & (UINT32_C(1) << color_slot)) == 0) {
                return 0;
            }
            candidate.required_channel_mask |= UINT32_C(1) << channel;
            candidate.required_geometry_present_mask |=
                UINT32_C(1) << color_slot;
        }
        if (color->enable == ACGC_GX_CANONICAL_CHANNEL_BOOLEAN_TRUE) {
            candidate.required_lighting_mask |= color->light_mask;
        }
        if (alpha->enable == ACGC_GX_CANONICAL_CHANNEL_BOOLEAN_TRUE) {
            candidate.required_lighting_mask |= alpha->light_mask;
        }
    }
    if (candidate.required_lighting_mask != 0) {
        const uint32_t normal_mask =
            (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM) |
            (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NBT);
        if ((geometry->present_mask & normal_mask) == 0 ||
            (candidate.lighting_loaded_mask & candidate.required_lighting_mask) !=
                candidate.required_lighting_mask ||
            (candidate.transform_normal_known_mask &
                (UINT32_C(1) << current_slot)) == 0) {
            return 0;
        }
        candidate.required_geometry_present_mask |=
            (geometry->present_mask &
                (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM)) != 0
            ? UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM
            : UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NBT;
    }
    *dependencies = candidate;
    return 1;
}

static int plan_validate_tev_texture_dependencies(
    const AcgcGxCanonicalTevState* tev,
    const AcgcGxCanonicalTextureState* texture,
    const AcgcGxCanonicalChannelState* channels,
    const AcgcGxCanonicalGeometryDependencyResults* geometry_dependencies
) {
    uint32_t stage_index;

    if (tev == NULL || texture == NULL || channels == NULL ||
        geometry_dependencies == NULL) {
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
                 (geometry_dependencies->texgen_present_mask &
                    (UINT32_C(1) << stage->tex_coord)) == 0)) {
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

AcgcAppleCanonicalPlanStatus acgc_apple_canonical_plan_build(
    const uint8_t* envelope_bytes,
    size_t envelope_byte_size,
    AcgcAppleCanonicalPlan* output
) {
    AcgcAppleCanonicalEnvelopeView view;
    AcgcAppleCanonicalPlan candidate;
    AcgcGxCanonicalGeometryDependencyResults dependencies;
    const uint8_t* section_bytes;
    size_t section_byte_size;
    AcgcAppleCanonicalPlanStatus status;
    AcgcAppleCanonicalEnvelopeParserStatus parser_status;

    status = plan_validate_input_output_ranges(
        envelope_bytes, envelope_byte_size, output);
    if (status != ACGC_APPLE_CANONICAL_PLAN_OK) {
        return status;
    }
    memset(&view, 0, sizeof(view));
    parser_status = acgc_apple_canonical_envelope_parse(
        envelope_bytes, envelope_byte_size, &view);
    status = plan_map_parser_status(parser_status);
    if (status != ACGC_APPLE_CANONICAL_PLAN_OK) {
        return status;
    }
    if (view.present_state_mask !=
            ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK ||
        view.required_state_mask !=
            ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK) {
        return ACGC_APPLE_CANONICAL_PLAN_STRUCTURAL;
    }

    memset(&candidate, 0, sizeof(candidate));

    /* Decode and validate section 2 before the Geometry defaults are used. */
    if (!plan_section_bytes(
            envelope_bytes, envelope_byte_size, &view,
            APPLE_CANONICAL_PLAN_SECTION_INDEX(
                ACGC_GX_CANONICAL_SECTION_ID_TRANSFORMS),
            &section_bytes, &section_byte_size) ||
        !plan_decode_fixed_words(
            section_bytes,
            section_byte_size,
            &candidate.transform,
            sizeof(candidate.transform))) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_DECODE;
    }
    if (!acgc_gx_canonical_transform_state_validate(&candidate.transform)) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_SEMANTIC;
    }

    /* Decode and validate section 4 before Geometry's matrix defaults. */
    if (!plan_section_bytes(
            envelope_bytes, envelope_byte_size, &view,
            APPLE_CANONICAL_PLAN_SECTION_INDEX(
                ACGC_GX_CANONICAL_SECTION_ID_TEXGENS),
            &section_bytes, &section_byte_size) ||
        !acgc_gx_canonical_texgen_state_decode(
            section_bytes,
            section_byte_size,
            &candidate.texgens)) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_DECODE;
    }
    if (!acgc_gx_canonical_texgen_state_validate(&candidate.texgens)) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_SEMANTIC;
    }
    if (plan_texgen_has_unsupported_bump(&candidate.texgens)) {
        return ACGC_APPLE_CANONICAL_PLAN_UNSUPPORTED_BUMP;
    }

    /* The remaining fixed sections are explicit LE word decodes. */
    if (!plan_section_bytes(
            envelope_bytes, envelope_byte_size, &view,
            APPLE_CANONICAL_PLAN_SECTION_INDEX(
                ACGC_GX_CANONICAL_SECTION_ID_CHANNELS),
            &section_bytes, &section_byte_size) ||
        !plan_decode_fixed_words(
            section_bytes, section_byte_size,
            &candidate.channels, sizeof(candidate.channels))) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_DECODE;
    }
    if (!acgc_gx_canonical_channel_state_validate(&candidate.channels)) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_SEMANTIC;
    }
    if (!plan_section_bytes(
            envelope_bytes, envelope_byte_size, &view,
            APPLE_CANONICAL_PLAN_SECTION_INDEX(
                ACGC_GX_CANONICAL_SECTION_ID_TEXTURES),
            &section_bytes, &section_byte_size) ||
        !plan_decode_fixed_words(
            section_bytes, section_byte_size,
            &candidate.texture, sizeof(candidate.texture))) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_DECODE;
    }
    if (!acgc_gx_canonical_texture_state_validate(&candidate.texture)) {
        return ACGC_APPLE_CANONICAL_PLAN_RESOURCE_METADATA;
    }
    if (!plan_section_bytes(
            envelope_bytes, envelope_byte_size, &view,
            APPLE_CANONICAL_PLAN_SECTION_INDEX(
                ACGC_GX_CANONICAL_SECTION_ID_TEV),
            &section_bytes, &section_byte_size) ||
        !plan_decode_fixed_words(
            section_bytes, section_byte_size,
            &candidate.tev, sizeof(candidate.tev))) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_DECODE;
    }
    if (!acgc_gx_canonical_tev_state_validate(&candidate.tev)) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_SEMANTIC;
    }
    if (!plan_section_bytes(
            envelope_bytes, envelope_byte_size, &view,
            APPLE_CANONICAL_PLAN_SECTION_INDEX(
                ACGC_GX_CANONICAL_SECTION_ID_LIGHTING),
            &section_bytes, &section_byte_size) ||
        !plan_decode_fixed_words(
            section_bytes, section_byte_size,
            &candidate.lighting, sizeof(candidate.lighting))) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_DECODE;
    }
    if (!acgc_gx_canonical_lighting_state_validate(&candidate.lighting)) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_SEMANTIC;
    }
    if (!plan_section_bytes(
            envelope_bytes, envelope_byte_size, &view,
            APPLE_CANONICAL_PLAN_SECTION_INDEX(
                ACGC_GX_CANONICAL_SECTION_ID_BLEND),
            &section_bytes, &section_byte_size) ||
        !plan_decode_fixed_words(
            section_bytes, section_byte_size,
            &candidate.blend, sizeof(candidate.blend))) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_DECODE;
    }
    if (!acgc_gx_canonical_blend_state_validate(&candidate.blend)) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_SEMANTIC;
    }
    if (!plan_section_bytes(
            envelope_bytes, envelope_byte_size, &view,
            APPLE_CANONICAL_PLAN_SECTION_INDEX(
                ACGC_GX_CANONICAL_SECTION_ID_ALPHA),
            &section_bytes, &section_byte_size) ||
        !plan_decode_fixed_words(
            section_bytes, section_byte_size,
            &candidate.alpha, sizeof(candidate.alpha))) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_DECODE;
    }
    if (!acgc_gx_canonical_alpha_state_validate(&candidate.alpha)) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_SEMANTIC;
    }
    if (!plan_section_bytes(
            envelope_bytes, envelope_byte_size, &view,
            APPLE_CANONICAL_PLAN_SECTION_INDEX(
                ACGC_GX_CANONICAL_SECTION_ID_DEPTH),
            &section_bytes, &section_byte_size) ||
        !plan_decode_fixed_words(
            section_bytes, section_byte_size,
            &candidate.depth, sizeof(candidate.depth))) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_DECODE;
    }
    if (!acgc_gx_canonical_depth_state_validate(&candidate.depth)) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_SEMANTIC;
    }
    if (!plan_section_bytes(
            envelope_bytes, envelope_byte_size, &view,
            APPLE_CANONICAL_PLAN_SECTION_INDEX(
                ACGC_GX_CANONICAL_SECTION_ID_RASTER),
            &section_bytes, &section_byte_size) ||
        !plan_decode_fixed_words(
            section_bytes, section_byte_size,
            &candidate.raster, sizeof(candidate.raster))) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_DECODE;
    }
    if (!acgc_gx_canonical_raster_state_validate(&candidate.raster)) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_SEMANTIC;
    }
    if (!plan_section_bytes(
            envelope_bytes, envelope_byte_size, &view,
            APPLE_CANONICAL_PLAN_SECTION_INDEX(
                ACGC_GX_CANONICAL_SECTION_ID_FOG),
            &section_bytes, &section_byte_size) ||
        !plan_decode_fixed_words(
            section_bytes, section_byte_size,
            &candidate.fog, sizeof(candidate.fog))) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_DECODE;
    }
    if (!acgc_gx_canonical_fog_state_validate(&candidate.fog)) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_SEMANTIC;
    }
    if (!plan_section_bytes(
            envelope_bytes, envelope_byte_size, &view,
            APPLE_CANONICAL_PLAN_SECTION_INDEX(
                ACGC_GX_CANONICAL_SECTION_ID_INDIRECT),
            &section_bytes, &section_byte_size) ||
        !plan_decode_fixed_words(
            section_bytes, section_byte_size,
            &candidate.indirect, sizeof(candidate.indirect))) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_DECODE;
    }
    if (!acgc_gx_canonical_indirect_state_validate(&candidate.indirect)) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_SEMANTIC;
    }
    if (!plan_section_bytes(
            envelope_bytes, envelope_byte_size, &view,
            APPLE_CANONICAL_PLAN_SECTION_INDEX(
                ACGC_GX_CANONICAL_SECTION_ID_DYNAMIC),
            &section_bytes, &section_byte_size) ||
        !plan_decode_fixed_words(
            section_bytes, section_byte_size,
            &candidate.dynamic, sizeof(candidate.dynamic))) {
        return ACGC_APPLE_CANONICAL_PLAN_SECTION_DECODE;
    }
    if (!acgc_gx_canonical_dynamic_state_validate(&candidate.dynamic)) {
        return ACGC_APPLE_CANONICAL_PLAN_RESOURCE_METADATA;
    }
    if (!acgc_gx_canonical_texture_dynamic_validate(
            &candidate.texture, &candidate.dynamic)) {
        return ACGC_APPLE_CANONICAL_PLAN_RESOURCE_METADATA;
    }

    /* Rebuild Geometry now that Transform and Texgen values are decoded. */
    if (!plan_section_bytes(
            envelope_bytes, envelope_byte_size, &view,
            APPLE_CANONICAL_PLAN_SECTION_INDEX(
                ACGC_GX_CANONICAL_SECTION_ID_GEOMETRY),
            &section_bytes, &section_byte_size)) {
        return ACGC_APPLE_CANONICAL_PLAN_STRUCTURAL;
    }
    status = plan_decode_geometry(
        section_bytes,
        section_byte_size,
        &candidate.transform,
        &candidate.texgens,
        &candidate.geometry);
    if (status != ACGC_APPLE_CANONICAL_PLAN_OK) {
        return status;
    }
    if (!plan_build_geometry_dependencies(
            &candidate.geometry,
            &candidate.transform,
            &candidate.texgens,
            &candidate.channels,
            &candidate.lighting,
            &dependencies) ||
        !acgc_gx_canonical_geometry_state_validate_dependencies(
            section_bytes, section_byte_size, &dependencies)) {
        return ACGC_APPLE_CANONICAL_PLAN_DEPENDENCY;
    }
    if (!plan_validate_tev_texture_dependencies(
            &candidate.tev,
            &candidate.texture,
            &candidate.channels,
            &dependencies) ||
        !acgc_gx_canonical_indirect_state_validate_dependencies(
            &candidate.indirect,
            &candidate.tev,
            &candidate.texture,
            &dependencies)) {
        return ACGC_APPLE_CANONICAL_PLAN_DEPENDENCY;
    }

    *output = candidate;
    return ACGC_APPLE_CANONICAL_PLAN_OK;
}

const char* acgc_apple_canonical_plan_status_string(
    AcgcAppleCanonicalPlanStatus status
) {
    switch (status) {
        case ACGC_APPLE_CANONICAL_PLAN_OK:
            return "ok";
        case ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT:
            return "invalid-argument";
        case ACGC_APPLE_CANONICAL_PLAN_INPUT_OUTPUT_OVERLAP:
            return "input-output-overlap";
        case ACGC_APPLE_CANONICAL_PLAN_STRUCTURAL:
            return "structural";
        case ACGC_APPLE_CANONICAL_PLAN_SECTION_DECODE:
            return "section-decode";
        case ACGC_APPLE_CANONICAL_PLAN_SECTION_SEMANTIC:
            return "section-semantic";
        case ACGC_APPLE_CANONICAL_PLAN_DEPENDENCY:
            return "dependency";
        case ACGC_APPLE_CANONICAL_PLAN_RESOURCE_METADATA:
            return "resource-metadata";
        case ACGC_APPLE_CANONICAL_PLAN_UNSUPPORTED_BUMP:
            return "unsupported-bump";
        case ACGC_APPLE_CANONICAL_PLAN_GEOMETRY_LIMIT:
            return "geometry-limit";
        case ACGC_APPLE_CANONICAL_PLAN_OVERFLOW:
            return "overflow";
        default:
            return "unknown";
    }
}
