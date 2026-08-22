#include "pc_gx_geometry_dependencies.h"

#include <dolphin/gx/GXEnum.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    PC_GX_GEOMETRY_DEPENDENCY_DESCRIPTOR_VCD_KNOWN = 1,
    PC_GX_GEOMETRY_DEPENDENCY_DESCRIPTOR_KNOWN = 3
};

static int pc_gx_geometry_slot_is_supported(uint32_t slot) {
    return slot == GX_VA_POS || slot == GX_VA_NRM ||
        slot == GX_VA_CLR0 || slot == GX_VA_TEX0;
}

static int pc_gx_geometry_vcd_is_indexed(uint32_t vcd_type) {
    return vcd_type == GX_INDEX8 || vcd_type == GX_INDEX16;
}

static uint32_t pc_gx_geometry_expected_word_count(uint32_t slot) {
    if (slot == GX_VA_POS || slot == GX_VA_NRM) return 3;
    if (slot == GX_VA_CLR0) return 1;
    if (slot == GX_VA_TEX0) return 2;
    return 0;
}

static uint32_t pc_gx_geometry_scalar_bytes(uint32_t vat_type) {
    if (vat_type == GX_U8 || vat_type == GX_S8) return 1;
    if (vat_type == GX_U16 || vat_type == GX_S16) return 2;
    if (vat_type == GX_F32) return 4;
    return 0;
}

