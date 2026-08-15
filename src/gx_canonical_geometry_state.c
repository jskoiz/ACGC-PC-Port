#include "acgc/gx_canonical_geometry_state.h"

#include <string.h>

typedef struct AcgcGxCanonicalGeometryDecodedDescriptor {
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
} AcgcGxCanonicalGeometryDecodedDescriptor;

static uint32_t canonical_geometry_read_le32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static uint16_t canonical_geometry_read_le16(const uint8_t* bytes) {
    return (uint16_t)((uint16_t)bytes[0] |
        ((uint16_t)bytes[1] << 8));
}

static int canonical_geometry_u64_add(
    uint64_t left,
    uint64_t right,
    uint64_t* result
) {
    if (result == NULL || right > UINT64_MAX - left) {
        return 0;
    }
    *result = left + right;
    return 1;
}

static int canonical_geometry_u64_mul(
    uint64_t left,
    uint64_t right,
    uint64_t* result
) {
    if (result == NULL || (left != 0 && right > UINT64_MAX / left)) {
        return 0;
    }
    *result = left * right;
    return 1;
}

static int canonical_geometry_u64_align4(
    uint64_t value,
    uint64_t* result
) {
    uint64_t rounded;

    if (!canonical_geometry_u64_add(value, UINT64_C(3), &rounded) ||
        result == NULL) {
        return 0;
    }
    *result = rounded & ~UINT64_C(3);
    return 1;
}

static int canonical_geometry_range_is_valid(
    uint64_t offset,
    uint64_t length,
    uint64_t extent
) {
    uint64_t end;

    return canonical_geometry_u64_add(offset, length, &end) &&
        offset <= extent && end <= extent;
}

static int canonical_geometry_bytes_are_zero(
    const uint8_t* section_bytes,
    uint64_t begin,
    uint64_t end
) {
    uint64_t index;

    if (section_bytes == NULL || begin > end) {
        return 0;
    }
    for (index = begin; index < end; index++) {
        if (section_bytes[(size_t)index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int canonical_geometry_binary32_is_finite(uint32_t bits) {
    return (bits & UINT32_C(0x7F800000)) != UINT32_C(0x7F800000);
}

/* Compare finite binary32 bit patterns without using a host float. */
static int canonical_geometry_binary32_less(uint32_t left, uint32_t right) {
    const uint32_t left_magnitude = left & UINT32_C(0x7FFFFFFF);
    const uint32_t right_magnitude = right & UINT32_C(0x7FFFFFFF);
    const int left_negative = (left & UINT32_C(0x80000000)) != 0;
    const int right_negative = (right & UINT32_C(0x80000000)) != 0;

    if (left_magnitude == 0 && right_magnitude == 0) {
        return 0;
    }
    if (left_negative != right_negative) {
        return left_negative;
    }
    if (left_negative) {
        return left_magnitude > right_magnitude;
    }
    return left_magnitude < right_magnitude;
}

/*
 * Convert a small exact rational to binary32 using round-to-nearest-even.
 * All callers use bounded GX integer ranges, so the normal-number path is
 * sufficient and every shift remains inside uint64_t.
 */
static uint32_t canonical_geometry_binary32_from_rational(
    int64_t numerator,
    uint64_t denominator
) {
    uint64_t magnitude;
    uint64_t scaled_numerator;
    uint64_t scaled_denominator;
    uint64_t quotient;
    uint64_t remainder;
    uint64_t rounding_denominator;
    uint64_t rounding_remainder;
    int exponent = 0;
    int shift;
    uint32_t sign = 0;

    if (numerator == 0) {
        return 0;
    }
    if (numerator < 0) {
        sign = UINT32_C(0x80000000);
        magnitude = (uint64_t)(-(numerator + 1)) + UINT64_C(1);
    } else {
        magnitude = (uint64_t)numerator;
    }

    /* Find e such that 2^e <= magnitude/denominator < 2^(e+1). */
    scaled_numerator = magnitude;
    scaled_denominator = denominator;
    if (scaled_numerator >= scaled_denominator) {
        while (scaled_numerator >= scaled_denominator * UINT64_C(2)) {
            scaled_denominator *= UINT64_C(2);
            exponent++;
        }
    } else {
        while (scaled_numerator < scaled_denominator) {
            scaled_numerator *= UINT64_C(2);
            exponent--;
        }
    }

    shift = 23 - exponent;
    if (shift >= 0) {
        rounding_denominator = denominator;
        rounding_remainder = magnitude << (uint32_t)shift;
        quotient = rounding_remainder / rounding_denominator;
        remainder = rounding_remainder % rounding_denominator;
    } else {
        rounding_denominator = denominator << (uint32_t)(-shift);
        quotient = magnitude / rounding_denominator;
        remainder = magnitude % rounding_denominator;
    }

    if (remainder > rounding_denominator / UINT64_C(2) ||
        (remainder == rounding_denominator / UINT64_C(2) &&
         (rounding_denominator % UINT64_C(2)) == 0 &&
         (quotient & UINT64_C(1)) != 0)) {
        quotient++;
    }

    if (quotient >= (UINT64_C(1) << 24)) {
        quotient >>= 1;
        exponent++;
    }

    return sign |
        ((uint32_t)(exponent + 127) << 23) |
        ((uint32_t)quotient & UINT32_C(0x007FFFFF));
}

static int canonical_geometry_scalar_raw_is_valid(
    uint32_t vat_type,
    uint32_t raw_value,
    int64_t* signed_value,
    uint64_t* unsigned_value
) {
    if (signed_value == NULL || unsigned_value == NULL) {
        return 0;
    }

    switch (vat_type) {
        case ACGC_GX_CANONICAL_GEOMETRY_COMP_U8:
            if ((raw_value & ~UINT32_C(0xFF)) != 0) {
                return 0;
            }
            *unsigned_value = raw_value;
            *signed_value = (int64_t)raw_value;
            return 1;
        case ACGC_GX_CANONICAL_GEOMETRY_COMP_S8:
            if ((raw_value & ~UINT32_C(0xFF)) != 0) {
                return 0;
            }
            *signed_value = (int64_t)(int8_t)raw_value;
            *unsigned_value = (uint64_t)(*signed_value);
            return 1;
        case ACGC_GX_CANONICAL_GEOMETRY_COMP_U16:
            if ((raw_value & ~UINT32_C(0xFFFF)) != 0) {
                return 0;
            }
            *unsigned_value = raw_value;
            *signed_value = (int64_t)raw_value;
            return 1;
        case ACGC_GX_CANONICAL_GEOMETRY_COMP_S16:
            if ((raw_value & ~UINT32_C(0xFFFF)) != 0) {
                return 0;
            }
            *signed_value = (int64_t)(int16_t)raw_value;
            *unsigned_value = (uint64_t)(*signed_value);
            return 1;
        default:
            return 0;
    }
}

int acgc_gx_canonical_geometry_decode_scalar_word(
    uint32_t vat_type,
    uint32_t vat_fraction,
    uint32_t raw_value,
    uint32_t* canonical_word
) {
    int64_t signed_value;
    uint64_t unsigned_value;
    uint64_t denominator;

    if (canonical_word == NULL) {
        return 0;
    }
    if (vat_type == ACGC_GX_CANONICAL_GEOMETRY_COMP_F32) {
        if (!canonical_geometry_binary32_is_finite(raw_value)) {
            return 0;
        }
        /* GX ignores the fixed-point argument for F32. */
        *canonical_word = raw_value;
        return 1;
    }
    if (vat_fraction > 31 ||
        !canonical_geometry_scalar_raw_is_valid(
            vat_type, raw_value, &signed_value, &unsigned_value)) {
        return 0;
    }

    denominator = UINT64_C(1) << vat_fraction;
    if (vat_type == ACGC_GX_CANONICAL_GEOMETRY_COMP_U8 ||
        vat_type == ACGC_GX_CANONICAL_GEOMETRY_COMP_U16) {
        *canonical_word = canonical_geometry_binary32_from_rational(
            (int64_t)unsigned_value,
            denominator
        );
    } else {
        *canonical_word = canonical_geometry_binary32_from_rational(
            signed_value,
            denominator
        );
    }
    return 1;
}

int acgc_gx_canonical_geometry_decode_normal_word(
    uint32_t vat_type,
    uint32_t raw_value,
    uint32_t* canonical_word
) {
    int64_t signed_value;
    uint64_t unused_unsigned_value;
    uint64_t denominator;

    if (canonical_word == NULL) {
        return 0;
    }
    if (vat_type == ACGC_GX_CANONICAL_GEOMETRY_COMP_F32) {
        if (!canonical_geometry_binary32_is_finite(raw_value)) {
            return 0;
        }
        *canonical_word = raw_value;
        return 1;
    }
    if (!canonical_geometry_scalar_raw_is_valid(
            vat_type,
            raw_value,
            &signed_value,
            &unused_unsigned_value)) {
        return 0;
    }
    if (vat_type == ACGC_GX_CANONICAL_GEOMETRY_COMP_S8) {
        denominator = UINT64_C(127);
    } else if (vat_type == ACGC_GX_CANONICAL_GEOMETRY_COMP_S16) {
        denominator = UINT64_C(32767);
    } else {
        return 0;
    }

    *canonical_word = canonical_geometry_binary32_from_rational(
        signed_value,
        denominator
    );
    return 1;
}

static uint32_t canonical_geometry_expand4(uint32_t value) {
    return (value << 4) | value;
}

static uint32_t canonical_geometry_expand5(uint32_t value) {
    return (value << 3) | (value >> 2);
}

static uint32_t canonical_geometry_expand6(uint32_t value) {
    return (value << 2) | (value >> 4);
}

int acgc_gx_canonical_geometry_decode_color_word(
    uint32_t vat_count,
    uint32_t vat_type,
    uint32_t raw_value,
    uint32_t* canonical_word
) {
    uint32_t red;
    uint32_t green;
    uint32_t blue;
    uint32_t alpha;

    if (canonical_word == NULL) {
        return 0;
    }

    switch (vat_type) {
        case ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGB565:
            if (vat_count != ACGC_GX_CANONICAL_GEOMETRY_CLR_RGB ||
                (raw_value & ~UINT32_C(0xFFFF)) != 0) {
                return 0;
            }
            red = canonical_geometry_expand5((raw_value >> 11) & 0x1F);
            green = canonical_geometry_expand6((raw_value >> 5) & 0x3F);
            blue = canonical_geometry_expand5(raw_value & 0x1F);
            alpha = 0xFF;
            break;
        case ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGB8:
            if (vat_count != ACGC_GX_CANONICAL_GEOMETRY_CLR_RGB ||
                (raw_value & ~UINT32_C(0x00FFFFFF)) != 0) {
                return 0;
            }
            red = (raw_value >> 16) & 0xFF;
            green = (raw_value >> 8) & 0xFF;
            blue = raw_value & 0xFF;
            alpha = 0xFF;
            break;
        case ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBX8:
            if (vat_count != ACGC_GX_CANONICAL_GEOMETRY_CLR_RGB) {
                return 0;
            }
            red = (raw_value >> 24) & 0xFF;
            green = (raw_value >> 16) & 0xFF;
            blue = (raw_value >> 8) & 0xFF;
            alpha = 0xFF;
            break;
        case ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA4:
            if (vat_count != ACGC_GX_CANONICAL_GEOMETRY_CLR_RGBA ||
                (raw_value & ~UINT32_C(0xFFFF)) != 0) {
                return 0;
            }
            red = canonical_geometry_expand4((raw_value >> 12) & 0xF);
            green = canonical_geometry_expand4((raw_value >> 8) & 0xF);
            blue = canonical_geometry_expand4((raw_value >> 4) & 0xF);
            alpha = canonical_geometry_expand4(raw_value & 0xF);
            break;
        case ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA6:
            if (vat_count != ACGC_GX_CANONICAL_GEOMETRY_CLR_RGBA ||
                (raw_value & ~UINT32_C(0x00FFFFFF)) != 0) {
                return 0;
            }
            red = canonical_geometry_expand6((raw_value >> 18) & 0x3F);
            green = canonical_geometry_expand6((raw_value >> 12) & 0x3F);
            blue = canonical_geometry_expand6((raw_value >> 6) & 0x3F);
            alpha = canonical_geometry_expand6(raw_value & 0x3F);
            break;
        case ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA8:
            if (vat_count != ACGC_GX_CANONICAL_GEOMETRY_CLR_RGBA) {
                return 0;
            }
            red = (raw_value >> 24) & 0xFF;
            green = (raw_value >> 16) & 0xFF;
            blue = (raw_value >> 8) & 0xFF;
            alpha = raw_value & 0xFF;
            break;
        default:
            return 0;
    }

    *canonical_word = red | (green << 8) | (blue << 16) |
        (alpha << 24);
    return 1;
}

static int canonical_geometry_is_matrix_slot(uint32_t slot) {
    return slot <= ACGC_GX_CANONICAL_GEOMETRY_MATRIX_ATTR_LAST;
}

static int canonical_geometry_is_array_slot(uint32_t slot) {
    return slot >= ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS_MTX_ARRAY &&
        slot <= ACGC_GX_CANONICAL_GEOMETRY_ATTR_LIGHT_ARRAY;
}

static int canonical_geometry_is_position_slot(uint32_t slot) {
    return slot == ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS;
}

static int canonical_geometry_is_normal_slot(uint32_t slot) {
    return slot == ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM ||
        slot == ACGC_GX_CANONICAL_GEOMETRY_ATTR_NBT;
}

static int canonical_geometry_is_color_slot(uint32_t slot) {
    return slot == ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0 ||
        slot == ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR1;
}

static int canonical_geometry_is_texcoord_slot(uint32_t slot) {
    return slot >= ACGC_GX_CANONICAL_GEOMETRY_TEX_ATTR_FIRST &&
        slot <= ACGC_GX_CANONICAL_GEOMETRY_TEX_ATTR_LAST;
}

static int canonical_geometry_is_indexed(uint32_t vcd_type) {
    return vcd_type == ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX8 ||
        vcd_type == ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX16;
}

static uint32_t canonical_geometry_descriptor_word(
    const AcgcGxCanonicalGeometryDecodedDescriptor* descriptor,
    uint32_t word
) {
    switch (word) {
        case 0: return descriptor->vcd_type;
        case 1: return descriptor->vat_count;
        case 2: return descriptor->vat_type;
        case 3: return descriptor->vat_fraction;
        case 4: return descriptor->value_encoding;
        case 5: return descriptor->canonical_word_count;
        case 6: return descriptor->value_offset;
        case 7: return descriptor->value_bytes;
        case 8: return descriptor->value_stride;
        case 9: return descriptor->value_count;
        case 10: return descriptor->index_offset;
        case 11: return descriptor->index_bytes;
        case 12: return descriptor->index_stride;
        case 13: return descriptor->index_count;
        case 14: return descriptor->reserved[0];
        case 15: return descriptor->reserved[1];
        default: return UINT32_C(0xFFFFFFFF);
    }
}

static void canonical_geometry_read_descriptor(
    const uint8_t* section_bytes,
    uint32_t slot,
    AcgcGxCanonicalGeometryDecodedDescriptor* descriptor
) {
    const size_t base = (size_t)ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_OFFSET +
        (size_t)slot * ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_SIZE;

    descriptor->vcd_type = canonical_geometry_read_le32(
        section_bytes + base + ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VCD_TYPE_OFFSET);
    descriptor->vat_count = canonical_geometry_read_le32(
        section_bytes + base + ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VAT_COUNT_OFFSET);
    descriptor->vat_type = canonical_geometry_read_le32(
        section_bytes + base + ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VAT_TYPE_OFFSET);
    descriptor->vat_fraction = canonical_geometry_read_le32(
        section_bytes + base + ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VAT_FRACTION_OFFSET);
    descriptor->value_encoding = canonical_geometry_read_le32(
        section_bytes + base + ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_ENCODING_OFFSET);
    descriptor->canonical_word_count = canonical_geometry_read_le32(
        section_bytes + base + ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_CANONICAL_WORD_COUNT_OFFSET);
    descriptor->value_offset = canonical_geometry_read_le32(
        section_bytes + base + ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_OFFSET);
    descriptor->value_bytes = canonical_geometry_read_le32(
        section_bytes + base + ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_BYTES_OFFSET);
    descriptor->value_stride = canonical_geometry_read_le32(
        section_bytes + base + ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_STRIDE_OFFSET);
    descriptor->value_count = canonical_geometry_read_le32(
        section_bytes + base + ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_COUNT_OFFSET);
    descriptor->index_offset = canonical_geometry_read_le32(
        section_bytes + base + ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_OFFSET);
    descriptor->index_bytes = canonical_geometry_read_le32(
        section_bytes + base + ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_BYTES_OFFSET);
    descriptor->index_stride = canonical_geometry_read_le32(
        section_bytes + base + ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_STRIDE_OFFSET);
    descriptor->index_count = canonical_geometry_read_le32(
        section_bytes + base + ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_COUNT_OFFSET);
    descriptor->reserved[0] = canonical_geometry_read_le32(
        section_bytes + base + ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_RESERVED0_OFFSET);
    descriptor->reserved[1] = canonical_geometry_read_le32(
        section_bytes + base + ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_RESERVED1_OFFSET);
}

static int canonical_geometry_scalar_format_is_valid(
    uint32_t vat_type,
    uint32_t vat_fraction
) {
    if (vat_type > ACGC_GX_CANONICAL_GEOMETRY_COMP_F32) {
        return 0;
    }
    /* F32's hardware-ignored argument is zero in canonical storage. */
    return vat_type == ACGC_GX_CANONICAL_GEOMETRY_COMP_F32
        ? vat_fraction == 0
        : vat_fraction <= 31;
}

static int canonical_geometry_descriptor_format_is_valid(
    uint32_t slot,
    const AcgcGxCanonicalGeometryDecodedDescriptor* descriptor
) {
    if (canonical_geometry_is_matrix_slot(slot)) {
        return descriptor->vat_count == 0 && descriptor->vat_type == 0 &&
            descriptor->vat_fraction == 0;
    }
    if (canonical_geometry_is_position_slot(slot)) {
        return (descriptor->vat_count ==
                    ACGC_GX_CANONICAL_GEOMETRY_POS_XY ||
                descriptor->vat_count ==
                    ACGC_GX_CANONICAL_GEOMETRY_POS_XYZ) &&
            canonical_geometry_scalar_format_is_valid(
                descriptor->vat_type,
                descriptor->vat_fraction);
    }
    if (canonical_geometry_is_normal_slot(slot)) {
        if (descriptor->vat_count != ACGC_GX_CANONICAL_GEOMETRY_NRM_XYZ &&
            descriptor->vat_count != ACGC_GX_CANONICAL_GEOMETRY_NRM_NBT) {
            return 0;
        }
        if (descriptor->vat_type != ACGC_GX_CANONICAL_GEOMETRY_COMP_S8 &&
            descriptor->vat_type != ACGC_GX_CANONICAL_GEOMETRY_COMP_S16 &&
            descriptor->vat_type != ACGC_GX_CANONICAL_GEOMETRY_COMP_F32) {
            return 0;
        }
        /* GX ignores frac for normal attributes; canonical storage is zero. */
        return descriptor->vat_fraction == 0;
    }
    if (canonical_geometry_is_color_slot(slot)) {
        /* GX ignores frac for packed colors; canonical storage is zero. */
        if (descriptor->vat_fraction != 0) {
            return 0;
        }
        if (descriptor->vat_count == ACGC_GX_CANONICAL_GEOMETRY_CLR_RGB) {
            return descriptor->vat_type ==
                    ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGB565 ||
                descriptor->vat_type ==
                    ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGB8 ||
                descriptor->vat_type ==
                    ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBX8;
        }
        if (descriptor->vat_count == ACGC_GX_CANONICAL_GEOMETRY_CLR_RGBA) {
            return descriptor->vat_type ==
                    ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA4 ||
                descriptor->vat_type ==
                    ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA6 ||
                descriptor->vat_type ==
                    ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA8;
        }
        return 0;
    }
    if (canonical_geometry_is_texcoord_slot(slot)) {
        return (descriptor->vat_count == ACGC_GX_CANONICAL_GEOMETRY_TEX_S ||
                descriptor->vat_count == ACGC_GX_CANONICAL_GEOMETRY_TEX_ST) &&
            canonical_geometry_scalar_format_is_valid(
                descriptor->vat_type,
                descriptor->vat_fraction);
    }
    return 0;
}

static uint32_t canonical_geometry_expected_word_count(
    uint32_t slot,
    const AcgcGxCanonicalGeometryDecodedDescriptor* descriptor
) {
    if (canonical_geometry_is_matrix_slot(slot)) {
        return 1;
    }
    if (canonical_geometry_is_position_slot(slot)) {
        return 3;
    }
    if (canonical_geometry_is_normal_slot(slot)) {
        return descriptor->vat_count == ACGC_GX_CANONICAL_GEOMETRY_NRM_NBT
            ? 9
            : 3;
    }
    if (canonical_geometry_is_color_slot(slot)) {
        return 1;
    }
    if (canonical_geometry_is_texcoord_slot(slot)) {
        return 2;
    }
    return 0;
}

static int canonical_geometry_position_id_is_exact(uint32_t value) {
    uint32_t slot;

    for (slot = 0;
         slot < ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_COUNT;
         slot++) {
        if (value == slot *
                ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_STRIDE) {
            return 1;
        }
    }
    return 0;
}

static int canonical_geometry_ordinary_texture_id_to_record(
    uint32_t value,
    uint32_t* record
) {
    uint32_t index;

    if (record == NULL) {
        return 0;
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_COUNT;
         index++) {
        if (value ==
                ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_FIRST +
                    index *
                        ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_STRIDE) {
            *record = index;
            return 1;
        }
    }
    return 0;
}

/*
 * Check exact representability in the source quantizer rather than merely
 * checking the numeric range.  The source domains are monotone under the
 * canonical round-to-nearest-even conversion, so a bounded binary search
 * avoids accepting values such as U8 frac0 = 0.5f without scanning a host
 * float or relying on host rounding.
 */
static int canonical_geometry_integer_word_is_exact(
    uint32_t word,
    uint32_t vat_type,
    uint32_t vat_fraction
) {
    int32_t low;
    int32_t high;
    uint64_t denominator;

    if (!canonical_geometry_binary32_is_finite(word) ||
        vat_fraction > 31) {
        return 0;
    }
    switch (vat_type) {
        case ACGC_GX_CANONICAL_GEOMETRY_COMP_U8:
            low = 0;
            high = 255;
            break;
        case ACGC_GX_CANONICAL_GEOMETRY_COMP_S8:
            low = -128;
            high = 127;
            break;
        case ACGC_GX_CANONICAL_GEOMETRY_COMP_U16:
            low = 0;
            high = 65535;
            break;
        case ACGC_GX_CANONICAL_GEOMETRY_COMP_S16:
            low = -32768;
            high = 32767;
            break;
        default:
            return 0;
    }
    denominator = UINT64_C(1) << vat_fraction;
    while (low <= high) {
        const int32_t candidate = low + (high - low) / 2;
        const uint32_t candidate_word =
            canonical_geometry_binary32_from_rational(
                candidate, denominator);

        if (candidate_word == word) {
            return 1;
        }
        if (canonical_geometry_binary32_less(candidate_word, word)) {
            low = candidate + 1;
        } else if (canonical_geometry_binary32_less(word, candidate_word)) {
            high = candidate - 1;
        } else {
            /* The only numerical tie with distinct bits here is +/- zero. */
            return 0;
        }
    }
    return 0;
}

static int canonical_geometry_normal_word_is_exact(
    uint32_t word,
    uint32_t vat_type
) {
    int32_t low;
    int32_t high;
    uint64_t denominator;

    if (!canonical_geometry_binary32_is_finite(word)) {
        return 0;
    }
    if (vat_type == ACGC_GX_CANONICAL_GEOMETRY_COMP_F32) {
        return 1;
    }
    if (vat_type == ACGC_GX_CANONICAL_GEOMETRY_COMP_S8) {
        low = -128;
        high = 127;
        denominator = UINT64_C(127);
    } else if (vat_type == ACGC_GX_CANONICAL_GEOMETRY_COMP_S16) {
        low = -32768;
        high = 32767;
        denominator = UINT64_C(32767);
    } else {
        return 0;
    }
    while (low <= high) {
        const int32_t candidate = low + (high - low) / 2;
        const uint32_t candidate_word =
            canonical_geometry_binary32_from_rational(
                candidate, denominator);

        if (candidate_word == word) {
            return 1;
        }
        if (canonical_geometry_binary32_less(candidate_word, word)) {
            low = candidate + 1;
        } else if (canonical_geometry_binary32_less(word, candidate_word)) {
            high = candidate - 1;
        } else {
            return 0;
        }
    }
    return 0;
}

static uint32_t canonical_geometry_color_byte(
    uint32_t word,
    uint32_t shift
) {
    return (word >> shift) & UINT32_C(0xFF);
}

static int canonical_geometry_color_word_is_exact(
    const AcgcGxCanonicalGeometryDecodedDescriptor* descriptor,
    uint32_t word
) {
    const uint32_t red = canonical_geometry_color_byte(word, 0);
    const uint32_t green = canonical_geometry_color_byte(word, 8);
    const uint32_t blue = canonical_geometry_color_byte(word, 16);
    const uint32_t alpha = canonical_geometry_color_byte(word, 24);

    if (descriptor->vat_count == ACGC_GX_CANONICAL_GEOMETRY_CLR_RGB) {
        if (alpha != UINT32_C(0xFF)) {
            return 0;
        }
        if (descriptor->vat_type ==
                ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGB565) {
            return red == canonical_geometry_expand5(red >> 3) &&
                green == canonical_geometry_expand6(green >> 2) &&
                blue == canonical_geometry_expand5(blue >> 3);
        }
        return descriptor->vat_type ==
                ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGB8 ||
            descriptor->vat_type ==
                ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBX8;
    }
    if (descriptor->vat_type == ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA4) {
        return red == canonical_geometry_expand4(red >> 4) &&
            green == canonical_geometry_expand4(green >> 4) &&
            blue == canonical_geometry_expand4(blue >> 4) &&
            alpha == canonical_geometry_expand4(alpha >> 4);
    }
    if (descriptor->vat_type == ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA6) {
        return red == canonical_geometry_expand6(red >> 2) &&
            green == canonical_geometry_expand6(green >> 2) &&
            blue == canonical_geometry_expand6(blue >> 2) &&
            alpha == canonical_geometry_expand6(alpha >> 2);
    }
    return descriptor->vat_type == ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA8;
}

static int canonical_geometry_value_record_is_valid(
    const uint8_t* section_bytes,
    uint32_t slot,
    const AcgcGxCanonicalGeometryDecodedDescriptor* descriptor,
    uint32_t record_index
) {
    const uint64_t record_offset = (uint64_t)descriptor->value_offset +
        (uint64_t)record_index * descriptor->value_stride;
    const size_t byte_offset = (size_t)record_offset;
    uint32_t word;
    uint32_t component;

    if (canonical_geometry_is_matrix_slot(slot)) {
        word = canonical_geometry_read_le32(section_bytes + byte_offset);
        if (slot == ACGC_GX_CANONICAL_GEOMETRY_ATTR_PNMTXIDX) {
            return canonical_geometry_position_id_is_exact(word);
        }
        return canonical_geometry_ordinary_texture_id_to_record(word, &component);
    }
    if (canonical_geometry_is_color_slot(slot)) {
        word = canonical_geometry_read_le32(section_bytes + byte_offset);
        return canonical_geometry_color_word_is_exact(descriptor, word);
    }
    for (component = 0;
         component < descriptor->canonical_word_count;
         component++) {
        word = canonical_geometry_read_le32(
            section_bytes + byte_offset + (size_t)component * 4);
        if (canonical_geometry_is_normal_slot(slot)) {
            if (!canonical_geometry_normal_word_is_exact(
                    word, descriptor->vat_type)) {
                return 0;
            }
        } else if (descriptor->vat_type ==
                ACGC_GX_CANONICAL_GEOMETRY_COMP_F32) {
            if (!canonical_geometry_binary32_is_finite(word)) {
                return 0;
            }
        } else if (!canonical_geometry_integer_word_is_exact(
                       word,
                       descriptor->vat_type,
                       descriptor->vat_fraction)) {
            return 0;
        }
        if (canonical_geometry_is_position_slot(slot) &&
            descriptor->vat_count == ACGC_GX_CANONICAL_GEOMETRY_POS_XY &&
            component == 2 && word != 0) {
            return 0;
        }
        if (canonical_geometry_is_texcoord_slot(slot) &&
            descriptor->vat_count == ACGC_GX_CANONICAL_GEOMETRY_TEX_S &&
            component == 1 && word != 0) {
            return 0;
        }
    }
    return 1;
}

static int canonical_geometry_index_stream_is_valid(
    const uint8_t* section_bytes,
    const AcgcGxCanonicalGeometryDecodedDescriptor* descriptor
) {
    uint32_t seen[4] = {0, 0, 0, 0};
    uint32_t next_new = 0;
    uint32_t index;

    for (index = 0; index < descriptor->index_count; index++) {
        const uint64_t offset = (uint64_t)descriptor->index_offset +
            (uint64_t)index * descriptor->index_stride;
        uint32_t value;
        uint32_t word;

        if (descriptor->index_stride == 1) {
            value = section_bytes[(size_t)offset];
        } else {
            value = canonical_geometry_read_le16(
                section_bytes + (size_t)offset);
        }
        if (value >= descriptor->value_count) {
            return 0;
        }
        word = value / 32;
        if ((seen[word] & (UINT32_C(1) << (value % 32))) == 0) {
            if (value != next_new || next_new >= descriptor->value_count) {
                return 0;
            }
            seen[word] |= UINT32_C(1) << (value % 32);
            next_new++;
        }
    }
    return next_new == descriptor->value_count;
}

static int canonical_geometry_read_header(
    const uint8_t* section_bytes,
    size_t section_byte_size,
    uint32_t* stream_bytes,
    uint32_t* primitive,
    uint32_t* vertex_count,
    uint32_t* present_mask,
    uint32_t* indexed_mask
) {
    uint32_t vtxfmt;
    uint64_t stream_end;

    if (section_bytes == NULL ||
        section_byte_size < ACGC_GX_CANONICAL_GEOMETRY_MIN_SECTION_SIZE ||
        (uint64_t)section_byte_size >
            ACGC_GX_CANONICAL_GEOMETRY_MAX_SECTION_SIZE ||
        stream_bytes == NULL || primitive == NULL || vertex_count == NULL ||
        present_mask == NULL || indexed_mask == NULL) {
        return 0;
    }
    vtxfmt = canonical_geometry_read_le32(
        section_bytes + ACGC_GX_CANONICAL_GEOMETRY_HEADER_VTXFMT_OFFSET);
    if (vtxfmt >= ACGC_GX_CANONICAL_GEOMETRY_VTXFMT_COUNT ||
        canonical_geometry_read_le32(
            section_bytes +
                ACGC_GX_CANONICAL_GEOMETRY_HEADER_DESCRIPTOR_COUNT_OFFSET) !=
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT ||
        canonical_geometry_read_le32(
            section_bytes +
                ACGC_GX_CANONICAL_GEOMETRY_HEADER_DESCRIPTOR_OFFSET_OFFSET) !=
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_OFFSET ||
        canonical_geometry_read_le32(
            section_bytes +
                ACGC_GX_CANONICAL_GEOMETRY_HEADER_DESCRIPTOR_BYTES_OFFSET) !=
            ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_BYTES ||
        canonical_geometry_read_le32(
            section_bytes +
                ACGC_GX_CANONICAL_GEOMETRY_HEADER_STREAM_OFFSET_OFFSET) !=
            ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET ||
        canonical_geometry_read_le32(
            section_bytes +
                ACGC_GX_CANONICAL_GEOMETRY_HEADER_RESERVED0_OFFSET) != 0 ||
        canonical_geometry_read_le32(
            section_bytes +
                ACGC_GX_CANONICAL_GEOMETRY_HEADER_RESERVED1_OFFSET) != 0) {
        return 0;
    }
    *stream_bytes = canonical_geometry_read_le32(
        section_bytes + ACGC_GX_CANONICAL_GEOMETRY_HEADER_STREAM_BYTES_OFFSET);
    *primitive = canonical_geometry_read_le32(
        section_bytes + ACGC_GX_CANONICAL_GEOMETRY_HEADER_PRIMITIVE_OFFSET);
    *vertex_count = canonical_geometry_read_le32(
        section_bytes + ACGC_GX_CANONICAL_GEOMETRY_HEADER_VERTEX_COUNT_OFFSET);
    *present_mask = canonical_geometry_read_le32(
        section_bytes + ACGC_GX_CANONICAL_GEOMETRY_HEADER_PRESENT_MASK_OFFSET);
    *indexed_mask = canonical_geometry_read_le32(
        section_bytes + ACGC_GX_CANONICAL_GEOMETRY_HEADER_INDEXED_MASK_OFFSET);
    if ((*present_mask & ~ACGC_GX_CANONICAL_GEOMETRY_VALID_ATTRIBUTE_MASK) != 0 ||
        (*indexed_mask & ~ACGC_GX_CANONICAL_GEOMETRY_VALID_ATTRIBUTE_MASK) != 0 ||
        *stream_bytes > ACGC_GX_CANONICAL_GEOMETRY_MAX_STREAM_BYTES ||
        !canonical_geometry_u64_add(
            ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET,
            *stream_bytes,
            &stream_end) ||
        stream_end != (uint64_t)section_byte_size) {
        return 0;
    }
    return 1;
}

static int canonical_geometry_topology_is_valid(
    uint32_t primitive,
    uint32_t vertex_count
) {
    if (vertex_count > ACGC_GX_CANONICAL_GEOMETRY_MAX_VERTEX_COUNT) {
        return 0;
    }
    if (primitive == ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES) {
        return vertex_count >= 3 && (vertex_count % 3) == 0;
    }
    if (primitive == ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS) {
        return vertex_count >= 4 && (vertex_count % 4) == 0;
    }
    return 0;
}

static int canonical_geometry_descriptor_is_valid(
    const AcgcGxCanonicalGeometryDecodedDescriptor* descriptor,
    uint32_t slot,
    uint32_t vertex_count,
    uint64_t section_byte_size
) {
    uint32_t expected_word_count;
    uint64_t expected_value_bytes;
    uint64_t expected_index_bytes;
    uint32_t expected_index_stride;
    int indexed;

    if (descriptor->reserved[0] != 0 || descriptor->reserved[1] != 0) {
        return 0;
    }
    if (canonical_geometry_is_array_slot(slot)) {
        uint32_t word;

        for (word = 0; word < 16; word++) {
            if (canonical_geometry_descriptor_word(descriptor, word) != 0) {
                return 0;
            }
        }
        return 1;
    }
    if (descriptor->vcd_type > ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX16) {
        return 0;
    }
    if (descriptor->vcd_type == ACGC_GX_CANONICAL_GEOMETRY_VCD_NONE) {
        uint32_t word;

        for (word = 0; word < 16; word++) {
            if (canonical_geometry_descriptor_word(descriptor, word) != 0) {
                return 0;
            }
        }
        return 1;
    }
    if (canonical_geometry_is_matrix_slot(slot) &&
        descriptor->vcd_type != ACGC_GX_CANONICAL_GEOMETRY_VCD_DIRECT) {
        return 0;
    }
    if (canonical_geometry_is_normal_slot(slot) &&
        descriptor->vat_count == ACGC_GX_CANONICAL_GEOMETRY_NRM_NBT &&
        canonical_geometry_is_indexed(descriptor->vcd_type)) {
        return 0;
    }
    if (slot == ACGC_GX_CANONICAL_GEOMETRY_ATTR_NBT &&
        descriptor->vat_count != ACGC_GX_CANONICAL_GEOMETRY_NRM_NBT) {
        return 0;
    }
    if (!canonical_geometry_descriptor_format_is_valid(slot, descriptor)) {
        return 0;
    }
    expected_word_count = canonical_geometry_expected_word_count(
        slot, descriptor);
    if (expected_word_count == 0 ||
        descriptor->value_encoding != 1 ||
        descriptor->canonical_word_count != expected_word_count ||
        descriptor->value_stride != expected_word_count * 4) {
        return 0;
    }
    indexed = canonical_geometry_is_indexed(descriptor->vcd_type);
    if (indexed) {
        if (descriptor->index_count != vertex_count ||
            descriptor->value_count == 0 ||
            descriptor->value_count > vertex_count) {
            return 0;
        }
        expected_index_stride =
            descriptor->vcd_type == ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX8
                ? 1
                : 2;
        if (descriptor->index_stride != expected_index_stride) {
            return 0;
        }
    } else {
        if (descriptor->value_count != vertex_count ||
            descriptor->index_offset != 0 ||
            descriptor->index_bytes != 0 ||
            descriptor->index_stride != 0 ||
            descriptor->index_count != 0) {
            return 0;
        }
        expected_index_stride = 0;
    }

    if (!canonical_geometry_u64_mul(
            descriptor->value_count,
            descriptor->value_stride,
            &expected_value_bytes) ||
        !canonical_geometry_u64_mul(
            descriptor->index_count,
            expected_index_stride,
            &expected_index_bytes) ||
        expected_value_bytes != descriptor->value_bytes ||
        expected_index_bytes != descriptor->index_bytes ||
        descriptor->value_offset < ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET ||
        (descriptor->value_offset % ACGC_GX_CANONICAL_GEOMETRY_ALIGNMENT) != 0 ||
        !canonical_geometry_range_is_valid(
            descriptor->value_offset,
            descriptor->value_bytes,
            section_byte_size)) {
        return 0;
    }
    if (indexed &&
        (descriptor->index_offset <
            ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET ||
         (descriptor->index_offset % ACGC_GX_CANONICAL_GEOMETRY_ALIGNMENT) != 0 ||
         !canonical_geometry_range_is_valid(
             descriptor->index_offset,
             descriptor->index_bytes,
             section_byte_size))) {
        return 0;
    }
    return 1;
}

static int canonical_geometry_all_regions_are_canonical(
    const uint8_t* section_bytes,
    size_t section_byte_size,
    uint32_t stream_bytes,
    uint32_t present_mask,
    uint32_t indexed_mask,
    const AcgcGxCanonicalGeometryDecodedDescriptor* descriptors
) {
    const uint64_t section_extent = (uint64_t)section_byte_size;
    const uint64_t stream_start =
        ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET;
    uint64_t stream_end;
    uint64_t cursor;
    uint32_t slot;

    if (!canonical_geometry_u64_add(
            stream_start, stream_bytes, &stream_end) ||
        stream_end != section_extent) {
        return 0;
    }

    cursor = stream_start;
    for (slot = 0; slot < ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT; slot++) {
        const AcgcGxCanonicalGeometryDecodedDescriptor* descriptor =
            &descriptors[slot];
        uint64_t aligned_cursor;
        uint64_t value_end;
        uint64_t index_end;

        if ((present_mask & (UINT32_C(1) << slot)) == 0) {
            continue;
        }
        if (!canonical_geometry_u64_align4(cursor, &aligned_cursor) ||
            aligned_cursor > stream_end ||
            !canonical_geometry_bytes_are_zero(
                section_bytes, cursor, aligned_cursor) ||
            (uint64_t)descriptor->value_offset != aligned_cursor ||
            !canonical_geometry_u64_add(
                aligned_cursor,
                descriptor->value_bytes,
                &value_end)) {
            return 0;
        }
        cursor = value_end;
        if ((indexed_mask & (UINT32_C(1) << slot)) != 0) {
            if (!canonical_geometry_u64_align4(cursor, &aligned_cursor) ||
                aligned_cursor > stream_end ||
                !canonical_geometry_bytes_are_zero(
                    section_bytes, cursor, aligned_cursor) ||
                (uint64_t)descriptor->index_offset != aligned_cursor ||
                !canonical_geometry_u64_add(
                    aligned_cursor,
                    descriptor->index_bytes,
                    &index_end)) {
                return 0;
            }
            cursor = index_end;
        }
        {
            uint32_t record;

            for (record = 0; record < descriptor->value_count; record++) {
                if (!canonical_geometry_value_record_is_valid(
                        section_bytes,
                        slot,
                        descriptor,
                        record)) {
                    return 0;
                }
            }
            if ((indexed_mask & (UINT32_C(1) << slot)) != 0 &&
                !canonical_geometry_index_stream_is_valid(
                    section_bytes, descriptor)) {
                return 0;
            }
        }
    }
    /* The complete stream extent may include zero final padding. */
    return cursor <= stream_end &&
        canonical_geometry_bytes_are_zero(section_bytes, cursor, stream_end);
}

int acgc_gx_canonical_geometry_state_validate(
    const uint8_t* section_bytes,
    size_t section_byte_size
) {
    AcgcGxCanonicalGeometryDecodedDescriptor descriptors[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT];
    uint32_t stream_bytes;
    uint32_t primitive;
    uint32_t vertex_count;
    uint32_t present_mask;
    uint32_t indexed_mask;
    uint32_t computed_present_mask = 0;
    uint32_t computed_indexed_mask = 0;
    uint32_t slot;

    if (!canonical_geometry_read_header(
            section_bytes,
            section_byte_size,
            &stream_bytes,
            &primitive,
            &vertex_count,
            &present_mask,
            &indexed_mask) ||
        !canonical_geometry_topology_is_valid(primitive, vertex_count)) {
        return 0;
    }

    for (slot = 0; slot < ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT; slot++) {
        canonical_geometry_read_descriptor(section_bytes, slot, &descriptors[slot]);
        if (!canonical_geometry_descriptor_is_valid(
                &descriptors[slot],
                slot,
                vertex_count,
                (uint64_t)section_byte_size)) {
            return 0;
        }
        if (descriptors[slot].vcd_type !=
                ACGC_GX_CANONICAL_GEOMETRY_VCD_NONE) {
            computed_present_mask |= UINT32_C(1) << slot;
        }
        if (canonical_geometry_is_indexed(descriptors[slot].vcd_type)) {
            computed_indexed_mask |= UINT32_C(1) << slot;
        }
    }

    if ((present_mask & (UINT32_C(1) <<
            ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS)) == 0 ||
        ((present_mask & (UINT32_C(1) <<
            ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM)) != 0 &&
         (present_mask & (UINT32_C(1) <<
            ACGC_GX_CANONICAL_GEOMETRY_ATTR_NBT)) != 0) ||
        present_mask != computed_present_mask ||
        indexed_mask != computed_indexed_mask) {
        return 0;
    }

    return canonical_geometry_all_regions_are_canonical(
        section_bytes,
        section_byte_size,
        stream_bytes,
        present_mask,
        indexed_mask,
        descriptors
    );
}

int acgc_gx_canonical_geometry_validate(
    const uint8_t* section_bytes,
    size_t section_byte_size
) {
    return acgc_gx_canonical_geometry_state_validate(
        section_bytes, section_byte_size);
}

static int canonical_geometry_dependency_flag_is_valid(uint32_t value) {
    return value == 0 || value == 1;
}

static int canonical_geometry_dependency_context_is_valid(
    const AcgcGxCanonicalGeometryDependencyResults* dependencies
) {
    uint32_t index;

    if (dependencies == NULL ||
        !canonical_geometry_dependency_flag_is_valid(
            dependencies->transform_valid) ||
        !canonical_geometry_dependency_flag_is_valid(
            dependencies->texgens_valid) ||
        !canonical_geometry_dependency_flag_is_valid(
            dependencies->channels_valid) ||
        !canonical_geometry_dependency_flag_is_valid(
            dependencies->lighting_valid) ||
        !canonical_geometry_dependency_flag_is_valid(
            dependencies->bump_valid) ||
        dependencies->reserved0 != 0 ||
        (dependencies->required_geometry_present_mask &
            ~ACGC_GX_CANONICAL_GEOMETRY_VALID_ATTRIBUTE_MASK) != 0 ||
        dependencies->required_channel_mask > 3 ||
        (dependencies->required_lighting_mask & ~UINT32_C(0xFF)) != 0 ||
        (dependencies->required_bump_mask & ~UINT32_C(0xFF)) != 0 ||
        (dependencies->transform_position_known_mask & ~UINT32_C(0x3FF)) != 0 ||
        (dependencies->transform_normal_known_mask & ~UINT32_C(0x3FF)) != 0 ||
        !canonical_geometry_dependency_flag_is_valid(
            dependencies->transform_current_position_known) ||
        (dependencies->texgen_present_mask & ~UINT32_C(0xFF)) != 0 ||
        (dependencies->texgen_ordinary_known_mask & ~UINT32_C(0x7FF)) != 0 ||
        (dependencies->texgen_post_known_mask & ~UINT32_C(0x1FFFFF)) != 0 ||
        (dependencies->lighting_loaded_mask & ~UINT32_C(0xFF)) != 0 ||
        (dependencies->bump_known_mask & ~UINT32_C(0xFF)) != 0) {
        return 0;
    }
    for (index = 0; index < 8; index++) {
        if ((dependencies->texgen_present_mask & (UINT32_C(1) << index)) == 0 &&
            dependencies->texgen_selector[index] != 0) {
            return 0;
        }
    }
    for (index = 0; index < 4; index++) {
        if (dependencies->reserved[index] != 0) {
            return 0;
        }
    }
    if (dependencies->transform_current_position_known == 0 &&
        dependencies->transform_current_position_id != 0) {
        return 0;
    }
    if (dependencies->transform_current_position_known != 0 &&
        !canonical_geometry_position_id_is_exact(
            dependencies->transform_current_position_id)) {
        return 0;
    }
    return 1;
}

static int canonical_geometry_position_record_is_known(
    uint32_t id,
    uint32_t known_mask
) {
    uint32_t slot;

    for (slot = 0;
         slot < ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_COUNT;
         slot++) {
        if (id == slot *
                ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_STRIDE) {
            return (known_mask & (UINT32_C(1) << slot)) != 0;
        }
    }
    return 0;
}

static int canonical_geometry_ordinary_texture_record_is_known(
    uint32_t id,
    const AcgcGxCanonicalGeometryDependencyResults* dependencies
) {
    uint32_t record;

    if (!canonical_geometry_ordinary_texture_id_to_record(id, &record)) {
        return 0;
    }
    return (dependencies->texgen_ordinary_known_mask &
        (UINT32_C(1) << record)) != 0;
}

static int canonical_geometry_texgen_selector_is_valid(
    uint32_t selector,
    const AcgcGxCanonicalGeometryDependencyResults* dependencies
) {
    /* Zero is not an implicit identity; selector 60 is ordinary record 10. */
    return canonical_geometry_ordinary_texture_record_is_known(
        selector, dependencies);
}

static int canonical_geometry_dependency_values_are_valid(
    const uint8_t* section_bytes,
    uint32_t present_mask,
    const AcgcGxCanonicalGeometryDecodedDescriptor* descriptors,
    const AcgcGxCanonicalGeometryDependencyResults* dependencies
) {
    const uint32_t pn_mask = UINT32_C(1) <<
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_PNMTXIDX;
    uint32_t coord;
    int normal_present =
        (present_mask & (UINT32_C(1) <<
            ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM)) != 0 ||
        (present_mask & (UINT32_C(1) <<
            ACGC_GX_CANONICAL_GEOMETRY_ATTR_NBT)) != 0;

    if (dependencies->transform_valid == 0 ||
        (present_mask & dependencies->required_geometry_present_mask) !=
            dependencies->required_geometry_present_mask) {
        return 0;
    }
    if (dependencies->required_channel_mask != 0 &&
        dependencies->channels_valid == 0) {
        return 0;
    }
    if (dependencies->required_lighting_mask != 0 &&
        (dependencies->lighting_valid == 0 ||
         (dependencies->lighting_loaded_mask &
            dependencies->required_lighting_mask) !=
             dependencies->required_lighting_mask)) {
        return 0;
    }
    if (dependencies->required_bump_mask != 0 &&
        (dependencies->bump_valid == 0 ||
         (dependencies->bump_known_mask & dependencies->required_bump_mask) !=
             dependencies->required_bump_mask)) {
        return 0;
    }

    if ((present_mask & pn_mask) == 0) {
        if (dependencies->transform_current_position_known == 0 ||
            !canonical_geometry_position_record_is_known(
                dependencies->transform_current_position_id,
                dependencies->transform_position_known_mask) ||
            (normal_present &&
             !canonical_geometry_position_record_is_known(
                 dependencies->transform_current_position_id,
                 dependencies->transform_normal_known_mask))) {
            return 0;
        }
    } else {
        const AcgcGxCanonicalGeometryDecodedDescriptor* descriptor =
            &descriptors[ACGC_GX_CANONICAL_GEOMETRY_ATTR_PNMTXIDX];
        uint32_t record;

        for (record = 0; record < descriptor->value_count; record++) {
            const uint64_t offset = (uint64_t)descriptor->value_offset +
                (uint64_t)record * descriptor->value_stride;
            const uint32_t selector = canonical_geometry_read_le32(
                section_bytes + (size_t)offset);
            if (!canonical_geometry_position_record_is_known(
                    selector, dependencies->transform_position_known_mask) ||
                (normal_present &&
                 !canonical_geometry_position_record_is_known(
                     selector, dependencies->transform_normal_known_mask))) {
                return 0;
            }
        }
    }

    for (coord = 0; coord < 8; coord++) {
        const uint32_t tex_matrix_slot =
            ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0MTXIDX + coord;
        const uint32_t texcoord_slot =
            ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0 + coord;
        const uint32_t tex_matrix_mask = UINT32_C(1) << tex_matrix_slot;
        const uint32_t texcoord_mask = UINT32_C(1) << texcoord_slot;
        const int needs_texgen = (present_mask &
            (tex_matrix_mask | texcoord_mask)) != 0;

        if (!needs_texgen) {
            continue;
        }
        if (dependencies->texgens_valid == 0 ||
            (dependencies->texgen_present_mask & (UINT32_C(1) << coord)) == 0 ||
            !canonical_geometry_texgen_selector_is_valid(
                dependencies->texgen_selector[coord], dependencies)) {
            return 0;
        }
        if ((present_mask & tex_matrix_mask) != 0) {
            const AcgcGxCanonicalGeometryDecodedDescriptor* descriptor =
                &descriptors[tex_matrix_slot];
            uint32_t record;

            for (record = 0; record < descriptor->value_count; record++) {
                const uint64_t offset = (uint64_t)descriptor->value_offset +
                    (uint64_t)record * descriptor->value_stride;
                const uint32_t selector = canonical_geometry_read_le32(
                    section_bytes + (size_t)offset);
                uint32_t ordinary_record;

                if (!canonical_geometry_ordinary_texture_id_to_record(
                        selector, &ordinary_record) ||
                    (dependencies->texgen_ordinary_known_mask &
                        (UINT32_C(1) << ordinary_record)) == 0) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

int acgc_gx_canonical_geometry_state_validate_dependencies(
    const uint8_t* section_bytes,
    size_t section_byte_size,
    const AcgcGxCanonicalGeometryDependencyResults* dependencies
) {
    AcgcGxCanonicalGeometryDecodedDescriptor descriptors[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT];
    uint32_t stream_bytes;
    uint32_t primitive;
    uint32_t vertex_count;
    uint32_t present_mask;
    uint32_t indexed_mask;
    uint32_t slot;

    if (!canonical_geometry_dependency_context_is_valid(dependencies) ||
        !acgc_gx_canonical_geometry_state_validate(
            section_bytes, section_byte_size) ||
        !canonical_geometry_read_header(
            section_bytes,
            section_byte_size,
            &stream_bytes,
            &primitive,
            &vertex_count,
            &present_mask,
            &indexed_mask)) {
        return 0;
    }
    (void)stream_bytes;
    (void)primitive;
    (void)vertex_count;
    (void)indexed_mask;
    for (slot = 0; slot < ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT; slot++) {
        canonical_geometry_read_descriptor(section_bytes, slot, &descriptors[slot]);
    }
    return canonical_geometry_dependency_values_are_valid(
        section_bytes,
        present_mask,
        descriptors,
        dependencies
    );
}

static int canonical_geometry_entry_is_absent(
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry
) {
    return entry != NULL &&
        entry->section_id == ACGC_GX_CANONICAL_GEOMETRY_SECTION_ID &&
        entry->section_version == 0 &&
        entry->byte_offset == 0 &&
        entry->byte_size == 0 &&
        entry->count == 0 &&
        entry->capacity == 0 &&
        entry->valid_mask == 0 &&
        entry->reserved == 0;
}

int acgc_gx_canonical_geometry_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
) {
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    if (!acgc_gx_canonical_envelope_validate(
            envelope, envelope_byte_size)) {
        return 0;
    }
    entry = &envelope->directory[
        ACGC_GX_CANONICAL_GEOMETRY_SECTION_ID - 1];
    if ((envelope->header.present_state_mask &
            ACGC_GX_CANONICAL_GEOMETRY_SECTION_MASK) == 0) {
        return canonical_geometry_entry_is_absent(entry);
    }
    return entry->section_id == ACGC_GX_CANONICAL_GEOMETRY_SECTION_ID &&
        entry->section_version ==
            ACGC_GX_CANONICAL_GEOMETRY_STATE_VERSION &&
        entry->byte_size >= ACGC_GX_CANONICAL_GEOMETRY_MIN_SECTION_SIZE &&
        entry->byte_size <= ACGC_GX_CANONICAL_GEOMETRY_MAX_SECTION_SIZE &&
        entry->count == ACGC_GX_CANONICAL_GEOMETRY_STATE_COUNT &&
        entry->capacity == ACGC_GX_CANONICAL_GEOMETRY_STATE_CAPACITY &&
        entry->valid_mask == ACGC_GX_CANONICAL_GEOMETRY_SECTION_MASK &&
        entry->reserved == 0;
}