static uint32_t pc_gx_geometry_array_value_bytes(
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
    scalar_bytes = pc_gx_geometry_scalar_bytes(attribute->vat_type);
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

static int pc_gx_geometry_descriptor_format_is_valid(
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
            return type == GX_RGB565 || type == GX_RGB8 ||
                type == GX_RGBX8;
        }
        if (count == GX_CLR_RGBA) {
            return type == GX_RGBA4 || type == GX_RGBA6 ||
                type == GX_RGBA8;
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

static int pc_gx_geometry_value_is_canonicalizable(
    uint32_t slot,
    const PCGXRawGeometryAttribute* attribute,
    const uint32_t* raw_words
) {
    uint32_t canonical_words[PC_GX_GEOMETRY_MAX_VALUE_WORDS];
    uint32_t component_count;
    uint32_t word;

    if (raw_words == NULL ||
        attribute->value_word_count !=
            pc_gx_geometry_expected_word_count(slot)) {
        return 0;
    }
    for (word = attribute->value_word_count;
         word < PC_GX_GEOMETRY_MAX_VALUE_WORDS;
         word++) {
        if (raw_words[word] != 0) return 0;
    }
    memset(canonical_words, 0, sizeof(canonical_words));

    if (slot == GX_VA_CLR0) {
        return acgc_gx_canonical_geometry_decode_color_word(
            attribute->vat_count,
            attribute->vat_type,
            raw_words[0],
            &canonical_words[0]
        );
    }
    if (slot == GX_VA_NRM) {
        for (word = 0; word < 3; word++) {
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
    if (slot == GX_VA_POS) {
        component_count = attribute->vat_count == GX_POS_XY ? 2 : 3;
    }
    for (word = 0; word < component_count; word++) {
        if (!acgc_gx_canonical_geometry_decode_scalar_word(
                attribute->vat_type,
                attribute->vat_fraction,
                raw_words[word],
                &canonical_words[word])) {
            return 0;
        }
    }
    if (slot == GX_VA_POS && attribute->vat_count == GX_POS_XY &&
        raw_words[2] != 0) {
        return 0;
    }
    if (slot == GX_VA_TEX0 && attribute->vat_count == GX_TEX_S &&
        raw_words[1] != 0) {
        return 0;
    }
    return 1;
}

static int pc_gx_geometry_attribute_record_domains_are_valid(
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

static int pc_gx_geometry_attribute_records_are_valid(
    uint32_t slot,
    const PCGXRawGeometryAttribute* attribute,
    uint32_t vertex_count
) {
    uint32_t record;
    uint32_t vertex;
    uint32_t next_new;

    if (attribute->reserved[0] != 0 || attribute->reserved[1] != 0 ||
        (attribute->descriptor_known &
            ~(uint32_t)PC_GX_GEOMETRY_DEPENDENCY_DESCRIPTOR_KNOWN) != 0 ||
        (attribute->array_known &
            ~(uint32_t)(PC_GX_GEOMETRY_ARRAY_KNOWN |
                        PC_GX_GEOMETRY_ARRAY_DATA_KNOWN)) != 0 ||
        !pc_gx_geometry_descriptor_format_is_valid(slot, attribute)) {
        return 0;
    }
    if (attribute->value_count == 0 ||
        attribute->value_count > vertex_count) {
        return 0;
    }
    if (!pc_gx_geometry_attribute_record_domains_are_valid(
            attribute,
            attribute->value_count,
            attribute->index_count,
            attribute->vcd_type == GX_DIRECT)) {
        return 0;
    }
    for (record = 0; record < attribute->value_count; record++) {
        if (!pc_gx_geometry_value_is_canonicalizable(
                slot, attribute, attribute->value_words[record])) {
            return 0;
        }
    }

    if (attribute->vcd_type == GX_DIRECT) {
        return attribute->value_count == vertex_count &&
            attribute->index_count == 0 && attribute->index_stride == 0;
    }

    if (!pc_gx_geometry_vcd_is_indexed(attribute->vcd_type) ||
        attribute->array_known !=
            (PC_GX_GEOMETRY_ARRAY_KNOWN |
             PC_GX_GEOMETRY_ARRAY_DATA_KNOWN) ||
        attribute->index_count != vertex_count ||
        attribute->index_stride !=
            (attribute->vcd_type == GX_INDEX8 ? 1u : 2u) ||
        attribute->array_generation == 0) {
        return 0;
    }
    {
        uint32_t value_bytes =
            pc_gx_geometry_array_value_bytes(slot, attribute);

        if (value_bytes == 0 || attribute->array_stride < value_bytes) {
            return 0;
        }
        for (record = 0; record < attribute->value_count; record++) {
            uint64_t offset =
                (uint64_t)attribute->value_source_index[record] *
                attribute->array_stride;
            uint64_t end = offset + value_bytes;

            for (vertex = record + 1;
                 vertex < attribute->value_count;
                 vertex++) {
                if (attribute->value_source_index[record] ==
                    attribute->value_source_index[vertex]) {
                    return 0;
                }
            }
            if (end < offset || end > attribute->array_byte_size) {
                return 0;
            }
        }
    }
    next_new = 0;
    for (vertex = 0; vertex < vertex_count; vertex++) {
        uint32_t index = attribute->index_values[vertex];
        uint32_t prior_vertex;

        if (attribute->index_known[vertex] != 1 ||
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

static int pc_gx_geometry_batch_requirements(
    const PCGXRawGeometryBatch* batch,
    uint32_t* present_mask
) {
    uint32_t slot;

    if (batch == NULL || present_mask == NULL ||
        batch->active != 0 || batch->known != 1 || batch->invalid != 0 ||
        batch->expected_vertex_count != batch->vertex_count ||
        batch->vertex_count == 0 ||
        batch->vertex_count > PC_GX_GEOMETRY_MAX_VERTICES ||
        batch->vtxfmt >= ACGC_GX_CANONICAL_GEOMETRY_VTXFMT_COUNT ||
        (batch->primitive !=
            ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES &&
         batch->primitive !=
            ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS) ||
        (batch->primitive ==
            ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES &&
         batch->vertex_count % 3 != 0) ||
        (batch->primitive ==
            ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS &&
         batch->vertex_count % 4 != 0)) {
        return 0;
    }

    *present_mask = 0;
    for (slot = 0; slot < ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT;
         slot++) {
        const PCGXRawGeometryAttribute* attribute = &batch->attr[slot];

        if ((attribute->descriptor_known &
                ~(uint32_t)PC_GX_GEOMETRY_DEPENDENCY_DESCRIPTOR_KNOWN) != 0 ||
            (attribute->array_known &
                ~(uint32_t)(PC_GX_GEOMETRY_ARRAY_KNOWN |
                            PC_GX_GEOMETRY_ARRAY_DATA_KNOWN)) != 0 ||
            (attribute->descriptor_known &
                PC_GX_GEOMETRY_DEPENDENCY_DESCRIPTOR_VCD_KNOWN) == 0) {
            return 0;
        }
        if (attribute->vcd_type == GX_NONE) {
            if (attribute->value_word_count != 0 ||
                attribute->value_count != 0 ||
                attribute->index_count != 0 ||
                attribute->index_stride != 0 ||
                !pc_gx_geometry_attribute_record_domains_are_valid(
                    attribute, 0, 0, 0) ||
                attribute->reserved[0] != 0 ||
                attribute->reserved[1] != 0) {
                return 0;
            }
            continue;
        }
        if ((attribute->descriptor_known &
                PC_GX_GEOMETRY_DEPENDENCY_DESCRIPTOR_KNOWN) !=
            PC_GX_GEOMETRY_DEPENDENCY_DESCRIPTOR_KNOWN ||
            !pc_gx_geometry_slot_is_supported(slot) ||
            (attribute->vcd_type != GX_DIRECT &&
             !pc_gx_geometry_vcd_is_indexed(attribute->vcd_type)) ||
            !pc_gx_geometry_attribute_records_are_valid(
                slot, attribute, batch->vertex_count)) {
            return 0;
        }
        *present_mask |= UINT32_C(1) << slot;
    }
    return (*present_mask &
            (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS)) != 0;
}

static int pc_gx_geometry_position_id_to_slot(
    uint32_t id,
    uint32_t* slot
) {
    if (slot == NULL ||
        id < ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_FIRST ||
        id > ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_LAST ||
        (id - ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_FIRST) %
            ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_STRIDE != 0) {
        return 0;
    }
    *slot = (id - ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_FIRST) /
        ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_STRIDE;
    return *slot < ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_COUNT;
}

static int pc_gx_geometry_ordinary_id_to_slot(
    uint32_t id,
    uint32_t* slot
) {
    if (slot == NULL ||
        id < ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_FIRST ||
        id > ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_LAST ||
        (id - ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_FIRST) %
            ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_STRIDE != 0) {
        return 0;
    }
    *slot = (id - ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_FIRST) /
        ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_STRIDE;
    return *slot < ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_COUNT;
}

static int pc_gx_geometry_texgen_source_slot(
    uint32_t source,
    uint32_t* slot
) {
    if (slot == NULL) return 0;
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
                (source - ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX0);
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

static int pc_gx_geometry_texgen_is_bump(uint32_t function) {
    return function >= ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP0 &&
        function <= ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP7;
}

static int pc_gx_geometry_texgen_record_is_usable(
    const AcgcGxCanonicalTexgenState* texgens,
    uint32_t coord,
    uint32_t* selector
) {
    const AcgcGxCanonicalTexgenRecord* record;
    uint32_t ordinary_slot;

    if (texgens == NULL || selector == NULL ||
        coord >= ACGC_GX_CANONICAL_TEXGEN_COUNT ||
        coord >= texgens->header.active_texgen_count ||
        (texgens->header.texgen_known_mask & (UINT32_C(1) << coord)) == 0) {
        return 0;
    }
    record = &texgens->texgen[coord];
    if (record->component_known !=
            ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL ||
        !pc_gx_geometry_ordinary_id_to_slot(
            record->ordinary_matrix_id, &ordinary_slot) ||
        (texgens->header.ordinary_matrix_known_mask &
            (UINT32_C(1) << ordinary_slot)) == 0) {
        return 0;
    }
    *selector = record->ordinary_matrix_id;
    return 1;
}

static int pc_gx_geometry_channel_control_uses_vertex(
    const AcgcGxCanonicalChannelControl* control
) {
    return control != NULL &&
        control->enable == ACGC_GX_CANONICAL_CHANNEL_BOOLEAN_TRUE &&
        (control->ambient_source == ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX ||
         control->material_source == ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX);
}

int pc_gx_geometry_build_dependency_results(
    const PCGXRawGeometryBatch* batch,
    const AcgcGxCanonicalTransformState* transform,
    const AcgcGxCanonicalTexgenState* texgens,
    const AcgcGxCanonicalChannelState* channels,
    const AcgcGxCanonicalLightingState* lighting,
    AcgcGxCanonicalGeometryDependencyResults* output
) {
    AcgcGxCanonicalGeometryDependencyResults candidate;
    uint32_t present_mask;
    uint32_t required_geometry_mask;
    uint32_t required_channel_mask = 0;
    uint32_t required_lighting_mask = 0;
    uint32_t coord;
    uint32_t channel;
    uint32_t current_slot;

    if (output == NULL ||
        !acgc_gx_canonical_transform_state_validate(transform) ||
        !acgc_gx_canonical_texgen_state_validate(texgens) ||
        !acgc_gx_canonical_channel_state_validate(channels) ||
        !acgc_gx_canonical_lighting_state_validate(lighting) ||
        !pc_gx_geometry_batch_requirements(batch, &present_mask)) {
        return 0;
    }

    memset(&candidate, 0, sizeof(candidate));
    candidate.transform_valid = 1;
    candidate.texgens_valid = 1;
    candidate.channels_valid = 1;
    candidate.lighting_valid = 1;
    candidate.transform_position_known_mask =
        (transform->known_mask >> 2) & UINT32_C(0x3FF);
    candidate.transform_normal_known_mask =
        (transform->known_mask >> 12) & UINT32_C(0x3FF);
    candidate.transform_current_position_known =
        (transform->known_mask &
            ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_KNOWN_MASK) != 0;
    candidate.transform_current_position_id =
        candidate.transform_current_position_known != 0
            ? transform->current_position_id : 0;
    candidate.texgen_ordinary_known_mask =
        texgens->header.ordinary_matrix_known_mask;
    candidate.texgen_post_known_mask =
        texgens->header.post_matrix_known_mask;
    candidate.lighting_loaded_mask = lighting->loaded_mask;

    required_geometry_mask = present_mask;
    if (!candidate.transform_current_position_known ||
        !pc_gx_geometry_position_id_to_slot(
            candidate.transform_current_position_id, &current_slot) ||
        (candidate.transform_position_known_mask &
            (UINT32_C(1) << current_slot)) == 0) {
        return 0;
    }

    if ((present_mask &
            (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM)) != 0 &&
        (candidate.transform_normal_known_mask &
            (UINT32_C(1) << current_slot)) == 0) {
        return 0;
    }

    for (coord = 0; coord < texgens->header.active_texgen_count; coord++) {
        const AcgcGxCanonicalTexgenRecord* record;
        uint32_t selector;

        candidate.texgen_present_mask |= UINT32_C(1) << coord;
        if (!pc_gx_geometry_texgen_record_is_usable(
                texgens, coord, &selector)) {
            return 0;
        }
        candidate.texgen_selector[coord] = selector;
        record = &texgens->texgen[coord];
        if (pc_gx_geometry_texgen_is_bump(record->function)) {
            /*
             * The exact predecessor is the Bump/Indirect state captured by
             * decomp src/static/dolphin/gx/GXBump.c; no such canonical input
             * is available at this boundary.
             */
            return 0;
        }
    }

    for (coord = 0; coord < ACGC_GX_CANONICAL_TEXGEN_COUNT; coord++) {
        const uint32_t texcoord_bit = UINT32_C(1) <<
            (ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0 + coord);
        uint32_t source_slot;
        const AcgcGxCanonicalTexgenRecord* record;

        if ((present_mask & texcoord_bit) == 0) continue;
        if (coord >= texgens->header.active_texgen_count) {
            return 0;
        }
        record = &texgens->texgen[coord];
        if (!pc_gx_geometry_texgen_source_slot(
                record->source, &source_slot) ||
            (present_mask & (UINT32_C(1) << source_slot)) == 0) {
            return 0;
        }
        required_geometry_mask |= UINT32_C(1) << source_slot;
    }

    for (channel = 0; channel < channels->active_count; channel++) {
        const AcgcGxCanonicalChannelRecord* record =
            &channels->records[channel];
        const AcgcGxCanonicalChannelControl* color = &record->color;
        const AcgcGxCanonicalChannelControl* alpha = &record->alpha;

        if (pc_gx_geometry_channel_control_uses_vertex(color) ||
            pc_gx_geometry_channel_control_uses_vertex(alpha)) {
            const uint32_t color_slot =
                ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0 + channel;

            required_channel_mask |= UINT32_C(1) << channel;
            if ((present_mask & (UINT32_C(1) << color_slot)) == 0) {
                return 0;
            }
            required_geometry_mask |= UINT32_C(1) << color_slot;
        }
        if (color->enable == ACGC_GX_CANONICAL_CHANNEL_BOOLEAN_TRUE) {
            required_lighting_mask |= color->light_mask;
        }
        if (alpha->enable == ACGC_GX_CANONICAL_CHANNEL_BOOLEAN_TRUE) {
            required_lighting_mask |= alpha->light_mask;
        }
    }

    if (required_lighting_mask != 0) {
        if ((present_mask &
                (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM)) == 0 ||
            (lighting->loaded_mask & required_lighting_mask) !=
                required_lighting_mask ||
            (candidate.transform_normal_known_mask &
                (UINT32_C(1) << current_slot)) == 0) {
            return 0;
        }
        required_geometry_mask |=
            UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM;
    }

    candidate.required_geometry_present_mask = required_geometry_mask;
    candidate.required_channel_mask = required_channel_mask;
    candidate.required_lighting_mask = required_lighting_mask;
    candidate.required_bump_mask = 0;
    candidate.bump_valid = 0;
    candidate.bump_known_mask = 0;

    *output = candidate;
    return 1;
}
