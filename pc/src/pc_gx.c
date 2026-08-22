/* pc_gx.c - GX API → OpenGL 3.3: state management, vertex submission, draw dispatch */
#include "pc_gx_internal.h"
#include "pc_gx_texture_raw_state.h"
#include "pc_profiler.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
static GLushort quad_index_buf[(PC_GX_MAX_VERTS / 4) * 6];
#include <math.h>
#include <dolphin/gx/GXEnum.h>

/* Can't include GXTev.h — it uses enum types while we use u32 */
void GXSetTevColorIn(u32 stage, u32 a, u32 b, u32 c, u32 d);
void GXSetTevAlphaIn(u32 stage, u32 a, u32 b, u32 c, u32 d);
void GXSetTevColorOp(u32 stage, u32 op, u32 bias, u32 scale, GXBool clamp, u32 out_reg);
void GXSetTevAlphaOp(u32 stage, u32 op, u32 bias, u32 scale, GXBool clamp, u32 out_reg);

typedef struct { u8 r, g, b, a; } GXColor;

#undef glUniform1i
#undef glUniform2i
#undef glUniform3i
#undef glUniform4i
#undef glUniform1f
#undef glUniform2f
#undef glUniform3f
#undef glUniform4f
#undef glUniform1iv
#undef glUniform2iv
#undef glUniform3iv
#undef glUniform4iv
#undef glUniform4fv
#undef glUniform3fv
#undef glUniformMatrix3fv
#undef glUniformMatrix4fv

#define glUniform1i(...)        (pc_profiler_add_count_uniform(), glad_glUniform1i(__VA_ARGS__))
#define glUniform2i(...)        (pc_profiler_add_count_uniform(), glad_glUniform2i(__VA_ARGS__))
#define glUniform3i(...)        (pc_profiler_add_count_uniform(), glad_glUniform3i(__VA_ARGS__))
#define glUniform4i(...)        (pc_profiler_add_count_uniform(), glad_glUniform4i(__VA_ARGS__))
#define glUniform1f(...)        (pc_profiler_add_count_uniform(), glad_glUniform1f(__VA_ARGS__))
#define glUniform2f(...)        (pc_profiler_add_count_uniform(), glad_glUniform2f(__VA_ARGS__))
#define glUniform3f(...)        (pc_profiler_add_count_uniform(), glad_glUniform3f(__VA_ARGS__))
#define glUniform4f(...)        (pc_profiler_add_count_uniform(), glad_glUniform4f(__VA_ARGS__))
#define glUniform1iv(...)       (pc_profiler_add_count_uniform(), glad_glUniform1iv(__VA_ARGS__))
#define glUniform2iv(...)       (pc_profiler_add_count_uniform(), glad_glUniform2iv(__VA_ARGS__))
#define glUniform3iv(...)       (pc_profiler_add_count_uniform(), glad_glUniform3iv(__VA_ARGS__))
#define glUniform4iv(...)       (pc_profiler_add_count_uniform(), glad_glUniform4iv(__VA_ARGS__))
#define glUniform4fv(...)       (pc_profiler_add_count_uniform(), glad_glUniform4fv(__VA_ARGS__))
#define glUniform3fv(...)       (pc_profiler_add_count_uniform(), glad_glUniform3fv(__VA_ARGS__))
#define glUniformMatrix3fv(...) (pc_profiler_add_count_uniform(), glad_glUniformMatrix3fv(__VA_ARGS__))
#define glUniformMatrix4fv(...) (pc_profiler_add_count_uniform(), glad_glUniformMatrix4fv(__VA_ARGS__))

/* --- Global GX State --- */
PCGXState g_gx;

static PCGXSemanticPacketHandoffCallback s_semantic_packet_handoff;
static void* s_semantic_packet_handoff_context;

#ifdef PC_GX_ALPHA_RAW_SHADOW_FIXTURE
static PCGXAlphaFlushFixtureObserver s_alpha_flush_fixture_observer;
static void* s_alpha_flush_fixture_observer_context;
#endif

#ifdef PC_GX_RASTER_RAW_SHADOW_FIXTURE
static PCGXRasterFlushFixtureObserver s_raster_flush_fixture_observer;
static void* s_raster_flush_fixture_observer_context;
#endif

#ifdef PC_GX_DEPTH_RAW_SHADOW_FIXTURE
static PCGXDepthFlushFixtureObserver s_depth_flush_fixture_observer;
static void* s_depth_flush_fixture_observer_context;
#endif

#ifdef PC_GX_TEXGEN_RAW_SHADOW_FIXTURE
static PCGXTexgenFlushFixtureObserver s_texgen_flush_fixture_observer;
static void* s_texgen_flush_fixture_observer_context;
#endif

#ifdef PC_GX_GEOMETRY_RAW_BATCH_FIXTURE
static PCGXGeometryFlushFixtureObserver s_geometry_flush_fixture_observer;
static void* s_geometry_flush_fixture_observer_context;
#endif

#define PCGX_GEOMETRY_DESCRIPTOR_VCD_KNOWN UINT32_C(1)
#define PCGX_GEOMETRY_DESCRIPTOR_VAT_KNOWN UINT32_C(2)

static int pc_gx_raw_geometry_is_matrix_attr(int attr) {
    return attr >= GX_VA_PNMTXIDX && attr <= GX_VA_TEX7MTXIDX;
}

static int pc_gx_raw_geometry_is_array_attr(int attr) {
    return attr >= GX_POS_MTX_ARRAY && attr <= GX_LIGHT_ARRAY;
}

static int pc_gx_raw_geometry_is_supported_attr(int attr) {
    return attr == GX_VA_POS || attr == GX_VA_NRM ||
        attr == GX_VA_CLR0 || attr == GX_VA_TEX0;
}

static int pc_gx_raw_geometry_is_scalar_attr(int attr) {
    return attr == GX_VA_POS || attr == GX_VA_NRM || attr == GX_VA_TEX0;
}

static int pc_gx_raw_geometry_is_color_attr(int attr) {
    return attr == GX_VA_CLR0;
}

static int pc_gx_raw_geometry_array_attr(int attr) {
    /* Raw capture keeps the public slot.  NBT and matrix-array slots are
     * deliberately unsupported until their source semantics are owned. */
    return attr;
}

static uint32_t pc_gx_raw_geometry_expected_word_count(
    int attr,
    uint32_t count
) {
    (void)count;
    if (attr == GX_VA_POS) return 3;
    if (attr == GX_VA_NRM) return 3;
    if (pc_gx_raw_geometry_is_color_attr(attr)) return 1;
    if (attr == GX_VA_TEX0) return 2;
    return 0;
}

static uint32_t pc_gx_raw_geometry_expected_components(
    int attr,
    uint32_t count
) {
    if (attr == GX_VA_POS) return count == GX_POS_XY ? 2 : 3;
    if (attr == GX_VA_NRM) return 3;
    if (attr == GX_VA_TEX0)
        return count == GX_TEX_S ? 1 : 2;
    return 1;
}

static int pc_gx_raw_geometry_scalar_type_is_valid(uint32_t type) {
    return type <= GX_F32;
}

static uint32_t pc_gx_raw_geometry_scalar_bytes(uint32_t type) {
    if (type == GX_U8 || type == GX_S8) return 1;
    if (type == GX_U16 || type == GX_S16) return 2;
    if (type == GX_F32) return 4;
    return 0;
}

static uint32_t pc_gx_raw_geometry_array_value_bytes(
    int attr,
    const PCGXRawGeometryAttribute* descriptor
) {
    uint32_t component_count;
    uint32_t scalar_bytes;

    if (attr == GX_VA_CLR0) {
        if (descriptor->vat_type == GX_RGB565 ||
            descriptor->vat_type == GX_RGBA4) return 2;
        if (descriptor->vat_type == GX_RGB8 ||
            descriptor->vat_type == GX_RGBA6) return 3;
        if (descriptor->vat_type == GX_RGBX8 ||
            descriptor->vat_type == GX_RGBA8) return 4;
        return 0;
    }
    if (!pc_gx_raw_geometry_is_scalar_attr(attr)) return 0;
    component_count = pc_gx_raw_geometry_expected_components(
        attr, descriptor->vat_count);
    scalar_bytes = pc_gx_raw_geometry_scalar_bytes(descriptor->vat_type);
    if (component_count == 0 || scalar_bytes == 0 ||
        component_count > UINT32_MAX / scalar_bytes) return 0;
    return component_count * scalar_bytes;
}

static int pc_gx_raw_geometry_format_is_valid(
    int attr,
    const PCGXRawGeometryAttribute* descriptor
) {
    uint32_t type = descriptor->vat_type;
    uint32_t count = descriptor->vat_count;

    if (!pc_gx_raw_geometry_is_supported_attr(attr)) return 0;
    if (attr == GX_VA_POS) {
        return (count == GX_POS_XY || count == GX_POS_XYZ) &&
            pc_gx_raw_geometry_scalar_type_is_valid(type) &&
            (type == GX_F32 ? descriptor->vat_fraction == 0 :
             descriptor->vat_fraction <= 31);
    }
    if (attr == GX_VA_NRM) {
        /* This lane has no NBT/NBT3 setter-owned payload source. */
        return (count == GX_NRM_XYZ) &&
            (type == GX_S8 || type == GX_S16 || type == GX_F32) &&
            descriptor->vat_fraction == 0;
    }
    if (pc_gx_raw_geometry_is_color_attr(attr)) {
        if (descriptor->vat_fraction != 0) return 0;
        if (count == GX_CLR_RGB) {
            return type == GX_RGB565 || type == GX_RGB8 || type == GX_RGBX8;
        }
        if (count == GX_CLR_RGBA) {
            return type == GX_RGBA4 || type == GX_RGBA6 || type == GX_RGBA8;
        }
        return 0;
    }
    if (attr == GX_VA_TEX0) {
        return (count == GX_TEX_S || count == GX_TEX_ST) &&
            pc_gx_raw_geometry_scalar_type_is_valid(type) &&
            (type == GX_F32 ? descriptor->vat_fraction == 0 :
             descriptor->vat_fraction <= 31);
    }
    return 0;
}

static void pc_gx_raw_geometry_mark_invalid(void) {
    g_gx.raw_geometry.invalid = 1;
    g_gx.raw_geometry.current.invalid = 1;
    if (g_gx.raw_geometry.live.active != 0)
        g_gx.raw_geometry.live.invalid = 1;
}

static void pc_gx_raw_geometry_note_mid_begin_mutation(void) {
    /* Preserve the PC host setter behavior, but make the in-flight raw batch
     * fail closed just as the decomp GX setters reject the mutation. */
    if (g_gx.in_begin) pc_gx_raw_geometry_mark_invalid();
}

static void pc_gx_raw_geometry_clear_current(void) {
    memset(&g_gx.raw_geometry.current, 0,
           sizeof(g_gx.raw_geometry.current));
}

static int pc_gx_raw_geometry_words_are_valid(
    int attr,
    const PCGXRawGeometryAttribute* descriptor,
    const uint32_t* words,
    uint32_t word_count
) {
    uint32_t word;

    if (words == NULL || word_count != descriptor->value_word_count)
        return 0;
    if (descriptor->vat_type == GX_F32) {
        for (word = 0; word < word_count; word++) {
            float value;
            memcpy(&value, &words[word], sizeof(value));
            if (!isfinite(value)) return 0;
        }
    }
    if (attr == GX_VA_CLR0) {
        switch (descriptor->vat_type) {
        case GX_RGB565:
        case GX_RGBA4:
            return words[0] <= UINT32_C(0x0000FFFF);
        case GX_RGB8:
        case GX_RGBA6:
            return words[0] <= UINT32_C(0x00FFFFFF);
        case GX_RGBX8:
            /* The fourth byte is the GX RGBX8 X byte, not a color value. */
            return 1;
        case GX_RGBA8:
            return 1;
        default:
            return 0;
        }
    }
    return 1;
}

static void pc_gx_raw_geometry_set_vcd(int attr, uint32_t type) {
    int format;

    if (attr < 0 || attr >= GX_VA_MAX_ATTR || type > GX_INDEX16) {
        pc_gx_raw_geometry_mark_invalid();
        return;
    }
    for (format = 0; format < PC_GX_MAX_VTXFMT; format++) {
        PCGXRawGeometryFormat* entry =
            &g_gx.raw_geometry.format[format][attr];

        entry->vcd_type = type;
        entry->vcd_known = 1;
        if (attr == GX_VA_NRM && type != GX_NONE) {
            g_gx.raw_geometry.format[format][GX_VA_NBT].vcd_type = GX_NONE;
            g_gx.raw_geometry.format[format][GX_VA_NBT].vcd_known = 1;
        } else if (attr == GX_VA_NBT && type != GX_NONE) {
            g_gx.raw_geometry.format[format][GX_VA_NRM].vcd_type = GX_NONE;
            g_gx.raw_geometry.format[format][GX_VA_NRM].vcd_known = 1;
        }
    }
}

static void pc_gx_raw_geometry_clear_vcd(void) {
    int format;
    int attr;

    for (format = 0; format < PC_GX_MAX_VTXFMT; format++) {
        for (attr = 0; attr < PC_GX_MAX_ATTR; attr++) {
            g_gx.raw_geometry.format[format][attr].vcd_type = GX_NONE;
            g_gx.raw_geometry.format[format][attr].vcd_known = 1;
        }
        /* GXClearVtxDesc leaves the position field as DIRECT on GX. */
        g_gx.raw_geometry.format[format][GX_VA_POS].vcd_type = GX_DIRECT;
    }
}

static void pc_gx_raw_geometry_set_vat(
    uint32_t vtxfmt,
    int attr,
    uint32_t count,
    uint32_t type,
    uint32_t fraction
) {
    PCGXRawGeometryFormat* entry;

    if (vtxfmt >= PC_GX_MAX_VTXFMT || attr < GX_VA_POS ||
        attr >= GX_VA_MAX_ATTR || count > GX_NRM_NBT3 ||
        type > GX_RGBA8 || fraction > 31) {
        pc_gx_raw_geometry_mark_invalid();
        return;
    }
    entry = &g_gx.raw_geometry.format[vtxfmt][attr];
    entry->vat_count = count;
    entry->vat_type = type;
    entry->vat_fraction = fraction;
    entry->vat_known = 1;
    if (attr == GX_VA_NRM || attr == GX_VA_NBT) {
        g_gx.raw_geometry.format[vtxfmt][GX_VA_NRM].vat_count = count;
        g_gx.raw_geometry.format[vtxfmt][GX_VA_NRM].vat_type = type;
        g_gx.raw_geometry.format[vtxfmt][GX_VA_NRM].vat_fraction = fraction;
        g_gx.raw_geometry.format[vtxfmt][GX_VA_NRM].vat_known = 1;
        g_gx.raw_geometry.format[vtxfmt][GX_VA_NBT].vat_count = count;
        g_gx.raw_geometry.format[vtxfmt][GX_VA_NBT].vat_type = type;
        g_gx.raw_geometry.format[vtxfmt][GX_VA_NBT].vat_fraction = fraction;
        g_gx.raw_geometry.format[vtxfmt][GX_VA_NBT].vat_known = 1;
    }
}

static void pc_gx_raw_geometry_set_array(
    int attr,
    const void* data,
    uint32_t size,
    uint32_t stride
) {
    PCGXRawGeometryArray* entry;
    uint64_t generation;

    attr = pc_gx_raw_geometry_array_attr(attr);
    if (attr < GX_VA_POS || attr > GX_LIGHT_ARRAY) {
        pc_gx_raw_geometry_mark_invalid();
        return;
    }
    generation = ++g_gx.raw_geometry.next_array_generation;
    if (generation == 0) generation = ++g_gx.raw_geometry.next_array_generation;
    entry = &g_gx.raw_geometry.array[attr];
    entry->generation = generation;
    entry->byte_size = size;
    entry->stride = stride;
    entry->known = 1;
    entry->data_known = data != NULL;
}

static void pc_gx_raw_geometry_copy_descriptor(
    PCGXRawGeometryAttribute* destination,
    const PCGXRawGeometryFormat* source
) {
    destination->vcd_type = source->vcd_type;
    destination->vat_count = source->vat_count;
    destination->vat_type = source->vat_type;
    destination->vat_fraction = source->vat_fraction;
    destination->descriptor_known =
        (source->vcd_known ? PCGX_GEOMETRY_DESCRIPTOR_VCD_KNOWN : 0) |
        (source->vat_known ? PCGX_GEOMETRY_DESCRIPTOR_VAT_KNOWN : 0);
}

static void pc_gx_raw_geometry_begin_batch(
    uint32_t primitive,
    uint32_t vtxfmt,
    uint32_t expected_vertex_count
) {
    PCGXRawGeometryBatch* batch = &g_gx.raw_geometry.live;
    int attr;

    memset(batch, 0, sizeof(*batch));
    pc_gx_raw_geometry_clear_current();
    batch->active = 1;
    batch->primitive = primitive == GX_TRIANGLES ? 1 :
        primitive == GX_QUADS ? 2 : 0;
    batch->vtxfmt = vtxfmt;
    batch->expected_vertex_count = expected_vertex_count;
    batch->invalid = g_gx.raw_geometry.invalid;

    if ((primitive != GX_TRIANGLES && primitive != GX_QUADS) ||
        vtxfmt >= PC_GX_MAX_VTXFMT || expected_vertex_count == 0 ||
        expected_vertex_count > PC_GX_GEOMETRY_MAX_VERTICES ||
        (primitive == GX_TRIANGLES && expected_vertex_count % 3 != 0) ||
        (primitive == GX_QUADS && expected_vertex_count % 4 != 0)) {
        batch->invalid = 1;
    }

    for (attr = 0; attr < PC_GX_MAX_ATTR; attr++) {
        PCGXRawGeometryAttribute* descriptor = &batch->attr[attr];
        const PCGXRawGeometryFormat* format;
        int array_attr = pc_gx_raw_geometry_array_attr(attr);

        if (vtxfmt >= PC_GX_MAX_VTXFMT) continue;
        format = &g_gx.raw_geometry.format[vtxfmt][attr];
        pc_gx_raw_geometry_copy_descriptor(descriptor, format);
        if (array_attr >= 0 && array_attr < PC_GX_MAX_ATTR) {
            const PCGXRawGeometryArray* array =
                &g_gx.raw_geometry.array[array_attr];
            descriptor->array_known =
                (array->known ? PC_GX_GEOMETRY_ARRAY_KNOWN : 0) |
                (array->data_known ? PC_GX_GEOMETRY_ARRAY_DATA_KNOWN : 0);
            descriptor->array_generation = array->generation;
            descriptor->array_byte_size = array->byte_size;
            descriptor->array_stride = array->stride;
        }

        if (format->vcd_known == 0 || format->vcd_type > GX_INDEX16) {
            batch->invalid = 1;
            continue;
        }
        if (format->vcd_type == GX_NONE) continue;
        if (pc_gx_raw_geometry_is_matrix_attr(attr) ||
            pc_gx_raw_geometry_is_array_attr(attr) ||
            !pc_gx_raw_geometry_is_supported_attr(attr)) {
            /* Matrix selectors/arrays, CLR1, TEX1-TEX7, and NBT are not
             * source-owned by this raw batch.  Do not infer their payload. */
            batch->invalid = 1;
            continue;
        }
        if (!pc_gx_raw_geometry_format_is_valid(attr, descriptor) ||
            format->vat_known == 0) {
            batch->invalid = 1;
            continue;
        }
        descriptor->value_word_count =
            pc_gx_raw_geometry_expected_word_count(attr, format->vat_count);
        if (format->vcd_type == GX_INDEX8) {
            descriptor->index_stride = 1;
        } else if (format->vcd_type == GX_INDEX16) {
            descriptor->index_stride = 2;
        }
        if (format->vcd_type != GX_DIRECT &&
            (descriptor->array_known !=
                 (PC_GX_GEOMETRY_ARRAY_KNOWN |
                  PC_GX_GEOMETRY_ARRAY_DATA_KNOWN) ||
             g_gx.array_base[array_attr] == NULL)) {
            batch->invalid = 1;
        }
        if (format->vcd_type != GX_DIRECT &&
            descriptor->array_stride <
                pc_gx_raw_geometry_array_value_bytes(attr, descriptor)) {
            batch->invalid = 1;
        }
    }
    batch->known = batch->invalid == 0;
}

static int pc_gx_raw_geometry_current_is_active(void) {
    return g_gx.raw_geometry.live.active != 0;
}

static int pc_gx_raw_geometry_set_current_words(
    int attr,
    const uint32_t* words,
    uint32_t word_count,
    uint32_t source_index,
    int source_index_known
) {
    PCGXRawGeometryAttribute* descriptor;
    uint32_t normalized[PC_GX_GEOMETRY_MAX_VALUE_WORDS];
    uint32_t attribute_bit;

    if (!pc_gx_raw_geometry_current_is_active()) return 0;
    if (attr < 0 || attr >= PC_GX_MAX_ATTR || words == NULL ||
        word_count > PC_GX_GEOMETRY_MAX_VALUE_WORDS) {
        pc_gx_raw_geometry_mark_invalid();
        return 0;
    }
    descriptor = &g_gx.raw_geometry.live.attr[attr];
    attribute_bit = UINT32_C(1) << attr;
    if (descriptor->vcd_type == GX_NONE ||
        descriptor->vcd_type > GX_INDEX16 ||
        (descriptor->vcd_type == GX_DIRECT && source_index_known) ||
        (descriptor->vcd_type != GX_DIRECT && !source_index_known)) {
        pc_gx_raw_geometry_mark_invalid();
        g_gx.raw_geometry.current.attribute_known_mask &= ~attribute_bit;
        memset(g_gx.raw_geometry.current.raw_words[attr], 0,
               sizeof(g_gx.raw_geometry.current.raw_words[attr]));
        return 0;
    }
    if (word_count != descriptor->value_word_count) {
        pc_gx_raw_geometry_mark_invalid();
        g_gx.raw_geometry.current.attribute_known_mask &= ~attribute_bit;
        return 0;
    }
    memset(normalized, 0, sizeof(normalized));
    memcpy(normalized, words, word_count * sizeof(uint32_t));
    if (attr == GX_VA_CLR0 && descriptor->vat_type == GX_RGBX8)
        normalized[0] &= UINT32_C(0xFFFFFF00);
    if (!pc_gx_raw_geometry_words_are_valid(
            attr, descriptor, normalized, word_count)) {
        pc_gx_raw_geometry_mark_invalid();
        g_gx.raw_geometry.current.attribute_known_mask &= ~attribute_bit;
        memset(g_gx.raw_geometry.current.raw_words[attr], 0,
               sizeof(g_gx.raw_geometry.current.raw_words[attr]));
        return 0;
    }
    memcpy(g_gx.raw_geometry.current.raw_words[attr], normalized,
           word_count * sizeof(uint32_t));
    g_gx.raw_geometry.current.attribute_known_mask |= attribute_bit;
    if (source_index_known) {
        g_gx.raw_geometry.current.source_index[attr] = source_index;
        g_gx.raw_geometry.current.source_index_known_mask |=
            UINT32_C(1) << attr;
    }
    return 1;
}

static uint32_t pc_gx_raw_geometry_read_u16(const uint8_t* bytes) {
    uint16_t value;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

static uint32_t pc_gx_raw_geometry_read_u32(const uint8_t* bytes) {
    uint32_t value;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

static int pc_gx_raw_geometry_read_array_words(
    int attr,
    uint32_t index,
    uint32_t* words,
    uint32_t word_count
) {
    PCGXRawGeometryAttribute* descriptor;
    const PCGXRawGeometryArray* array;
    const uint8_t* base;
    uint64_t offset;
    uint32_t component_count;
    uint32_t scalar_bytes;
    uint32_t value_bytes;
    uint32_t component;
    int array_attr;

    if (!pc_gx_raw_geometry_current_is_active() || words == NULL ||
        attr < 0 || attr >= PC_GX_MAX_ATTR) return 0;
    descriptor = &g_gx.raw_geometry.live.attr[attr];
    if (descriptor->vcd_type != GX_INDEX8 &&
        descriptor->vcd_type != GX_INDEX16) return 0;
    if (!pc_gx_raw_geometry_is_supported_attr(attr)) return 0;
    array_attr = pc_gx_raw_geometry_array_attr(attr);
    if (array_attr < GX_VA_POS || array_attr > GX_LIGHT_ARRAY) return 0;
    array = &g_gx.raw_geometry.array[array_attr];
    base = (const uint8_t*)g_gx.array_base[array_attr];
    if (!array->known || !array->data_known || base == NULL ||
        array->generation != descriptor->array_generation ||
        word_count != descriptor->value_word_count) return 0;

    value_bytes = pc_gx_raw_geometry_array_value_bytes(attr, descriptor);
    if (value_bytes == 0 || array->stride < value_bytes) return 0;
    offset = (uint64_t)index * array->stride;
    if (offset > array->byte_size ||
        value_bytes > array->byte_size - offset) return 0;

    if (pc_gx_raw_geometry_is_color_attr(attr)) {
        if (descriptor->vat_type == GX_RGB565 ||
            descriptor->vat_type == GX_RGBA4) {
            words[0] = pc_gx_raw_geometry_read_u16(base + offset);
        } else if (descriptor->vat_type == GX_RGB8) {
            words[0] = ((uint32_t)base[offset] << 16) |
                ((uint32_t)base[offset + 1] << 8) | base[offset + 2];
        } else if (descriptor->vat_type == GX_RGBX8) {
            words[0] = ((uint32_t)base[offset] << 24) |
                ((uint32_t)base[offset + 1] << 16) |
                ((uint32_t)base[offset + 2] << 8) | base[offset + 3];
        } else if (descriptor->vat_type == GX_RGBA6) {
            words[0] = ((uint32_t)base[offset] << 16) |
                ((uint32_t)base[offset + 1] << 8) | base[offset + 2];
        } else {
            /* GX_RGBA8 arrays use GXColor byte order, while the raw direct
             * API carries the equivalent RRGGBBAA word. */
            words[0] = ((uint32_t)base[offset] << 24) |
                ((uint32_t)base[offset + 1] << 16) |
                ((uint32_t)base[offset + 2] << 8) | base[offset + 3];
        }
        return 1;
    }
    component_count = pc_gx_raw_geometry_expected_components(
        attr, descriptor->vat_count);
    scalar_bytes = pc_gx_raw_geometry_scalar_bytes(descriptor->vat_type);
    if (scalar_bytes == 0 || component_count > word_count) return 0;
    for (component = 0; component < component_count; component++) {
        const uint8_t* source = base + offset + component * scalar_bytes;
        if (descriptor->vat_type == GX_U8)
            words[component] = source[0];
        else if (descriptor->vat_type == GX_S8)
            words[component] = source[0];
        else if (descriptor->vat_type == GX_U16)
            words[component] = pc_gx_raw_geometry_read_u16(source);
        else if (descriptor->vat_type == GX_S16)
            words[component] = pc_gx_raw_geometry_read_u16(source);
        else
            words[component] = pc_gx_raw_geometry_read_u32(source);
    }
    while (component < word_count) words[component++] = 0;
    return 1;
}

static int pc_gx_raw_geometry_set_indexed(
    int attr,
    uint32_t index,
    uint32_t emitted_vcd_type
) {
    uint32_t words[PC_GX_GEOMETRY_MAX_VALUE_WORDS];
    PCGXRawGeometryAttribute* descriptor;

    memset(words, 0, sizeof(words));
    if (!pc_gx_raw_geometry_current_is_active() || attr < 0 ||
        attr >= PC_GX_MAX_ATTR) return 0;
    descriptor = &g_gx.raw_geometry.live.attr[attr];
    /* The API entry point carries the encoded index width.  Do this check
     * before decoding the array so a width/API mismatch cannot be accepted
     * merely because the host-side array happens to be readable. */
    if ((emitted_vcd_type != GX_INDEX8 && emitted_vcd_type != GX_INDEX16) ||
        descriptor->vcd_type != emitted_vcd_type) {
        pc_gx_raw_geometry_mark_invalid();
        memset(g_gx.raw_geometry.current.raw_words[attr], 0,
               sizeof(g_gx.raw_geometry.current.raw_words[attr]));
        g_gx.raw_geometry.current.source_index[attr] = index;
        g_gx.raw_geometry.current.source_index_known_mask |=
            UINT32_C(1) << attr;
        return 0;
    }
    if (!pc_gx_raw_geometry_read_array_words(
            attr, index, words, descriptor->value_word_count)) {
        pc_gx_raw_geometry_mark_invalid();
        memset(g_gx.raw_geometry.current.raw_words[attr], 0,
               sizeof(g_gx.raw_geometry.current.raw_words[attr]));
        g_gx.raw_geometry.current.source_index[attr] = index;
        g_gx.raw_geometry.current.source_index_known_mask |=
            UINT32_C(1) << attr;
        return 0;
    }
    if (!pc_gx_raw_geometry_set_current_words(
            attr, words, descriptor->value_word_count, index, 1)) {
        g_gx.raw_geometry.current.source_index[attr] = index;
        g_gx.raw_geometry.current.source_index_known_mask |=
            UINT32_C(1) << attr;
        return 0;
    }
    return 1;
}

static int pc_gx_raw_geometry_host_scalar_word(
    int attr,
    const PCGXRawGeometryAttribute* descriptor,
    uint32_t word,
    f32* value
) {
    int32_t signed_value;

    if (value == NULL) return 0;
    if (descriptor->vat_type == GX_F32) {
        memcpy(value, &word, sizeof(*value));
    } else if (attr == GX_VA_NRM) {
        if (descriptor->vat_type == GX_S8) {
            signed_value = (int8_t)(uint8_t)word;
            *value = (f32)signed_value / 127.0f;
        } else if (descriptor->vat_type == GX_S16) {
            signed_value = (int16_t)(uint16_t)word;
            *value = (f32)signed_value / 32767.0f;
        } else {
            return 0;
        }
    } else {
        switch (descriptor->vat_type) {
        case GX_U8:
            signed_value = (int32_t)(uint8_t)word;
            break;
        case GX_S8:
            signed_value = (int8_t)(uint8_t)word;
            break;
        case GX_U16:
            signed_value = (int32_t)(uint16_t)word;
            break;
        case GX_S16:
            signed_value = (int16_t)(uint16_t)word;
            break;
        default:
            return 0;
        }
        *value = ldexpf((f32)signed_value,
                        -(int)descriptor->vat_fraction);
    }
    return isfinite(*value);
}

static int pc_gx_raw_geometry_host_array_scalar(
    int attr,
    uint32_t index,
    f32* values,
    uint32_t value_count
) {
    PCGXRawGeometryAttribute* descriptor;
    uint32_t words[PC_GX_GEOMETRY_MAX_VALUE_WORDS];
    uint32_t component_count;
    uint32_t component;

    if (!pc_gx_raw_geometry_is_scalar_attr(attr) || values == NULL ||
        value_count == 0 || !pc_gx_raw_geometry_current_is_active()) {
        return 0;
    }
    descriptor = &g_gx.raw_geometry.live.attr[attr];
    component_count = pc_gx_raw_geometry_expected_components(
        attr, descriptor->vat_count);
    if (component_count == 0 || component_count > value_count ||
        descriptor->value_word_count > PC_GX_GEOMETRY_MAX_VALUE_WORDS) {
        return 0;
    }
    memset(words, 0, sizeof(words));
    if (!pc_gx_raw_geometry_read_array_words(
            attr, index, words, descriptor->value_word_count)) {
        return 0;
    }
    memset(values, 0, value_count * sizeof(values[0]));
    for (component = 0; component < component_count; component++) {
        if (!pc_gx_raw_geometry_host_scalar_word(
                attr, descriptor, words[component], &values[component])) {
            memset(values, 0, value_count * sizeof(values[0]));
            return 0;
        }
    }
    return 1;
}

static const uint8_t* pc_gx_raw_geometry_host_color_array_element(
    uint32_t index
) {
    const PCGXRawGeometryAttribute* descriptor =
        &g_gx.raw_geometry.live.attr[GX_VA_CLR0];
    const PCGXRawGeometryArray* array =
        &g_gx.raw_geometry.array[GX_VA_CLR0];
    const uint8_t* base = (const uint8_t*)g_gx.array_base[GX_VA_CLR0];
    uint32_t value_bytes;
    uint64_t offset;

    /* The legacy host mirror is four bytes wide.  Keep the older no-op
     * behavior for two/three-byte color arrays, but never read across an
     * element boundary while doing so. */
    value_bytes = pc_gx_raw_geometry_array_value_bytes(
        GX_VA_CLR0, descriptor);
    if (!pc_gx_raw_geometry_current_is_active() ||
        (descriptor->vcd_type != GX_INDEX8 &&
         descriptor->vcd_type != GX_INDEX16) || value_bytes != 4 ||
        !array->known || !array->data_known || base == NULL ||
        array->generation != descriptor->array_generation ||
        array->stride < value_bytes) {
        return NULL;
    }
    offset = (uint64_t)index * array->stride;
    if (offset > array->byte_size ||
        value_bytes > array->byte_size - offset) {
        return NULL;
    }
    return base + offset;
}

static void pc_gx_raw_geometry_commit_current(void) {
    PCGXRawGeometryBatch* batch = &g_gx.raw_geometry.live;
    uint32_t vertex;
    int attr;

    if (batch->active == 0) return;
    vertex = batch->vertex_count;
    if (vertex >= PC_GX_GEOMETRY_MAX_VERTICES) {
        pc_gx_raw_geometry_mark_invalid();
        return;
    }
    for (attr = 0; attr < PC_GX_MAX_ATTR; attr++) {
        PCGXRawGeometryAttribute* descriptor = &batch->attr[attr];
        uint32_t bit = UINT32_C(1) << attr;
        uint32_t record = UINT32_MAX;
        uint32_t source_index = 0;
        int known = (g_gx.raw_geometry.current.attribute_known_mask & bit) != 0;

        if (descriptor->vcd_type == GX_NONE) continue;
        if (!known) batch->invalid = 1;
        if (descriptor->vcd_type == GX_DIRECT) {
            record = descriptor->value_count;
            if (record >= PC_GX_GEOMETRY_MAX_VERTICES) {
                pc_gx_raw_geometry_mark_invalid();
                continue;
            }
            memcpy(descriptor->value_words[record],
                   g_gx.raw_geometry.current.raw_words[attr],
                   sizeof(descriptor->value_words[record]));
            descriptor->value_known[record] = (uint8_t)known;
            descriptor->value_source_index[record] = 0;
            descriptor->value_count++;
        } else {
            source_index = g_gx.raw_geometry.current.source_index[attr];
            if ((g_gx.raw_geometry.current.source_index_known_mask & bit) == 0)
                known = 0;
            for (record = 0; record < descriptor->value_count; record++) {
                if (descriptor->value_source_index[record] == source_index)
                    break;
            }
            if (record == descriptor->value_count) {
                if (record >= PC_GX_GEOMETRY_MAX_VERTICES) {
                    pc_gx_raw_geometry_mark_invalid();
                    continue;
                }
                memcpy(descriptor->value_words[record],
                       g_gx.raw_geometry.current.raw_words[attr],
                       sizeof(descriptor->value_words[record]));
                descriptor->value_source_index[record] = source_index;
                descriptor->value_known[record] = (uint8_t)known;
                descriptor->value_count++;
            } else if (!known) {
                descriptor->value_known[record] = 0;
                memset(descriptor->value_words[record], 0,
                       sizeof(descriptor->value_words[record]));
            }
            descriptor->index_values[vertex] = record;
            descriptor->source_indices[vertex] = source_index;
            descriptor->index_known[vertex] = (uint8_t)known;
            descriptor->index_count++;
        }
    }
    batch->vertex_count++;
    if (g_gx.raw_geometry.current.invalid != 0) batch->invalid = 1;
    pc_gx_raw_geometry_clear_current();
}

static void pc_gx_raw_geometry_capture_completed(int count) {
    PCGXRawGeometryBatch* batch = &g_gx.raw_geometry.live;

    if (batch->active == 0) return;
    if ((uint32_t)count != batch->vertex_count ||
        batch->vertex_count != batch->expected_vertex_count) {
        batch->invalid = 1;
    }
    batch->known = batch->invalid == 0;
    batch->active = 0;
    g_gx.raw_geometry.completed = *batch;
    g_gx.raw_geometry.completed.active = 0;
}

static void pc_gx_raw_geometry_begin_position_call(void) {
    if (!pc_gx_raw_geometry_current_is_active()) return;
    if (g_gx.vertex_pending) pc_gx_raw_geometry_commit_current();
    pc_gx_raw_geometry_clear_current();
}

static void pc_gx_raw_geometry_set_scalar_direct(
    int attr,
    uint32_t source_type,
    uint32_t component_count,
    const uint32_t* source_words
) {
    PCGXRawGeometryAttribute* descriptor;
    uint32_t expected_components;
    uint32_t words[PC_GX_GEOMETRY_MAX_VALUE_WORDS];

    memset(words, 0, sizeof(words));
    if (!pc_gx_raw_geometry_current_is_active()) return;
    if (attr < 0 || attr >= PC_GX_MAX_ATTR || source_words == NULL) {
        pc_gx_raw_geometry_mark_invalid();
        return;
    }
    descriptor = &g_gx.raw_geometry.live.attr[attr];
    expected_components = pc_gx_raw_geometry_expected_components(
        attr, descriptor->vat_count);
    if (descriptor->vcd_type != GX_DIRECT ||
        !pc_gx_raw_geometry_is_scalar_attr(attr) ||
        descriptor->vat_type != source_type ||
        component_count != expected_components ||
        descriptor->value_word_count > PC_GX_GEOMETRY_MAX_VALUE_WORDS) {
        pc_gx_raw_geometry_mark_invalid();
        return;
    }
    memcpy(words, source_words, component_count * sizeof(uint32_t));
    pc_gx_raw_geometry_set_current_words(
        attr, words, descriptor->value_word_count, 0, 0);
}

static int pc_gx_raw_geometry_color_entry_width_is_valid(
    const PCGXRawGeometryAttribute* descriptor,
    uint32_t entry_width
) {
    if (descriptor->vcd_type != GX_DIRECT) return 0;
    if (descriptor->vat_count == GX_CLR_RGB) {
        return (descriptor->vat_type == GX_RGB565 && entry_width == 2) ||
            (descriptor->vat_type == GX_RGB8 && entry_width == 3) ||
            (descriptor->vat_type == GX_RGBX8 && entry_width == 4);
    }
    if (descriptor->vat_count == GX_CLR_RGBA) {
        return (descriptor->vat_type == GX_RGBA4 && entry_width == 2) ||
            (descriptor->vat_type == GX_RGBA6 && entry_width == 3) ||
            (descriptor->vat_type == GX_RGBA8 && entry_width == 4);
    }
    return 0;
}

static void pc_gx_raw_geometry_set_color_direct_u8(
    uint32_t red,
    uint32_t green,
    uint32_t blue,
    uint32_t alpha,
    uint32_t entry_width
) {
    PCGXRawGeometryAttribute* descriptor;
    uint32_t packed;

    if (!pc_gx_raw_geometry_current_is_active()) return;
    descriptor = &g_gx.raw_geometry.live.attr[GX_VA_CLR0];
    if (!pc_gx_raw_geometry_color_entry_width_is_valid(
            descriptor, entry_width)) {
        pc_gx_raw_geometry_mark_invalid();
        return;
    }
    if (entry_width == 3)
        packed = (red << 16) | (green << 8) | blue;
    else if (entry_width == 4)
        packed = (red << 24) | (green << 16) | (blue << 8) | alpha;
    else {
        pc_gx_raw_geometry_mark_invalid();
        return;
    }
    pc_gx_raw_geometry_set_current_words(GX_VA_CLR0, &packed, 1, 0, 0);
}

static void pc_gx_raw_geometry_set_color_direct_u32(
    uint32_t packed,
    uint32_t entry_width
) {
    PCGXRawGeometryAttribute* descriptor;

    if (!pc_gx_raw_geometry_current_is_active()) return;
    descriptor = &g_gx.raw_geometry.live.attr[GX_VA_CLR0];
    if (!pc_gx_raw_geometry_color_entry_width_is_valid(
            descriptor, entry_width)) {
        pc_gx_raw_geometry_mark_invalid();
        return;
    }
    pc_gx_raw_geometry_set_current_words(GX_VA_CLR0, &packed, 1, 0, 0);
}

static void pc_gx_raw_geometry_set_color_direct_u16(uint16_t packed) {
    PCGXRawGeometryAttribute* descriptor;

    if (!pc_gx_raw_geometry_current_is_active()) return;
    descriptor = &g_gx.raw_geometry.live.attr[GX_VA_CLR0];
    if (!pc_gx_raw_geometry_color_entry_width_is_valid(descriptor, 2)) {
        pc_gx_raw_geometry_mark_invalid();
        return;
    }
    {
        uint32_t word = packed;
        pc_gx_raw_geometry_set_current_words(
            GX_VA_CLR0, &word, 1, 0, 0);
    }
}

/* Keep the v2 handoff ABI separate from the existing v1 callback. */
typedef void (*PCGXSemanticPacketV2HandoffCallback)(
    void* context,
    const AcgcGxSemanticPacketV2* packet
);
static PCGXSemanticPacketV2HandoffCallback s_semantic_packet_v2_handoff;
static void* s_semantic_packet_v2_handoff_context;

/* V3 is a separate, state-forwarding ABI. It never enters the v1/v2 seam. */
typedef void (*PCGXSemanticPacketV3HandoffCallback)(
    void* context,
    const AcgcGxSemanticPacketV3* packet
);
static PCGXSemanticPacketV3HandoffCallback s_semantic_packet_v3_handoff;
static void* s_semantic_packet_v3_handoff_context;

/* V4 carries the live alpha-write state and is a separate typed seam. */
typedef void (*PCGXSemanticPacketV4HandoffCallback)(
    void* context,
    const AcgcGxSemanticPacketV4* packet
);
static PCGXSemanticPacketV4HandoffCallback s_semantic_packet_v4_handoff;
static void* s_semantic_packet_v4_handoff_context;
static unsigned int s_semantic_packet_v2_trace_count;
static unsigned int s_semantic_packet_v4_trace_count;

typedef enum {
    PCGX_SEMANTIC_V2_REJECTION_VERTEX_OR_COUNT = 0,
    PCGX_SEMANTIC_V2_REJECTION_GLOBAL_COUNT,
    PCGX_SEMANTIC_V2_REJECTION_ALPHA_TEST,
    PCGX_SEMANTIC_V2_REJECTION_BLEND,
    PCGX_SEMANTIC_V2_REJECTION_DEPTH,
    PCGX_SEMANTIC_V2_REJECTION_COLOR_ALPHA_UPDATE,
    PCGX_SEMANTIC_V2_REJECTION_CULL,
    PCGX_SEMANTIC_V2_REJECTION_MATRIX_PROJECTION,
    PCGX_SEMANTIC_V2_REJECTION_CHANNEL,
    PCGX_SEMANTIC_V2_REJECTION_STAGE_TEXTURE,
    PCGX_SEMANTIC_V2_REJECTION_TEXGEN,
    PCGX_SEMANTIC_V2_REJECTION_SUPPORTED
} PCGXSemanticV2RejectionReason;

static const char* pc_gx_semantic_v2_rejection_reason_name(
    PCGXSemanticV2RejectionReason reason
);
static PCGXSemanticV2RejectionReason pc_gx_semantic_v2_rejection_reason(
    int first_vertex,
    int vertex_count,
    int expected_vertex_count
);

/* Legacy v2/v3/v4 gates still read these mirrors.  Durable setter-owned
 * Texgen provenance lives in PCGXState.raw_texgen below; these arrays are not
 * a canonical source and are reset with that shadow. */
static int s_tex_gen_extended_state_known[8];
static GXBool s_tex_gen_normalize[8];
static u32 s_tex_gen_post_mtx[8];

/* Shared IEEE-754 binary32 validation helpers are defined with the existing
 * Transform provenance code below the Texgen helpers. */
static int pc_gx_transform_words_are_finite(
    const uint32_t* words,
    size_t count
);
static int pc_gx_raw_texgen_bool_is_valid(uint32_t value);

/* Opt-in diagnostics for the live Apple bridge gate. The trace is deliberately
 * runtime-only and bounded; normal Windows/default behavior remains unchanged. */
static void pc_gx_trace_semantic_packet_v2(
    int first_vertex,
    int vertex_count,
    int expected_vertex_count,
    int result
) {
    const char* enabled = getenv("ACGC_METAL_REJECTION_TRACE");
    const PCGXTevStage* stage;
    PCGXSemanticV2RejectionReason reason;

    if (enabled == NULL || enabled[0] == '\0' ||
        s_semantic_packet_v2_trace_count >= 64) {
        return;
    }
    s_semantic_packet_v2_trace_count++;
    stage = &g_gx.tev_stages[0];
    reason = pc_gx_semantic_v2_rejection_reason(
        first_vertex,
        vertex_count,
        expected_vertex_count
    );
    fprintf(
        stderr,
        "[ACGC_V2_TRACE] n=%u result=%d reason=%s first=%d count=%d "
        "expected=%d current=%d pending=%d begin=%d expected_state=%d prim=%d "
        "chans=%d texgens=%d tev=%d ind=%d fog=%d "
        "alpha=%d/%d/%d refs=%d/%d update=%d "
        "blend=%d/%d/%d/%d z=%d/%d/%d color=%d "
        "cull=%d mtx=%d proj=%d stage0=%d/%d texgen0=%d/%d/%d/%d "
        "tex0=%u/%d/%d/%d known=%d\n",
        s_semantic_packet_v2_trace_count,
        result,
        pc_gx_semantic_v2_rejection_reason_name(reason),
        first_vertex,
        vertex_count,
        expected_vertex_count,
        g_gx.current_vertex_idx,
        g_gx.pending_verts,
        g_gx.in_begin,
        g_gx.expected_vertex_count,
        g_gx.current_primitive,
        g_gx.num_chans,
        g_gx.num_tex_gens,
        g_gx.num_tev_stages,
        g_gx.num_ind_stages,
        g_gx.fog_type,
        g_gx.alpha_comp0,
        g_gx.alpha_comp1,
        g_gx.alpha_op,
        g_gx.alpha_ref0,
        g_gx.alpha_ref1,
        g_gx.alpha_update_enable,
        g_gx.blend_mode,
        g_gx.blend_src,
        g_gx.blend_dst,
        g_gx.blend_logic_op,
        g_gx.z_compare_enable,
        g_gx.z_compare_func,
        g_gx.z_update_enable,
        g_gx.color_update_enable,
        g_gx.cull_mode,
        g_gx.current_mtx,
        g_gx.projection_type,
        stage->tex_coord,
        stage->tex_map,
        g_gx.tex_gen_type[0],
        g_gx.tex_gen_src[0],
        g_gx.tex_gen_mtx[0],
        s_tex_gen_post_mtx[0],
        g_gx.gl_textures[0],
        g_gx.tex_obj_w[0],
        g_gx.tex_obj_h[0],
        g_gx.tex_obj_fmt[0],
        s_tex_gen_extended_state_known[0]
    );
}

#ifdef PC_ENHANCEMENTS
/* Aspect correction: factor = gc_aspect/actual_aspect, offset = content left edge in GC coords */
static float g_aspect_factor = 1.0f;
static float g_aspect_offset = 0.0f;
static int   g_aspect_active = 0;

static void pc_gx_update_aspect(void) {
    float gc_aspect = (float)PC_GC_WIDTH / (float)PC_GC_HEIGHT;
    float win_aspect = (float)g_pc_window_w / (float)g_pc_window_h;
    if (win_aspect > gc_aspect + 0.01f) {
        g_aspect_factor = gc_aspect / win_aspect;
        g_aspect_offset = (1.0f - g_aspect_factor) / 2.0f * (float)PC_GC_WIDTH;
        g_aspect_active = 1;
    } else {
        g_aspect_factor = 1.0f;
        g_aspect_offset = 0.0f;
        g_aspect_active = 0;
    }
}

#ifdef PC_DARWIN_COMPILE_AUDIT
void pc_gx_transform_fixture_set_aspect(int active, float factor) {
    g_aspect_active = active != 0;
    g_aspect_factor = factor;
}
#endif

/* EFB capture: keep full-res GL textures from GXCopyTex instead of downsampling to 640x480 */
#define MAX_EFB_CAPTURES 4
static struct {
    u32 dest_ptr;
    GLuint gl_tex;
} s_efb_captures[MAX_EFB_CAPTURES];
static int s_efb_capture_count = 0;

void pc_gx_efb_capture_store(u32 dest_ptr, GLuint gl_tex) {
    for (int i = 0; i < s_efb_capture_count; i++) {
        if (s_efb_captures[i].dest_ptr == dest_ptr) {
            if (s_efb_captures[i].gl_tex)
                glDeleteTextures(1, &s_efb_captures[i].gl_tex);
            s_efb_captures[i].gl_tex = gl_tex;
            return;
        }
    }
    if (s_efb_capture_count >= MAX_EFB_CAPTURES) {
        if (s_efb_captures[0].gl_tex)
            glDeleteTextures(1, &s_efb_captures[0].gl_tex);
        memmove(&s_efb_captures[0], &s_efb_captures[1],
                (MAX_EFB_CAPTURES - 1) * sizeof(s_efb_captures[0]));
        s_efb_capture_count = MAX_EFB_CAPTURES - 1;
    }
    s_efb_captures[s_efb_capture_count].dest_ptr = dest_ptr;
    s_efb_captures[s_efb_capture_count].gl_tex = gl_tex;
    s_efb_capture_count++;
}

GLuint pc_gx_efb_capture_find(u32 data_ptr) {
    for (int i = 0; i < s_efb_capture_count; i++) {
        if (s_efb_captures[i].dest_ptr == data_ptr)
            return s_efb_captures[i].gl_tex;
    }
    return 0;
}

void pc_gx_efb_capture_cleanup(void) {
    for (int i = 0; i < s_efb_capture_count; i++) {
        if (s_efb_captures[i].gl_tex)
            glDeleteTextures(1, &s_efb_captures[i].gl_tex);
    }
    s_efb_capture_count = 0;
}
#endif

typedef struct {
    int active;
    u8* buf;
    u32 size;
    u32 off;
    int overflow;
} PCGXDLBuildState;

static PCGXDLBuildState g_pc_gx_dl = {0};

enum {
    PCGX_DL_OP_TEXCOPY_SRC = 0x1001,
    PCGX_DL_OP_TEXCOPY_DST = 0x1002,
    PCGX_DL_OP_COPY_FILTER = 0x1003,
    PCGX_DL_OP_COPY_TEX = 0x1004,
};

static void pc_gx_dl_write(const void* data, u32 len) {
    if (!g_pc_gx_dl.active || g_pc_gx_dl.overflow) return;
    if (g_pc_gx_dl.off + len > g_pc_gx_dl.size) {
        g_pc_gx_dl.overflow = 1;
        return;
    }
    memcpy(g_pc_gx_dl.buf + g_pc_gx_dl.off, data, len);
    g_pc_gx_dl.off += len;
}

static void pc_unpack_rgba8f(u32 packed, float* out_rgba) {
    /* Shift-based: works for colors packed as (r<<24|g<<16|b<<8|a) from N64 DL data */
    out_rgba[0] = ((packed >> 24) & 0xFF) / 255.0f;
    out_rgba[1] = ((packed >> 16) & 0xFF) / 255.0f;
    out_rgba[2] = ((packed >> 8) & 0xFF) / 255.0f;
    out_rgba[3] = (packed & 0xFF) / 255.0f;
}

/* Byte-based: reads RGBA from memory order. Use for GXColor structs (not N64 DL data). */
static void pc_unpack_gxcolor_f(u32 color_as_u32, float* out_rgba) {
    const u8* bytes = (const u8*)&color_as_u32;
    out_rgba[0] = bytes[0] / 255.0f;
    out_rgba[1] = bytes[1] / 255.0f;
    out_rgba[2] = bytes[2] / 255.0f;
    out_rgba[3] = bytes[3] / 255.0f;
}

static void pc_unpack_rgba8_raw(u32 packed, int32_t* out_rgba) {
    out_rgba[0] = (int32_t)((packed >> 24) & 0xFFu);
    out_rgba[1] = (int32_t)((packed >> 16) & 0xFFu);
    out_rgba[2] = (int32_t)((packed >> 8) & 0xFFu);
    out_rgba[3] = (int32_t)(packed & 0xFFu);
}

static void pc_unpack_gxcolor_raw(u32 color_as_u32, int32_t* out_rgba) {
    const u8* bytes = (const u8*)&color_as_u32;
    out_rgba[0] = (int32_t)bytes[0];
    out_rgba[1] = (int32_t)bytes[1];
    out_rgba[2] = (int32_t)bytes[2];
    out_rgba[3] = (int32_t)bytes[3];
}

static void pc_gx_tev_raw_store(
    PCGXTevRawColor* shadow,
    const int32_t* components,
    int valid,
    PCGXTevRawSource source
) {
    memcpy(shadow->components, components, sizeof(shadow->components));
    shadow->valid = valid ? 1 : 0;
    shadow->source = (uint8_t)source;
    memset(shadow->reserved, 0, sizeof(shadow->reserved));
}

static void pc_gx_raw_tev_indirect_mark_invalid(void) {
    g_gx.raw_tev_indirect.invalid = 1;
}

static int pc_gx_raw_tev_indirect_ready(void) {
    return g_gx.raw_tev_indirect.invalid == 0;
}

static PCGXRawTevStage* pc_gx_raw_tev_stage_begin(
    uint32_t stage,
    int* current_valid
) {
    /* A prior sticky failure suppresses publication, not legacy setters.
     * Report the current domain validity separately from the raw pointer. */
    *current_valid = 0;
    if (stage >= ACGC_GX_CANONICAL_TEV_STAGE_COUNT) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return NULL;
    }
    *current_valid = 1;
    if (!pc_gx_raw_tev_indirect_ready()) return NULL;
    return &g_gx.raw_tev_indirect.stages[stage];
}

static int pc_gx_raw_tev_operation_valid(uint32_t operation) {
    return operation <= ACGC_GX_CANONICAL_TEV_OPERATION_SUB ||
        (operation >= ACGC_GX_CANONICAL_TEV_OPERATION_COMPARE_MIN &&
         operation <= ACGC_GX_CANONICAL_TEV_OPERATION_COMPARE_MAX);
}

static int pc_gx_raw_tev_k_color_selector_valid(uint32_t selector) {
    return selector <= ACGC_GX_CANONICAL_TEV_KCOLOR_SELECTOR_LOW_MAX ||
        (selector >= ACGC_GX_CANONICAL_TEV_KCOLOR_SELECTOR_HIGH_MIN &&
         selector <= ACGC_GX_CANONICAL_TEV_KCOLOR_SELECTOR_MAX);
}

static int pc_gx_raw_tev_k_alpha_selector_valid(uint32_t selector) {
    return selector <= ACGC_GX_CANONICAL_TEV_KALPHA_SELECTOR_LOW_MAX ||
        (selector >= ACGC_GX_CANONICAL_TEV_KALPHA_SELECTOR_HIGH_MIN &&
         selector <= ACGC_GX_CANONICAL_TEV_KALPHA_SELECTOR_MAX);
}

static int pc_gx_raw_tev_texcoord_valid(uint32_t tex_coord) {
    return tex_coord <= ACGC_GX_CANONICAL_TEV_TEXCOORD_MAX ||
        tex_coord == ACGC_GX_CANONICAL_TEV_TEXCOORD_NULL;
}

static int pc_gx_raw_tev_texmap_valid(uint32_t tex_map) {
    return tex_map <= ACGC_GX_CANONICAL_TEV_TEXMAP_MAX ||
        tex_map == ACGC_GX_CANONICAL_TEV_TEXMAP_NULL ||
        tex_map == ACGC_GX_CANONICAL_TEV_TEXMAP_DISABLE;
}

static int pc_gx_raw_tev_channel_valid(uint32_t color_chan) {
    return color_chan <= ACGC_GX_CANONICAL_TEV_CHANNEL_MAX ||
        color_chan == ACGC_GX_CANONICAL_TEV_CHANNEL_NULL;
}

static int pc_gx_raw_tev_matrix_selector_valid(uint32_t matrix) {
    return matrix == ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_OFF ||
        (matrix >= ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_0 &&
         matrix <= ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_2) ||
        (matrix >= ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_S0 &&
         matrix <= ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_S2) ||
        (matrix >= ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_T0 &&
         matrix <= ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_T2);
}

static int pc_gx_raw_tev_stage_set_color_in(
    uint32_t stage,
    uint32_t a,
    uint32_t b,
    uint32_t c,
    uint32_t d
) {
    int current_valid;
    PCGXRawTevStage* raw = pc_gx_raw_tev_stage_begin(stage, &current_valid);
    if (!current_valid) return 0;
    if (a > ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX ||
        b > ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX ||
        c > ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX ||
        d > ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    if (raw == NULL) return 1;
    raw->value.color_a = a;
    raw->value.color_b = b;
    raw->value.color_c = c;
    raw->value.color_d = d;
    raw->known_mask |= PC_GX_RAW_TEV_STAGE_COLOR_A |
        PC_GX_RAW_TEV_STAGE_COLOR_B |
        PC_GX_RAW_TEV_STAGE_COLOR_C |
        PC_GX_RAW_TEV_STAGE_COLOR_D;
    return 1;
}

static int pc_gx_raw_tev_stage_set_alpha_in(
    uint32_t stage,
    uint32_t a,
    uint32_t b,
    uint32_t c,
    uint32_t d
) {
    int current_valid;
    PCGXRawTevStage* raw = pc_gx_raw_tev_stage_begin(stage, &current_valid);
    if (!current_valid) return 0;
    if (a > ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX ||
        b > ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX ||
        c > ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX ||
        d > ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    if (raw == NULL) return 1;
    raw->value.alpha_a = a;
    raw->value.alpha_b = b;
    raw->value.alpha_c = c;
    raw->value.alpha_d = d;
    raw->known_mask |= PC_GX_RAW_TEV_STAGE_ALPHA_A |
        PC_GX_RAW_TEV_STAGE_ALPHA_B |
        PC_GX_RAW_TEV_STAGE_ALPHA_C |
        PC_GX_RAW_TEV_STAGE_ALPHA_D;
    return 1;
}

static int pc_gx_raw_tev_stage_set_color_op(
    uint32_t stage,
    uint32_t operation,
    uint32_t bias,
    uint32_t scale,
    uint32_t clamp,
    uint32_t out_reg
) {
    int current_valid;
    PCGXRawTevStage* raw = pc_gx_raw_tev_stage_begin(stage, &current_valid);
    if (!current_valid) return 0;
    if (!pc_gx_raw_tev_operation_valid(operation) ||
        bias > ACGC_GX_CANONICAL_TEV_BIAS_MAX ||
        scale > ACGC_GX_CANONICAL_TEV_SCALE_MAX ||
        clamp > ACGC_GX_CANONICAL_TEV_BOOLEAN_MAX ||
        out_reg > ACGC_GX_CANONICAL_TEV_REGISTER_INDEX_MAX) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    if (raw == NULL) return 1;
    raw->value.color_op = operation;
    raw->value.color_bias = bias;
    raw->value.color_scale = scale;
    raw->value.color_clamp = clamp;
    raw->value.color_out = out_reg;
    raw->known_mask |= PC_GX_RAW_TEV_STAGE_COLOR_OP |
        PC_GX_RAW_TEV_STAGE_COLOR_BIAS |
        PC_GX_RAW_TEV_STAGE_COLOR_SCALE |
        PC_GX_RAW_TEV_STAGE_COLOR_CLAMP |
        PC_GX_RAW_TEV_STAGE_COLOR_OUT;
    return 1;
}

static int pc_gx_raw_tev_stage_set_alpha_op(
    uint32_t stage,
    uint32_t operation,
    uint32_t bias,
    uint32_t scale,
    uint32_t clamp,
    uint32_t out_reg
) {
    int current_valid;
    PCGXRawTevStage* raw = pc_gx_raw_tev_stage_begin(stage, &current_valid);
    if (!current_valid) return 0;
    if (!pc_gx_raw_tev_operation_valid(operation) ||
        bias > ACGC_GX_CANONICAL_TEV_BIAS_MAX ||
        scale > ACGC_GX_CANONICAL_TEV_SCALE_MAX ||
        clamp > ACGC_GX_CANONICAL_TEV_BOOLEAN_MAX ||
        out_reg > ACGC_GX_CANONICAL_TEV_REGISTER_INDEX_MAX) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    if (raw == NULL) return 1;
    raw->value.alpha_op = operation;
    raw->value.alpha_bias = bias;
    raw->value.alpha_scale = scale;
    raw->value.alpha_clamp = clamp;
    raw->value.alpha_out = out_reg;
    raw->known_mask |= PC_GX_RAW_TEV_STAGE_ALPHA_OP |
        PC_GX_RAW_TEV_STAGE_ALPHA_BIAS |
        PC_GX_RAW_TEV_STAGE_ALPHA_SCALE |
        PC_GX_RAW_TEV_STAGE_ALPHA_CLAMP |
        PC_GX_RAW_TEV_STAGE_ALPHA_OUT;
    return 1;
}

static int pc_gx_raw_tev_stage_set_order(
    uint32_t stage,
    uint32_t tex_coord,
    uint32_t tex_map,
    uint32_t color_chan
) {
    int current_valid;
    PCGXRawTevStage* raw = pc_gx_raw_tev_stage_begin(stage, &current_valid);
    if (!current_valid) return 0;
    if (!pc_gx_raw_tev_texcoord_valid(tex_coord) ||
        !pc_gx_raw_tev_texmap_valid(tex_map) ||
        !pc_gx_raw_tev_channel_valid(color_chan)) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    if (raw == NULL) return 1;
    raw->value.tex_coord = tex_coord;
    raw->value.tex_map = tex_map;
    raw->value.color_chan = color_chan;
    raw->known_mask |= PC_GX_RAW_TEV_STAGE_TEX_COORD |
        PC_GX_RAW_TEV_STAGE_TEX_MAP |
        PC_GX_RAW_TEV_STAGE_COLOR_CHAN;
    return 1;
}

static int pc_gx_raw_tev_stage_set_k_color(
    uint32_t stage,
    uint32_t selector
) {
    int current_valid;
    PCGXRawTevStage* raw = pc_gx_raw_tev_stage_begin(stage, &current_valid);
    if (!current_valid) return 0;
    if (!pc_gx_raw_tev_k_color_selector_valid(selector)) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    if (raw == NULL) return 1;
    raw->value.k_color_sel = selector;
    raw->known_mask |= PC_GX_RAW_TEV_STAGE_K_COLOR_SEL;
    return 1;
}

static int pc_gx_raw_tev_stage_set_k_alpha(
    uint32_t stage,
    uint32_t selector
) {
    int current_valid;
    PCGXRawTevStage* raw = pc_gx_raw_tev_stage_begin(stage, &current_valid);
    if (!current_valid) return 0;
    if (!pc_gx_raw_tev_k_alpha_selector_valid(selector)) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    if (raw == NULL) return 1;
    raw->value.k_alpha_sel = selector;
    raw->known_mask |= PC_GX_RAW_TEV_STAGE_K_ALPHA_SEL;
    return 1;
}

static int pc_gx_raw_tev_stage_set_swap(
    uint32_t stage,
    uint32_t ras_sel,
    uint32_t tex_sel
) {
    int current_valid;
    PCGXRawTevStage* raw = pc_gx_raw_tev_stage_begin(stage, &current_valid);
    if (!current_valid) return 0;
    if (ras_sel > ACGC_GX_CANONICAL_TEV_SWAP_MAX ||
        tex_sel > ACGC_GX_CANONICAL_TEV_SWAP_MAX) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    if (raw == NULL) return 1;
    raw->value.ras_swap = ras_sel;
    raw->value.tex_swap = tex_sel;
    raw->known_mask |= PC_GX_RAW_TEV_STAGE_RAS_SWAP |
        PC_GX_RAW_TEV_STAGE_TEX_SWAP;
    return 1;
}

static int pc_gx_raw_tev_stage_set_indirect(
    uint32_t stage,
    uint32_t ind_stage,
    uint32_t format,
    uint32_t bias,
    uint32_t matrix,
    uint32_t wrap_s,
    uint32_t wrap_t,
    uint32_t add_prev,
    uint32_t ind_lod,
    uint32_t alpha
) {
    int current_valid;
    PCGXRawTevStage* raw = pc_gx_raw_tev_stage_begin(stage, &current_valid);
    if (!current_valid) return 0;
    if (ind_stage > ACGC_GX_CANONICAL_TEV_INDIRECT_STAGE_MAX ||
        format > ACGC_GX_CANONICAL_TEV_INDIRECT_FORMAT_MAX ||
        bias > ACGC_GX_CANONICAL_TEV_INDIRECT_BIAS_MAX ||
        !pc_gx_raw_tev_matrix_selector_valid(matrix) ||
        wrap_s > ACGC_GX_CANONICAL_TEV_INDIRECT_WRAP_MAX ||
        wrap_t > ACGC_GX_CANONICAL_TEV_INDIRECT_WRAP_MAX ||
        add_prev > ACGC_GX_CANONICAL_TEV_BOOLEAN_MAX ||
        ind_lod > ACGC_GX_CANONICAL_TEV_BOOLEAN_MAX ||
        alpha > ACGC_GX_CANONICAL_TEV_INDIRECT_ALPHA_MAX) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    if (raw == NULL) return 1;
    raw->value.ind_stage = ind_stage;
    raw->value.ind_format = format;
    raw->value.ind_bias = bias;
    raw->value.ind_mtx = matrix;
    raw->value.ind_wrap_s = wrap_s;
    raw->value.ind_wrap_t = wrap_t;
    raw->value.ind_add_prev = add_prev;
    raw->value.ind_lod = ind_lod;
    raw->value.ind_alpha = alpha;
    raw->known_mask |= PC_GX_RAW_TEV_STAGE_IND_STAGE |
        PC_GX_RAW_TEV_STAGE_IND_FORMAT |
        PC_GX_RAW_TEV_STAGE_IND_BIAS |
        PC_GX_RAW_TEV_STAGE_IND_MTX |
        PC_GX_RAW_TEV_STAGE_IND_WRAP_S |
        PC_GX_RAW_TEV_STAGE_IND_WRAP_T |
        PC_GX_RAW_TEV_STAGE_IND_ADD_PREV |
        PC_GX_RAW_TEV_STAGE_IND_LOD |
        PC_GX_RAW_TEV_STAGE_IND_ALPHA;
    return 1;
}

static int pc_gx_raw_tev_set_num_stages(uint32_t count) {
    if (count < ACGC_GX_CANONICAL_TEV_ACTIVE_STAGE_COUNT_MIN ||
        count > ACGC_GX_CANONICAL_TEV_ACTIVE_STAGE_COUNT_MAX) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    if (!pc_gx_raw_tev_indirect_ready()) return 1;
    g_gx.raw_tev_indirect.active_tev_stage_count = count;
    g_gx.raw_tev_indirect.active_tev_stage_count_known = 1;
    return 1;
}

static int pc_gx_raw_tev_set_color(
    uint32_t id,
    const int32_t* components,
    int valid,
    PCGXTevRawSource source
) {
    if (id >= ACGC_GX_CANONICAL_TEV_REGISTER_COUNT) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    if (components == NULL) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    if (!valid) {
        if (pc_gx_raw_tev_indirect_ready()) {
            PCGXRawTevColor* shadow = &g_gx.raw_tev_indirect.registers[id];
            memcpy(shadow->components, components, sizeof(shadow->components));
            shadow->valid = 0;
            shadow->source = (uint8_t)source;
            shadow->known_mask = PC_GX_RAW_TEV_COMPONENT_KNOWN_MASK;
            shadow->reserved = 0;
        }
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    if (!pc_gx_raw_tev_indirect_ready()) return 1;
    PCGXRawTevColor* shadow = &g_gx.raw_tev_indirect.registers[id];
    memcpy(shadow->components, components, sizeof(shadow->components));
    shadow->valid = valid ? 1 : 0;
    shadow->source = (uint8_t)source;
    shadow->known_mask = PC_GX_RAW_TEV_COMPONENT_KNOWN_MASK;
    shadow->reserved = 0;
    return 1;
}

static int pc_gx_raw_tev_set_k_color(
    uint32_t id,
    const int32_t* components
) {
    if (id >= ACGC_GX_CANONICAL_TEV_KONST_COUNT) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    if (components == NULL) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    if (!pc_gx_raw_tev_indirect_ready()) return 1;
    PCGXRawTevColor* shadow = &g_gx.raw_tev_indirect.konst[id];
    memcpy(shadow->components, components, sizeof(shadow->components));
    shadow->valid = 1;
    shadow->source = PCGX_TEV_RAW_SOURCE_KCOLOR_U8;
    shadow->known_mask = PC_GX_RAW_TEV_COMPONENT_KNOWN_MASK;
    shadow->reserved = 0;
    return 1;
}

static int pc_gx_raw_tev_set_swap_table(
    uint32_t table,
    uint32_t red,
    uint32_t green,
    uint32_t blue,
    uint32_t alpha
) {
    if (table >= ACGC_GX_CANONICAL_TEV_SWAP_TABLE_COUNT ||
        red > ACGC_GX_CANONICAL_TEV_SWAP_MAX ||
        green > ACGC_GX_CANONICAL_TEV_SWAP_MAX ||
        blue > ACGC_GX_CANONICAL_TEV_SWAP_MAX ||
        alpha > ACGC_GX_CANONICAL_TEV_SWAP_MAX) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    if (!pc_gx_raw_tev_indirect_ready()) return 1;
    PCGXRawTevSwapTable* raw = &g_gx.raw_tev_indirect.swap_tables[table];
    raw->value.r = red;
    raw->value.g = green;
    raw->value.b = blue;
    raw->value.a = alpha;
    raw->known_mask = PC_GX_RAW_TEV_RECORD_KNOWN_MASK;
    return 1;
}

static int pc_gx_raw_indirect_set_num_stages(uint32_t count) {
    if (count > ACGC_GX_CANONICAL_INDIRECT_ACTIVE_STAGE_COUNT_MAX) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    if (!pc_gx_raw_tev_indirect_ready()) return 1;
    g_gx.raw_tev_indirect.active_indirect_stage_count = count;
    g_gx.raw_tev_indirect.active_indirect_stage_count_known = 1;
    return 1;
}

static int pc_gx_raw_indirect_set_order(
    uint32_t stage,
    uint32_t tex_coord,
    uint32_t tex_map
) {
    if (stage >= ACGC_GX_CANONICAL_INDIRECT_ORDER_CAPACITY ||
        tex_coord > ACGC_GX_CANONICAL_INDIRECT_TEXCOORD_MAX ||
        tex_map > ACGC_GX_CANONICAL_INDIRECT_TEXMAP_MAX) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    if (!pc_gx_raw_tev_indirect_ready()) return 1;
    PCGXRawIndirectOrder* raw = &g_gx.raw_tev_indirect.orders[stage];
    raw->value.tex_coord = tex_coord;
    raw->value.tex_map = tex_map;
    raw->known_mask |= UINT32_C(1) << 0;
    raw->known_mask |= UINT32_C(1) << 1;
    raw->value.reserved[0] = 0;
    raw->value.reserved[1] = 0;
    return 1;
}

static int pc_gx_raw_indirect_set_scale(
    uint32_t stage,
    uint32_t scale_s,
    uint32_t scale_t
) {
    if (stage >= ACGC_GX_CANONICAL_INDIRECT_ORDER_CAPACITY ||
        scale_s > ACGC_GX_CANONICAL_INDIRECT_SCALE_MAX ||
        scale_t > ACGC_GX_CANONICAL_INDIRECT_SCALE_MAX) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    if (!pc_gx_raw_tev_indirect_ready()) return 1;
    PCGXRawIndirectOrder* raw = &g_gx.raw_tev_indirect.orders[stage];
    raw->value.scale_s = scale_s;
    raw->value.scale_t = scale_t;
    raw->known_mask |= UINT32_C(1) << 2;
    raw->known_mask |= UINT32_C(1) << 3;
    raw->value.reserved[0] = 0;
    raw->value.reserved[1] = 0;
    return 1;
}

static int pc_gx_raw_indirect_matrix_id(uint32_t matrix) {
    switch (matrix) {
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_0:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_1:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_2:
            return (int)matrix - 1;
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_S0:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_S1:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_S2:
            return (int)matrix - 5;
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_T0:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_T1:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_T2:
            return (int)matrix - 9;
        default:
            return -1;
    }
}

static int pc_gx_raw_indirect_set_matrix(
    uint32_t matrix,
    const void* offset,
    s8 scale
) {
    int id;
    float copied[6];
    float scaled[6];
    int32_t quantized[6];
    int index;

    id = pc_gx_raw_indirect_matrix_id(matrix);
    if (id < 0 || offset == NULL) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    memcpy(copied, offset, sizeof(copied));
    for (index = 0; index < 6; index++) {
        scaled[index] = copied[index] * 1024.0f;
        /* Match the guest truncation only where the C conversion is defined. */
        if (!isfinite(copied[index]) || !isfinite(scaled[index]) ||
            scaled[index] < -2147483648.0f ||
            scaled[index] >= 2147483648.0f) {
            pc_gx_raw_tev_indirect_mark_invalid();
            return 0;
        }
        quantized[index] = (int32_t)scaled[index] & INT32_C(0x7FF);
        if (quantized[index] >= 1024) quantized[index] -= 2048;
    }

    if (!pc_gx_raw_tev_indirect_ready()) return 1;

    PCGXRawIndirectMatrix* raw = &g_gx.raw_tev_indirect.matrices[id];
    raw->value.s0 = quantized[0];
    raw->value.t0 = quantized[3];
    raw->value.s1 = quantized[1];
    raw->value.t1 = quantized[4];
    raw->value.s2 = quantized[2];
    raw->value.t2 = quantized[5];
    raw->value.encoded_scale = ((int32_t)scale + 0x11) & 0x3F;
    raw->value.reserved = 0;
    raw->known_mask = PC_GX_RAW_INDIRECT_MATRIX_KNOWN_MASK;
    return 1;
}

/* GXSetTevOp's guest expansion is not identical to the PC compatibility
 * expansion below it: later stages consume CPREV/APREV, and GX_BLEND orders
 * the color inputs as previous, one, texture.  Capture that logical guest
 * expansion after the legacy host setters have preserved their behavior. */
static int pc_gx_raw_tev_set_op(uint32_t stage, uint32_t mode) {
    uint32_t color_previous;
    uint32_t alpha_previous;

    if (stage >= ACGC_GX_CANONICAL_TEV_STAGE_COUNT) {
        pc_gx_raw_tev_indirect_mark_invalid();
        return 0;
    }
    color_previous = stage == 0 ? GX_CC_RASC : GX_CC_CPREV;
    alpha_previous = stage == 0 ? GX_CA_RASA : GX_CA_APREV;

    switch (mode) {
        case GX_MODULATE:
            if (!pc_gx_raw_tev_stage_set_color_in(
                stage, GX_CC_ZERO, GX_CC_TEXC, color_previous, GX_CC_ZERO
            ) || !pc_gx_raw_tev_stage_set_alpha_in(
                stage, GX_CA_ZERO, GX_CA_TEXA, alpha_previous, GX_CA_ZERO
            )) return 0;
            break;
        case GX_DECAL:
            if (!pc_gx_raw_tev_stage_set_color_in(
                stage, color_previous, GX_CC_TEXC, GX_CC_TEXA, GX_CC_ZERO
            ) || !pc_gx_raw_tev_stage_set_alpha_in(
                stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, alpha_previous
            )) return 0;
            break;
        case GX_BLEND:
            if (!pc_gx_raw_tev_stage_set_color_in(
                stage, color_previous, GX_CC_ONE, GX_CC_TEXC, GX_CC_ZERO
            ) || !pc_gx_raw_tev_stage_set_alpha_in(
                stage, GX_CA_ZERO, GX_CA_TEXA, alpha_previous, GX_CA_ZERO
            )) return 0;
            break;
        case GX_REPLACE:
            if (!pc_gx_raw_tev_stage_set_color_in(
                stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC
            ) || !pc_gx_raw_tev_stage_set_alpha_in(
                stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA
            )) return 0;
            break;
        case GX_PASSCLR:
            if (!pc_gx_raw_tev_stage_set_color_in(
                stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, color_previous
            ) || !pc_gx_raw_tev_stage_set_alpha_in(
                stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, alpha_previous
            )) return 0;
            break;
        default:
            pc_gx_raw_tev_indirect_mark_invalid();
            return 0;
    }
    if (!pc_gx_raw_tev_stage_set_color_op(
        stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV
    )) return 0;
    if (!pc_gx_raw_tev_stage_set_alpha_op(
        stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV
    )) return 0;
    return 1;
}

static void pc_gx_raw_alpha_mark_invalid(void) {
    g_gx.raw_alpha.invalid = 1;
}

static void pc_gx_raw_alpha_store_compare(
    uint32_t comp0,
    uint32_t ref0,
    uint32_t op,
    uint32_t comp1,
    uint32_t ref1
) {
    PCGXRawAlpha* shadow = &g_gx.raw_alpha;

    if (shadow->invalid != 0) return;
    if (comp0 > ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX ||
        ref0 > ACGC_GX_CANONICAL_ALPHA_REFERENCE_MAX ||
        op > ACGC_GX_CANONICAL_ALPHA_OPERATOR_MAX ||
        comp1 > ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX ||
        ref1 > ACGC_GX_CANONICAL_ALPHA_REFERENCE_MAX) {
        pc_gx_raw_alpha_mark_invalid();
        return;
    }

    shadow->value.comp0 = comp0;
    shadow->value.ref0 = ref0;
    shadow->value.op = op;
    shadow->value.comp1 = comp1;
    shadow->value.ref1 = ref1;
    shadow->known_mask |=
        PC_GX_RAW_ALPHA_KNOWN_COMP0 |
        PC_GX_RAW_ALPHA_KNOWN_REF0 |
        PC_GX_RAW_ALPHA_KNOWN_OP |
        PC_GX_RAW_ALPHA_KNOWN_COMP1 |
        PC_GX_RAW_ALPHA_KNOWN_REF1;
}

static void pc_gx_raw_alpha_store_boolean(
    uint32_t value,
    uint32_t known_bit,
    uint32_t* destination
) {
    PCGXRawAlpha* shadow = &g_gx.raw_alpha;

    if (shadow->invalid != 0) return;
    *destination = value != 0 ? 1u : 0u;
    shadow->known_mask |= known_bit;
}

#ifdef PC_GX_ALPHA_RAW_PRODUCER
int pc_gx_raw_alpha_build_canonical(
    AcgcGxCanonicalAlphaState* destination
) {
    const PCGXRawAlpha* shadow = &g_gx.raw_alpha;
    AcgcGxCanonicalAlphaState candidate;

    if (destination == NULL || shadow->invalid != 0 ||
        shadow->known_mask != PC_GX_RAW_ALPHA_KNOWN_ALL) {
        return 0;
    }

    candidate = shadow->value;
    if (!acgc_gx_canonical_alpha_state_validate(&candidate)) return 0;
    *destination = candidate;
    return 1;
}
#endif

static void pc_gx_raw_raster_mark_invalid(void) {
    g_gx.raw_raster.invalid = 1;
}

static uint32_t pc_gx_raw_raster_float_bits(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int pc_gx_raw_raster_float_is_finite(uint32_t bits) {
    return (bits & UINT32_C(0x7F800000)) != UINT32_C(0x7F800000);
}

static void pc_gx_raw_raster_store_viewport(
    f32 left,
    f32 top,
    f32 width,
    f32 height,
    f32 nearz,
    f32 farz
) {
    PCGXRawRaster* shadow = &g_gx.raw_raster;
    const float values[ACGC_GX_CANONICAL_RASTER_VIEWPORT_WORD_COUNT] = {
        left, top, width, height, nearz, farz
    };
    uint32_t bits[ACGC_GX_CANONICAL_RASTER_VIEWPORT_WORD_COUNT];
    uint32_t index;

    if (shadow->invalid != 0) return;
    for (index = 0;
         index < ACGC_GX_CANONICAL_RASTER_VIEWPORT_WORD_COUNT;
         index++) {
        bits[index] = pc_gx_raw_raster_float_bits(values[index]);
        if (!pc_gx_raw_raster_float_is_finite(bits[index])) {
            pc_gx_raw_raster_mark_invalid();
            return;
        }
    }

    memcpy(shadow->value.viewport_bits, bits, sizeof(bits));
    shadow->known_mask |=
        PC_GX_RAW_RASTER_KNOWN_VIEWPORT_LEFT |
        PC_GX_RAW_RASTER_KNOWN_VIEWPORT_TOP |
        PC_GX_RAW_RASTER_KNOWN_VIEWPORT_WIDTH |
        PC_GX_RAW_RASTER_KNOWN_VIEWPORT_HEIGHT |
        PC_GX_RAW_RASTER_KNOWN_VIEWPORT_NEAR |
        PC_GX_RAW_RASTER_KNOWN_VIEWPORT_FAR;
}

static int pc_gx_raw_raster_scissor_is_valid(
    uint32_t left,
    uint32_t top,
    uint32_t width,
    uint32_t height
) {
    return left < ACGC_GX_CANONICAL_RASTER_SCISSOR_LIMIT &&
        top < ACGC_GX_CANONICAL_RASTER_SCISSOR_LIMIT &&
        width < ACGC_GX_CANONICAL_RASTER_SCISSOR_LIMIT - left &&
        height < ACGC_GX_CANONICAL_RASTER_SCISSOR_LIMIT - top;
}

static void pc_gx_raw_raster_store_scissor(
    uint32_t left,
    uint32_t top,
    uint32_t width,
    uint32_t height
) {
    PCGXRawRaster* shadow = &g_gx.raw_raster;

    if (shadow->invalid != 0) return;
    if (!pc_gx_raw_raster_scissor_is_valid(left, top, width, height)) {
        pc_gx_raw_raster_mark_invalid();
        return;
    }

    shadow->value.scissor[0] = left;
    shadow->value.scissor[1] = top;
    shadow->value.scissor[2] = width;
    shadow->value.scissor[3] = height;
    shadow->known_mask |=
        PC_GX_RAW_RASTER_KNOWN_SCISSOR_LEFT |
        PC_GX_RAW_RASTER_KNOWN_SCISSOR_TOP |
        PC_GX_RAW_RASTER_KNOWN_SCISSOR_WIDTH |
        PC_GX_RAW_RASTER_KNOWN_SCISSOR_HEIGHT;
}

static void pc_gx_raw_raster_store_scissor_offset(s32 x, s32 y) {
    PCGXRawRaster* shadow = &g_gx.raw_raster;

    if (shadow->invalid != 0) return;
    if (x < ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET_MIN ||
        x > ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET_MAX ||
        y < ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET_MIN ||
        y > ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET_MAX) {
        pc_gx_raw_raster_mark_invalid();
        return;
    }

    shadow->value.scissor_offset[0] = x;
    shadow->value.scissor_offset[1] = y;
    shadow->known_mask |=
        PC_GX_RAW_RASTER_KNOWN_SCISSOR_OFFSET_X |
        PC_GX_RAW_RASTER_KNOWN_SCISSOR_OFFSET_Y;
}

static void pc_gx_raw_raster_store_bounded(
    uint32_t value,
    uint32_t minimum,
    uint32_t maximum,
    uint32_t known_bit,
    uint32_t* destination
) {
    PCGXRawRaster* shadow = &g_gx.raw_raster;

    if (shadow->invalid != 0) return;
    if (value < minimum || value > maximum) {
        pc_gx_raw_raster_mark_invalid();
        return;
    }

    *destination = value;
    shadow->known_mask |= known_bit;
}

static void pc_gx_raw_raster_store_size_and_tex_offset(
    uint32_t size,
    uint32_t tex_offset,
    uint32_t size_known_bit,
    uint32_t tex_offset_known_bit,
    uint32_t* size_destination,
    uint32_t* tex_offset_destination
) {
    PCGXRawRaster* shadow = &g_gx.raw_raster;

    if (shadow->invalid != 0) return;
    if (size > ACGC_GX_CANONICAL_RASTER_SIZE_MAX ||
        tex_offset > ACGC_GX_CANONICAL_RASTER_TEX_OFFSET_MAX) {
        pc_gx_raw_raster_mark_invalid();
        return;
    }

    *size_destination = size;
    *tex_offset_destination = tex_offset;
    shadow->known_mask |= size_known_bit | tex_offset_known_bit;
}

static void pc_gx_raw_raster_store_dst_alpha(
    GXBool enable,
    uint32_t alpha
) {
    PCGXRawRaster* shadow = &g_gx.raw_raster;
    const uint32_t enable_value = (uint32_t)enable;

    if (shadow->invalid != 0) return;
    if (enable_value > ACGC_GX_CANONICAL_RASTER_BOOLEAN_MAX ||
        alpha > ACGC_GX_CANONICAL_RASTER_DST_ALPHA_MAX) {
        pc_gx_raw_raster_mark_invalid();
        return;
    }

    shadow->value.dst_alpha_enable = enable_value;
    shadow->value.dst_alpha = alpha;
    shadow->known_mask |=
        PC_GX_RAW_RASTER_KNOWN_DST_ALPHA_ENABLE |
        PC_GX_RAW_RASTER_KNOWN_DST_ALPHA;
}

static void pc_gx_raw_raster_store_field_mask(
    GXBool odd,
    GXBool even
) {
    PCGXRawRaster* shadow = &g_gx.raw_raster;
    const uint32_t odd_value = (uint32_t)odd;
    const uint32_t even_value = (uint32_t)even;

    if (shadow->invalid != 0) return;
    if (odd_value > ACGC_GX_CANONICAL_RASTER_BOOLEAN_MAX ||
        even_value > ACGC_GX_CANONICAL_RASTER_BOOLEAN_MAX) {
        pc_gx_raw_raster_mark_invalid();
        return;
    }

    shadow->value.field_odd_mask = odd_value;
    shadow->value.field_even_mask = even_value;
    shadow->known_mask |=
        PC_GX_RAW_RASTER_KNOWN_FIELD_ODD_MASK |
        PC_GX_RAW_RASTER_KNOWN_FIELD_EVEN_MASK;
}

static void pc_gx_raw_raster_store_field_mode(
    GXBool field_mode,
    GXBool half_aspect
) {
    PCGXRawRaster* shadow = &g_gx.raw_raster;
    const uint32_t field_value = (uint32_t)field_mode;
    const uint32_t half_value = (uint32_t)half_aspect;

    if (shadow->invalid != 0) return;
    if (field_value > ACGC_GX_CANONICAL_RASTER_BOOLEAN_MAX ||
        half_value > ACGC_GX_CANONICAL_RASTER_BOOLEAN_MAX) {
        pc_gx_raw_raster_mark_invalid();
        return;
    }

    shadow->value.field_mode = field_value;
    shadow->value.half_aspect_ratio = half_value;
    shadow->known_mask |=
        PC_GX_RAW_RASTER_KNOWN_FIELD_MODE |
        PC_GX_RAW_RASTER_KNOWN_HALF_ASPECT;
}

static void pc_gx_raw_raster_store_texcoord_offsets(
    uint32_t coord,
    GXBool line,
    GXBool point
) {
    PCGXRawRaster* shadow = &g_gx.raw_raster;
    const uint32_t line_value = (uint32_t)line;
    const uint32_t point_value = (uint32_t)point;
    uint32_t bit;

    if (shadow->invalid != 0) return;
    if (coord >= 8 ||
        line_value > ACGC_GX_CANONICAL_RASTER_BOOLEAN_MAX ||
        point_value > ACGC_GX_CANONICAL_RASTER_BOOLEAN_MAX) {
        pc_gx_raw_raster_mark_invalid();
        return;
    }
    bit = UINT32_C(1) << coord;

    if (line_value != 0) shadow->value.line_texcoord_mask |= bit;
    else shadow->value.line_texcoord_mask &= ~bit;
    if (point_value != 0) shadow->value.point_texcoord_mask |= bit;
    else shadow->value.point_texcoord_mask &= ~bit;
    shadow->line_texcoord_known_mask |= bit;
    shadow->point_texcoord_known_mask |= bit;
    if (shadow->line_texcoord_known_mask ==
            PC_GX_RAW_RASTER_TEXCOORD_KNOWN_ALL) {
        shadow->known_mask |= PC_GX_RAW_RASTER_KNOWN_LINE_TEXCOORD_MASK;
    }
    if (shadow->point_texcoord_known_mask ==
            PC_GX_RAW_RASTER_TEXCOORD_KNOWN_ALL) {
        shadow->known_mask |= PC_GX_RAW_RASTER_KNOWN_POINT_TEXCOORD_MASK;
    }
}

#ifdef PC_GX_RASTER_RAW_PRODUCER
int pc_gx_raw_raster_build_canonical(
    AcgcGxCanonicalRasterState* destination
) {
    const PCGXRawRaster* shadow = &g_gx.raw_raster;
    AcgcGxCanonicalRasterState candidate;

    if (destination == NULL || shadow->invalid != 0 ||
        shadow->known_mask != PC_GX_RAW_RASTER_KNOWN_ALL ||
        shadow->line_texcoord_known_mask !=
            PC_GX_RAW_RASTER_TEXCOORD_KNOWN_ALL ||
        shadow->point_texcoord_known_mask !=
            PC_GX_RAW_RASTER_TEXCOORD_KNOWN_ALL) {
        return 0;
    }

    candidate = shadow->value;
    if (!acgc_gx_canonical_raster_state_validate(&candidate)) return 0;
    *destination = candidate;
    return 1;
}
#endif

static int pc_gx_raw_depth_bool_is_valid(uint32_t value) {
    return value == (uint32_t)GX_FALSE || value == (uint32_t)GX_TRUE;
}

static int pc_gx_raw_depth_compare_func_is_valid(uint32_t value) {
    return value >= (uint32_t)GX_NEVER && value <= (uint32_t)GX_ALWAYS;
}

static void pc_gx_raw_depth_store(
    uint32_t compare_enable,
    uint32_t compare_func,
    uint32_t update_enable
) {
    PCGXRawDepth* shadow = &g_gx.raw_depth;

    if (!pc_gx_raw_depth_bool_is_valid(compare_enable) ||
        !pc_gx_raw_depth_compare_func_is_valid(compare_func) ||
        !pc_gx_raw_depth_bool_is_valid(update_enable)) {
        memset(shadow, 0, sizeof(*shadow));
        return;
    }

    shadow->compare_enable = compare_enable;
    shadow->compare_func = compare_func;
    shadow->update_enable = update_enable;
    shadow->known = 1;
    memset(shadow->reserved, 0, sizeof(shadow->reserved));
}

static void pc_gx_raw_blend_mark_invalid(void) {
    g_gx.raw_blend.invalid = 1;
}

static void pc_gx_raw_blend_store(
    uint32_t mode,
    uint32_t source_factor,
    uint32_t destination_factor,
    uint32_t logic_op
) {
    PCGXRawBlend* shadow = &g_gx.raw_blend;

    /* A malformed setter epoch cannot be repaired by a later setter. Keep the
     * last valid value for diagnostics, but make the producer fail closed. */
    if (shadow->invalid != 0) return;
    if (mode < ACGC_GX_CANONICAL_BLEND_MODE_MIN ||
        mode > ACGC_GX_CANONICAL_BLEND_MODE_MAX ||
        source_factor < ACGC_GX_CANONICAL_BLEND_FACTOR_MIN ||
        source_factor > ACGC_GX_CANONICAL_BLEND_FACTOR_MAX ||
        destination_factor < ACGC_GX_CANONICAL_BLEND_FACTOR_MIN ||
        destination_factor > ACGC_GX_CANONICAL_BLEND_FACTOR_MAX ||
        logic_op < ACGC_GX_CANONICAL_BLEND_LOGIC_OP_MIN ||
        logic_op > ACGC_GX_CANONICAL_BLEND_LOGIC_OP_MAX) {
        pc_gx_raw_blend_mark_invalid();
        return;
    }

    shadow->value.mode = mode;
    shadow->value.source_factor = source_factor;
    shadow->value.destination_factor = destination_factor;
    shadow->value.logic_op = logic_op;
    shadow->known = 1;
    memset(shadow->reserved, 0, sizeof(shadow->reserved));
}

static void pc_gx_raw_fog_mark_invalid(void) {
    g_gx.raw_fog.invalid = 1;
}

static uint32_t pc_gx_raw_fog_float_bits(f32 value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint32_t pc_gx_raw_fog_color_rgba8(GXColor color) {
    return (uint32_t)color.r |
        ((uint32_t)color.g << 8) |
        ((uint32_t)color.b << 16) |
        ((uint32_t)color.a << 24);
}

static int pc_gx_raw_fog_type_is_valid(uint32_t fog_type) {
    switch (fog_type) {
        case ACGC_GX_CANONICAL_FOG_TYPE_NONE:
        case ACGC_GX_CANONICAL_FOG_TYPE_PERSP_LIN:
        case ACGC_GX_CANONICAL_FOG_TYPE_PERSP_EXP:
        case ACGC_GX_CANONICAL_FOG_TYPE_PERSP_EXP2:
        case ACGC_GX_CANONICAL_FOG_TYPE_PERSP_REVEXP:
        case ACGC_GX_CANONICAL_FOG_TYPE_PERSP_REVEXP2:
        case ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_LIN:
        case ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_EXP:
        case ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_EXP2:
        case ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_REVEXP:
        case ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_REVEXP2:
            return 1;
        default:
            return 0;
    }
}

static int pc_gx_raw_fog_float_is_finite(uint32_t bits) {
    return (bits & UINT32_C(0x7F800000)) != UINT32_C(0x7F800000);
}

static int pc_gx_raw_fog_binary32_less(uint32_t lhs, uint32_t rhs) {
    const uint32_t lhs_magnitude = lhs & UINT32_C(0x7FFFFFFF);
    const uint32_t rhs_magnitude = rhs & UINT32_C(0x7FFFFFFF);
    const int lhs_negative = (lhs & UINT32_C(0x80000000)) != 0;
    const int rhs_negative = (rhs & UINT32_C(0x80000000)) != 0;

    if (lhs_magnitude == 0 && rhs_magnitude == 0) return 0;
    if (lhs_negative != rhs_negative) return lhs_negative;
    if (lhs_negative) return lhs_magnitude > rhs_magnitude;
    return lhs_magnitude < rhs_magnitude;
}

static int pc_gx_raw_fog_values_are_valid(
    uint32_t fog_type,
    uint32_t start_bits,
    uint32_t end_bits,
    uint32_t near_bits,
    uint32_t far_bits
) {
    const int fog_is_active = fog_type != ACGC_GX_CANONICAL_FOG_TYPE_NONE;
    const int parameters_are_finite =
        pc_gx_raw_fog_float_is_finite(start_bits) &&
        pc_gx_raw_fog_float_is_finite(end_bits) &&
        pc_gx_raw_fog_float_is_finite(near_bits) &&
        pc_gx_raw_fog_float_is_finite(far_bits);

    if (!pc_gx_raw_fog_type_is_valid(fog_type) ||
        (fog_is_active && !parameters_are_finite)) {
        return 0;
    }
    if (pc_gx_raw_fog_float_is_finite(far_bits) &&
        pc_gx_raw_fog_binary32_less(far_bits, 0)) {
        return 0;
    }
    if (pc_gx_raw_fog_float_is_finite(far_bits) &&
        pc_gx_raw_fog_float_is_finite(near_bits) &&
        pc_gx_raw_fog_binary32_less(far_bits, near_bits)) {
        return 0;
    }
    return 1;
}

static int pc_gx_raw_fog_range_values_are_valid(
    uint32_t enable,
    uint32_t center,
    const u16* entries
) {
    uint32_t index;

    if (enable > 1 || center > ACGC_GX_CANONICAL_FOG_CENTER_SOURCE_MAX) {
        return 0;
    }
    if (enable == 0) return 1;
    if (center > ACGC_GX_CANONICAL_FOG_CENTER_MAX || entries == NULL) {
        return 0;
    }
    for (index = 0; index < ACGC_GX_CANONICAL_FOG_RANGE_COUNT; index++) {
        if (entries[index] > ACGC_GX_CANONICAL_FOG_RANGE_VALUE_MASK) {
            return 0;
        }
    }
    return 1;
}

static void pc_gx_raw_fog_store(
    u32 type,
    f32 startz,
    f32 endz,
    f32 nearz,
    f32 farz,
    GXColor color
) {
    PCGXRawFog* shadow = &g_gx.raw_fog;
    AcgcGxCanonicalFogState candidate;
    const uint32_t start_bits = pc_gx_raw_fog_float_bits(startz);
    const uint32_t end_bits = pc_gx_raw_fog_float_bits(endz);
    const uint32_t near_bits = pc_gx_raw_fog_float_bits(nearz);
    const uint32_t far_bits = pc_gx_raw_fog_float_bits(farz);

    if (shadow->invalid != 0) return;
    if (!pc_gx_raw_fog_values_are_valid(
        type, start_bits, end_bits, near_bits, far_bits
    )) {
        pc_gx_raw_fog_mark_invalid();
        return;
    }

    candidate = shadow->value;
    candidate.fog_type = type;
    candidate.start_bits = start_bits;
    candidate.end_bits = end_bits;
    candidate.near_bits = near_bits;
    candidate.far_bits = far_bits;
    candidate.color_rgba8 = pc_gx_raw_fog_color_rgba8(color);
    memset(candidate.reserved, 0, sizeof(candidate.reserved));

    shadow->value = candidate;
    shadow->known_mask |= PC_GX_RAW_FOG_KNOWN_FOG;
}

static void pc_gx_raw_fog_store_range(
    uint32_t enable,
    u16 center,
    const void* table
) {
    PCGXRawFog* shadow = &g_gx.raw_fog;
    AcgcGxCanonicalFogState candidate;
    u16 entries[ACGC_GX_CANONICAL_FOG_RANGE_COUNT];
    const uint32_t range_enable = enable;
    uint32_t index;

    if (shadow->invalid != 0) return;
    if (range_enable > 1) {
        pc_gx_raw_fog_mark_invalid();
        return;
    }
    if (range_enable != 0) {
        if (table == NULL) {
            pc_gx_raw_fog_mark_invalid();
            return;
        }
        /* The public PC ABI remains void*. Copy through bytes before any
         * u16 access so unaligned caller buffers are safe and deterministic. */
        memcpy(entries, table, sizeof(entries));
    }
    if (!pc_gx_raw_fog_range_values_are_valid(
        range_enable,
        (uint32_t)center,
        range_enable != 0 ? entries : NULL
    )) {
        pc_gx_raw_fog_mark_invalid();
        return;
    }

    candidate = shadow->value;
    candidate.range_adjust_enable = range_enable;
    candidate.range_center = (uint32_t)center;
    if (range_enable != 0) {
        for (index = 0;
             index < ACGC_GX_CANONICAL_FOG_RANGE_COUNT;
             index++) {
            candidate.range_adjust[index] = entries[index];
        }
    }
    memset(candidate.reserved, 0, sizeof(candidate.reserved));

    shadow->value = candidate;
    shadow->known_mask |= PC_GX_RAW_FOG_KNOWN_RANGE_ADJUST;
}

/* Mirror the decomp CHECK_GXBEGIN boundary at the existing PC flush seam.
 * A completed batch is committed first; an incomplete begin remains active.
 * Callers reject the operation before any raw/host/table mutation. */
static int pc_gx_raw_fog_flush_leaves_incomplete_begin(void) {
    pc_gx_flush_if_begin_complete();
    return g_gx.in_begin != 0;
}

#ifdef PC_GX_FOG_PRODUCER_FIXTURE
/* TARGET_PC defines GXBool as C bool, so the fixture uses this narrow seam to
 * exercise the underlying exact GX domain value 2 without changing the
 * public ABI or any production caller. */
void pc_gx_raw_fog_range_fixture(
    uint32_t enable,
    u16 center,
    const void* table
) {
    if (pc_gx_raw_fog_flush_leaves_incomplete_begin()) return;
    pc_gx_raw_fog_store_range(enable, center, table);
}
#endif

static int pc_gx_raw_fog_adj_u32_to_range_value(
    f32 value,
    u16* destination
) {
    uint32_t integer_value;

    /* A u32 conversion is undefined outside [0, 2^32), so reject before
     * the cast and retain the caller buffer's previous contents. */
    if (destination == NULL || !isfinite(value) || value < 0.0f ||
        value >= 4294967296.0f) {
        return 0;
    }
    integer_value = (uint32_t)value;
    *destination = (u16)(integer_value &
        ACGC_GX_CANONICAL_FOG_RANGE_VALUE_MASK);
    return 1;
}

/* Match GXInitFogAdjTable's ten-entry decomp formula without retaining a
 * caller pointer. All arithmetic completes in local storage before the
 * existing completed-batch flush boundary and final caller-buffer copy. */
static int pc_gx_raw_fog_init_adj_table(
    void* table,
    u16 width,
    f32 projmtx[4][4]
) {
    f32 near_z;
    f32 side_x;
    f32 inverse_width;
    f32 near_z_squared;
    u16 generated[ACGC_GX_CANONICAL_FOG_RANGE_COUNT];
    uint32_t index;

    if (table == NULL || projmtx == NULL || width == 0 || width > 640) {
        return 0;
    }
    if (!isfinite(projmtx[3][3])) return 0;

    if (projmtx[3][3] == 0.0f) {
        const f32 denominator = projmtx[2][2] - 1.0f;
        const f32 numerator = projmtx[2][3];
        f32 side_term;

        if (!isfinite(projmtx[2][2]) || !isfinite(numerator) ||
            !isfinite(projmtx[0][2]) || !isfinite(projmtx[0][0]) ||
            projmtx[0][0] == 0.0f || denominator == 0.0f ||
            !isfinite(denominator)) {
            return 0;
        }
        near_z = numerator / denominator;
        side_term = 1.0f + projmtx[0][2];
        if (!isfinite(near_z) || !isfinite(side_term)) return 0;
        side_term *= near_z;
        if (!isfinite(side_term)) return 0;
        side_x = side_term / projmtx[0][0];
    } else {
        const f32 denominator = projmtx[2][2];
        f32 numerator;
        f32 side_numerator;

        if (!isfinite(projmtx[2][2]) || !isfinite(projmtx[2][3]) ||
            !isfinite(projmtx[0][3]) || !isfinite(projmtx[0][0]) ||
            projmtx[0][0] == 0.0f || denominator == 0.0f ||
            !isfinite(denominator)) {
            return 0;
        }
        numerator = 1.0f + projmtx[2][3];
        side_numerator = -(projmtx[0][3] - 1.0f);
        if (!isfinite(numerator) || !isfinite(side_numerator)) return 0;
        near_z = numerator / denominator;
        side_x = side_numerator / projmtx[0][0];
    }
    if (!isfinite(near_z) || near_z == 0.0f || !isfinite(side_x)) {
        return 0;
    }

    inverse_width = 2.0f / width;
    near_z_squared = near_z * near_z;
    if (!isfinite(inverse_width) || !isfinite(near_z_squared) ||
        near_z_squared == 0.0f) {
        return 0;
    }

    for (index = 0; index < ACGC_GX_CANONICAL_FOG_RANGE_COUNT; index++) {
        f32 xi = (f32)((index + 1) << 5);
        f32 xi_squared;
        f32 ratio;
        f32 range_value;
        f32 scaled_value;

        xi *= inverse_width;
        if (!isfinite(xi)) return 0;
        xi *= side_x;
        if (!isfinite(xi)) return 0;
        xi_squared = xi * xi;
        if (!isfinite(xi_squared)) return 0;
        ratio = xi_squared / near_z_squared;
        if (!isfinite(ratio) || ratio < 0.0f) return 0;
        ratio += 1.0f;
        if (!isfinite(ratio) || ratio < 0.0f) return 0;
        range_value = sqrtf(ratio);
        if (!isfinite(range_value)) return 0;
        scaled_value = 256.0f * range_value;
        if (!pc_gx_raw_fog_adj_u32_to_range_value(
            scaled_value, &generated[index]
        )) {
            return 0;
        }
    }

    if (pc_gx_raw_fog_flush_leaves_incomplete_begin()) return 0;
    memcpy(table, generated, sizeof(generated));
    return 1;
}

static int pc_gx_raw_texgen_ordinary_slot(uint32_t id) {
    if (id == (uint32_t)GX_IDENTITY) {
        return PC_GX_TEXGEN_ORDINARY_MATRIX_COUNT - 1;
    }
    if (id >= (uint32_t)GX_TEXMTX0 && id <= (uint32_t)GX_TEXMTX9 &&
        ((id - (uint32_t)GX_TEXMTX0) % 3u) == 0u) {
        return (int)((id - (uint32_t)GX_TEXMTX0) / 3u);
    }
    return -1;
}

static int pc_gx_raw_texgen_post_slot(uint32_t id) {
    if (id == (uint32_t)GX_PTIDENTITY) {
        return PC_GX_TEXGEN_POST_MATRIX_COUNT - 1;
    }
    if (id >= (uint32_t)GX_PTTEXMTX0 && id <= (uint32_t)GX_PTTEXMTX19 &&
        ((id - (uint32_t)GX_PTTEXMTX0) % 3u) == 0u) {
        return (int)((id - (uint32_t)GX_PTTEXMTX0) / 3u);
    }
    return -1;
}

static int pc_gx_raw_texgen_matrix_type_is_valid(
    int post,
    uint32_t type
) {
    if (post) {
        return type == (uint32_t)GX_MTX3x4;
    }
    return type == (uint32_t)GX_MTX2x4 ||
        type == (uint32_t)GX_MTX3x4;
}

static uint32_t pc_gx_raw_texgen_matrix_word_count(uint32_t type) {
    return type == (uint32_t)GX_MTX2x4 ? 8u : 12u;
}

static uint32_t pc_gx_raw_texgen_matrix_word_mask(uint32_t count) {
    return count == 8u ? UINT32_C(0x000000FF) : UINT32_C(0x00000FFF);
}

static void pc_gx_raw_texgen_initialize_matrix_ids(void) {
    int index;

    for (index = 0; index < PC_GX_TEXGEN_ORDINARY_MATRIX_COUNT; index++) {
        g_gx.raw_texgen.ordinary[index].logical_id = (uint32_t)(
            index < 10 ? GX_TEXMTX0 + index * 3 : GX_IDENTITY
        );
    }
    for (index = 0; index < PC_GX_TEXGEN_POST_MATRIX_COUNT; index++) {
        g_gx.raw_texgen.post[index].logical_id = (uint32_t)(
            index < 20 ? GX_PTTEXMTX0 + index * 3 : GX_PTIDENTITY
        );
    }
}

static void pc_gx_raw_texgen_mark_invalid(void) {
    g_gx.raw_texgen.invalid = 1;
}

static void pc_gx_raw_texgen_clear_record(uint32_t index) {
    if (index < PC_GX_TEXGEN_COUNT) {
        memset(&g_gx.raw_texgen.texgen[index], 0,
               sizeof(g_gx.raw_texgen.texgen[index]));
    }
}

static int pc_gx_raw_texgen_function_is_regular(uint32_t function) {
    return function == (uint32_t)GX_TG_MTX2x4 ||
        function == (uint32_t)GX_TG_MTX3x4;
}

static int pc_gx_raw_texgen_regular_source_is_valid(uint32_t source) {
    return source <= (uint32_t)GX_TG_TANGENT ||
        (source >= (uint32_t)GX_TG_TEX0 &&
         source <= (uint32_t)GX_TG_TEX7) ||
        source == (uint32_t)GX_TG_COLOR0 ||
        source == (uint32_t)GX_TG_COLOR1;
}

static int pc_gx_raw_texgen_record_values_are_valid(
    uint32_t function,
    uint32_t source
) {
    if (pc_gx_raw_texgen_function_is_regular(function)) {
        return pc_gx_raw_texgen_regular_source_is_valid(source);
    }
    if (function >= (uint32_t)GX_TG_BUMP0 &&
        function <= (uint32_t)GX_TG_BUMP7) {
        return source >= (uint32_t)GX_TG_TEXCOORD0 &&
            source <= (uint32_t)GX_TG_TEXCOORD6;
    }
    if (function == (uint32_t)GX_TG_SRTG) {
        return source == (uint32_t)GX_TG_COLOR0 ||
            source == (uint32_t)GX_TG_COLOR1;
    }
    return 0;
}

static void pc_gx_raw_texgen_store(
    uint32_t dst,
    uint32_t function,
    uint32_t source,
    uint32_t ordinary_matrix_id,
    uint32_t normalize,
    uint32_t post_matrix_id
) {
    PCGXRawTexgenRecord* record;
    int valid;

    valid = dst < PC_GX_TEXGEN_COUNT &&
        pc_gx_raw_texgen_record_values_are_valid(function, source) &&
        pc_gx_raw_texgen_ordinary_slot(ordinary_matrix_id) >= 0 &&
        pc_gx_raw_texgen_post_slot(post_matrix_id) >= 0 &&
        (normalize == (uint32_t)GX_FALSE ||
         normalize == (uint32_t)GX_TRUE);
    if (!valid) {
        if (dst < PC_GX_TEXGEN_COUNT) {
            pc_gx_raw_texgen_clear_record(dst);
        }
        pc_gx_raw_texgen_mark_invalid();
        return;
    }

    record = &g_gx.raw_texgen.texgen[dst];
    record->function = function;
    record->source = source;
    record->ordinary_matrix_id = ordinary_matrix_id;
    record->normalize = normalize;
    record->post_matrix_id = post_matrix_id;
    record->component_known = PC_GX_TEXGEN_KNOWN_ALL;
}

static void pc_gx_raw_texgen_matrix_clear(
    PCGXRawTexMatrix* record,
    uint32_t provenance,
    uint32_t type
) {
    uint32_t logical_id = record->logical_id;

    memset(record, 0, sizeof(*record));
    record->logical_id = logical_id;
    record->slot_known = 1;
    record->provenance = provenance;
    record->last_load_type = type;
}

static PCGXRawTexMatrix* pc_gx_raw_texgen_matrix_record(
    uint32_t id,
    int* post
) {
    int slot = pc_gx_raw_texgen_ordinary_slot(id);

    if (slot >= 0) {
        *post = 0;
        return &g_gx.raw_texgen.ordinary[slot];
    }
    slot = pc_gx_raw_texgen_post_slot(id);
    if (slot >= 0) {
        *post = 1;
        return &g_gx.raw_texgen.post[slot];
    }
    return NULL;
}

static void pc_gx_raw_texgen_matrix_mark_unresolved(
    u16 mtx_indx,
    uint32_t id,
    uint32_t type
) {
    int post;
    uint32_t count;
    uint32_t mask;
    PCGXRawTexMatrix* record;

    (void)mtx_indx;
    record = pc_gx_raw_texgen_matrix_record(id, &post);
    if (record == NULL || !pc_gx_raw_texgen_matrix_type_is_valid(post, type)) {
        if (record != NULL) {
            pc_gx_raw_texgen_matrix_clear(
                record,
                PC_GX_TEXGEN_MATRIX_PROVENANCE_INVALID,
                type
            );
        }
        pc_gx_raw_texgen_mark_invalid();
        return;
    }

    count = pc_gx_raw_texgen_matrix_word_count(type);
    mask = pc_gx_raw_texgen_matrix_word_mask(count);
    record->slot_known = 1;
    record->provenance = PC_GX_TEXGEN_MATRIX_PROVENANCE_INDEXED_UNRESOLVED;
    record->last_load_type = type;
    record->last_written_word_count = count;
    record->known_word_mask &= ~mask;
    memset(record->words, 0, sizeof(uint32_t) * count);
}

static void pc_gx_raw_texgen_matrix_store_immediate(
    const void* mtx,
    uint32_t id,
    uint32_t type
) {
    int post;
    uint32_t count;
    uint32_t mask;
    uint32_t words[PC_GX_TEXGEN_MATRIX_WORD_COUNT];
    PCGXRawTexMatrix* record;

    record = pc_gx_raw_texgen_matrix_record(id, &post);
    if (record == NULL) {
        pc_gx_raw_texgen_mark_invalid();
        return;
    }
    if (!pc_gx_raw_texgen_matrix_type_is_valid(post, type)) {
        pc_gx_raw_texgen_matrix_clear(
            record,
            PC_GX_TEXGEN_MATRIX_PROVENANCE_INVALID,
            type
        );
        pc_gx_raw_texgen_mark_invalid();
        return;
    }
    count = pc_gx_raw_texgen_matrix_word_count(type);
    mask = pc_gx_raw_texgen_matrix_word_mask(count);
    if (mtx == NULL) {
        pc_gx_raw_texgen_matrix_clear(
            record,
            PC_GX_TEXGEN_MATRIX_PROVENANCE_INVALID,
            type
        );
        pc_gx_raw_texgen_mark_invalid();
        return;
    }

    memset(words, 0, sizeof(words));
    memcpy(words, mtx, sizeof(uint32_t) * count);
    if (!pc_gx_transform_words_are_finite(words, count)) {
        pc_gx_raw_texgen_matrix_clear(
            record,
            PC_GX_TEXGEN_MATRIX_PROVENANCE_INVALID,
            type
        );
        pc_gx_raw_texgen_mark_invalid();
        return;
    }

    record->slot_known = 1;
    record->provenance = PC_GX_TEXGEN_MATRIX_PROVENANCE_IMMEDIATE;
    record->last_load_type = type;
    record->last_written_word_count = count;
    memcpy(record->words, words, sizeof(uint32_t) * count);
    record->known_word_mask |= mask;
}

static int pc_gx_raw_texgen_matrix_range_is_known(
    const PCGXRawTexMatrix* record,
    uint32_t count
) {
    uint32_t mask = pc_gx_raw_texgen_matrix_word_mask(count);

    return record->slot_known != 0 &&
        (record->known_word_mask & mask) == mask;
}

static int pc_gx_raw_texgen_record_is_complete(
    const PCGXRawTexgenRecord* record
) {
    return record->component_known == PC_GX_TEXGEN_KNOWN_ALL &&
        pc_gx_raw_texgen_record_values_are_valid(
            record->function,
            record->source
        ) &&
        pc_gx_raw_texgen_ordinary_slot(record->ordinary_matrix_id) >= 0 &&
        pc_gx_raw_texgen_post_slot(record->post_matrix_id) >= 0 &&
        (record->normalize == (uint32_t)GX_FALSE ||
         record->normalize == (uint32_t)GX_TRUE);
}

static int pc_gx_raw_texgen_words_are_zero(const uint32_t* words) {
    uint32_t index;

    for (index = 0; index < PC_GX_TEXGEN_MATRIX_WORD_COUNT; index++) {
        if (words[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int pc_gx_raw_texgen_matrix_record_is_well_formed(
    const PCGXRawTexMatrix* record,
    uint32_t expected_id,
    int post
) {
    uint32_t word;

    if (record->logical_id != expected_id ||
        (record->known_word_mask & ~UINT32_C(0x00000FFF)) != 0) {
        return 0;
    }
    for (word = 0; word < PC_GX_TEXGEN_MATRIX_WORD_COUNT; word++) {
        if ((record->known_word_mask & (1u << word)) == 0 &&
            record->words[word] != 0) {
            return 0;
        }
    }
    if (record->slot_known == 0) {
        return record->provenance ==
                PC_GX_TEXGEN_MATRIX_PROVENANCE_NONE &&
            record->last_load_type == 0 &&
            record->last_written_word_count == 0 &&
            record->known_word_mask == 0 &&
            pc_gx_raw_texgen_words_are_zero(record->words);
    }
    if (record->slot_known != 1 || record->last_written_word_count > 12) {
        return 0;
    }
    if (record->last_written_word_count == 0) {
        return record->known_word_mask == 0 &&
            pc_gx_raw_texgen_words_are_zero(record->words);
    }
    if (record->last_written_word_count != 8 &&
        record->last_written_word_count != 12) {
        return 0;
    }
    if (post && record->last_written_word_count != 12) {
        return 0;
    }
    if (!pc_gx_raw_texgen_matrix_type_is_valid(
            post,
            record->last_load_type
        ) || (record->provenance !=
              PC_GX_TEXGEN_MATRIX_PROVENANCE_IMMEDIATE &&
              record->provenance !=
              PC_GX_TEXGEN_MATRIX_PROVENANCE_INDEXED_UNRESOLVED)) {
        return 0;
    }
    return 1;
}

static int pc_gx_raw_texgen_su_record_is_well_formed(
    const PCGXRawTexcoordSU* record
) {
    uint32_t known = record->component_known;

    if ((known & ~UINT32_C(0x0000007F)) != 0) {
        return 0;
    }
    if ((known & PC_GX_TEXGEN_SU_KNOWN_MANUAL) == 0 &&
        record->manual_enable != 0) {
        return 0;
    }
    if ((known & PC_GX_TEXGEN_SU_KNOWN_SCALE_S) == 0 &&
        record->scale_s_raw_u16 != 0) {
        return 0;
    }
    if ((known & PC_GX_TEXGEN_SU_KNOWN_SCALE_T) == 0 &&
        record->scale_t_raw_u16 != 0) {
        return 0;
    }
    if ((known & PC_GX_TEXGEN_SU_KNOWN_BIAS_S) == 0 &&
        record->bias_s != 0) {
        return 0;
    }
    if ((known & PC_GX_TEXGEN_SU_KNOWN_BIAS_T) == 0 &&
        record->bias_t != 0) {
        return 0;
    }
    if ((known & PC_GX_TEXGEN_SU_KNOWN_CYLINDER_S) == 0 &&
        record->cylinder_s != 0) {
        return 0;
    }
    if ((known & PC_GX_TEXGEN_SU_KNOWN_CYLINDER_T) == 0 &&
        record->cylinder_t != 0) {
        return 0;
    }
    return ((known & PC_GX_TEXGEN_SU_KNOWN_MANUAL) == 0 ||
            pc_gx_raw_texgen_bool_is_valid(record->manual_enable)) &&
        ((known & PC_GX_TEXGEN_SU_KNOWN_BIAS_S) == 0 ||
         pc_gx_raw_texgen_bool_is_valid(record->bias_s)) &&
        ((known & PC_GX_TEXGEN_SU_KNOWN_BIAS_T) == 0 ||
         pc_gx_raw_texgen_bool_is_valid(record->bias_t)) &&
        ((known & PC_GX_TEXGEN_SU_KNOWN_CYLINDER_S) == 0 ||
         pc_gx_raw_texgen_bool_is_valid(record->cylinder_s)) &&
        ((known & PC_GX_TEXGEN_SU_KNOWN_CYLINDER_T) == 0 ||
         pc_gx_raw_texgen_bool_is_valid(record->cylinder_t));
}

static int pc_gx_raw_texgen_active_state_is_valid(void) {
    const PCGXRawTexgen* shadow = &g_gx.raw_texgen;
    uint32_t bump_count = 0;
    uint32_t color_count = 0;
    uint32_t phase = 0;
    uint32_t color_mask = 0;
    uint32_t index;

    if (shadow->invalid != 0 || shadow->active_texgen_count_known == 0 ||
        shadow->active_texgen_count > PC_GX_TEXGEN_COUNT) {
        return 0;
    }

    for (index = 0; index < PC_GX_TEXGEN_COUNT; index++) {
        const PCGXRawTexgenRecord* record = &shadow->texgen[index];

        if ((record->component_known & ~PC_GX_TEXGEN_KNOWN_ALL) != 0 ||
            (record->component_known != 0 &&
             record->component_known != PC_GX_TEXGEN_KNOWN_ALL)) {
            return 0;
        }
        if (record->component_known == 0 &&
            (record->function != 0 || record->source != 0 ||
             record->ordinary_matrix_id != 0 || record->normalize != 0 ||
             record->post_matrix_id != 0)) {
            return 0;
        }
        if (record->component_known == PC_GX_TEXGEN_KNOWN_ALL &&
            !pc_gx_raw_texgen_record_is_complete(record)) {
            return 0;
        }
        if (!pc_gx_raw_texgen_su_record_is_well_formed(&shadow->su[index])) {
            return 0;
        }
    }
    for (index = 0; index < PC_GX_TEXGEN_ORDINARY_MATRIX_COUNT; index++) {
        uint32_t expected_id = index < 10 ?
            (uint32_t)(GX_TEXMTX0 + index * 3) : (uint32_t)GX_IDENTITY;

        if (!pc_gx_raw_texgen_matrix_record_is_well_formed(
                &shadow->ordinary[index], expected_id, 0
            )) {
            return 0;
        }
    }
    for (index = 0; index < PC_GX_TEXGEN_POST_MATRIX_COUNT; index++) {
        uint32_t expected_id = index < 20 ?
            (uint32_t)(GX_PTTEXMTX0 + index * 3) : (uint32_t)GX_PTIDENTITY;

        if (!pc_gx_raw_texgen_matrix_record_is_well_formed(
                &shadow->post[index], expected_id, 1
            )) {
            return 0;
        }
    }

    for (index = 0; index < shadow->active_texgen_count; index++) {
        const PCGXRawTexgenRecord* record = &shadow->texgen[index];
        const PCGXRawTexMatrix* ordinary;
        const PCGXRawTexMatrix* post;
        uint32_t ordinary_word_count = 0;
        int ordinary_slot;
        int post_slot;

        if (!pc_gx_raw_texgen_record_is_complete(record)) {
            return 0;
        }
        if (record->function == (uint32_t)GX_TG_MTX2x4 ||
            record->function == (uint32_t)GX_TG_MTX3x4) {
            if (phase != 0) {
                return 0;
            }
            ordinary_word_count = record->function ==
                    (uint32_t)GX_TG_MTX2x4 ? 8u : 12u;
        } else if (record->function >= (uint32_t)GX_TG_BUMP0 &&
                   record->function <= (uint32_t)GX_TG_BUMP7) {
            if (phase > 1 || ++bump_count > 3) {
                return 0;
            }
            {
                uint32_t source_index = record->source -
                    (uint32_t)GX_TG_TEXCOORD0;
                if (source_index >= index ||
                    shadow->texgen[source_index].component_known !=
                        PC_GX_TEXGEN_KNOWN_ALL ||
                    !pc_gx_raw_texgen_function_is_regular(
                        shadow->texgen[source_index].function
                    )) {
                    return 0;
                }
            }
            phase = 1;
        } else {
            if (phase > 2 || ++color_count > 2) {
                return 0;
            }
            phase = 2;
            if (record->source == (uint32_t)GX_TG_COLOR0) {
                if (color_count != 1 || (color_mask & 1u) != 0) {
                    return 0;
                }
                color_mask |= 1u;
            } else {
                if (color_count == 1 || (color_mask & 2u) != 0) {
                    return 0;
                }
                color_mask |= 2u;
            }
        }

        ordinary_slot = pc_gx_raw_texgen_ordinary_slot(
            record->ordinary_matrix_id
        );
        post_slot = pc_gx_raw_texgen_post_slot(record->post_matrix_id);
        ordinary = &shadow->ordinary[ordinary_slot];
        post = &shadow->post[post_slot];
        if ((ordinary_word_count != 0 &&
             !pc_gx_raw_texgen_matrix_range_is_known(
                 ordinary, ordinary_word_count
             )) || !pc_gx_raw_texgen_matrix_range_is_known(post, 12u)) {
            return 0;
        }
    }
    return 1;
}

static PCGXRawTexcoordSU* pc_gx_raw_texgen_su_record(uint32_t coord) {
    if (coord >= PC_GX_TEXGEN_COUNT) {
        return NULL;
    }
    return &g_gx.raw_texgen.su[coord];
}

static void pc_gx_raw_texgen_su_clear_components(
    PCGXRawTexcoordSU* record,
    uint32_t mask
) {
    record->component_known &= ~mask;
    if ((mask & PC_GX_TEXGEN_SU_KNOWN_MANUAL) != 0) {
        record->manual_enable = 0;
    }
    if ((mask & PC_GX_TEXGEN_SU_KNOWN_SCALE_S) != 0) {
        record->scale_s_raw_u16 = 0;
    }
    if ((mask & PC_GX_TEXGEN_SU_KNOWN_SCALE_T) != 0) {
        record->scale_t_raw_u16 = 0;
    }
    if ((mask & PC_GX_TEXGEN_SU_KNOWN_BIAS_S) != 0) {
        record->bias_s = 0;
    }
    if ((mask & PC_GX_TEXGEN_SU_KNOWN_BIAS_T) != 0) {
        record->bias_t = 0;
    }
    if ((mask & PC_GX_TEXGEN_SU_KNOWN_CYLINDER_S) != 0) {
        record->cylinder_s = 0;
    }
    if ((mask & PC_GX_TEXGEN_SU_KNOWN_CYLINDER_T) != 0) {
        record->cylinder_t = 0;
    }
}

static int pc_gx_raw_texgen_bool_is_valid(uint32_t value) {
    return value == (uint32_t)GX_FALSE || value == (uint32_t)GX_TRUE;
}

static void pc_gx_raw_texgen_su_store_manual(
    uint32_t coord,
    uint32_t enable,
    u16 ss,
    u16 ts
) {
    PCGXRawTexcoordSU* record = pc_gx_raw_texgen_su_record(coord);

    if (record == NULL) {
        pc_gx_raw_texgen_mark_invalid();
        return;
    }
    if (!pc_gx_raw_texgen_bool_is_valid(enable)) {
        pc_gx_raw_texgen_su_clear_components(
            record,
            PC_GX_TEXGEN_SU_KNOWN_MANUAL
        );
        pc_gx_raw_texgen_mark_invalid();
        return;
    }

    record->manual_enable = enable;
    record->component_known |= PC_GX_TEXGEN_SU_KNOWN_MANUAL;
    if (enable != (uint32_t)GX_FALSE) {
        /* The GX register receives (scale - 1) in a 16-bit field. */
        record->scale_s_raw_u16 = (uint32_t)(uint16_t)(ss - 1u);
        record->scale_t_raw_u16 = (uint32_t)(uint16_t)(ts - 1u);
        record->component_known |= PC_GX_TEXGEN_SU_KNOWN_SCALE_S |
            PC_GX_TEXGEN_SU_KNOWN_SCALE_T;
    }
    /* Disabling manual mode intentionally preserves both raw scale words. */
}

static void pc_gx_raw_texgen_su_store_bias(
    uint32_t coord,
    uint32_t s,
    uint32_t t
) {
    PCGXRawTexcoordSU* record = pc_gx_raw_texgen_su_record(coord);

    if (record == NULL || !pc_gx_raw_texgen_bool_is_valid(s) ||
        !pc_gx_raw_texgen_bool_is_valid(t)) {
        if (record != NULL) {
            pc_gx_raw_texgen_su_clear_components(
                record,
                PC_GX_TEXGEN_SU_KNOWN_BIAS_S |
                    PC_GX_TEXGEN_SU_KNOWN_BIAS_T
            );
        }
        pc_gx_raw_texgen_mark_invalid();
        return;
    }
    record->bias_s = s;
    record->bias_t = t;
    record->component_known |= PC_GX_TEXGEN_SU_KNOWN_BIAS_S |
        PC_GX_TEXGEN_SU_KNOWN_BIAS_T;
}

static void pc_gx_raw_texgen_su_store_cylinder(
    uint32_t coord,
    uint32_t s,
    uint32_t t
) {
    PCGXRawTexcoordSU* record = pc_gx_raw_texgen_su_record(coord);

    if (record == NULL || !pc_gx_raw_texgen_bool_is_valid(s) ||
        !pc_gx_raw_texgen_bool_is_valid(t)) {
        if (record != NULL) {
            pc_gx_raw_texgen_su_clear_components(
                record,
                PC_GX_TEXGEN_SU_KNOWN_CYLINDER_S |
                    PC_GX_TEXGEN_SU_KNOWN_CYLINDER_T
            );
        }
        pc_gx_raw_texgen_mark_invalid();
        return;
    }
    record->cylinder_s = s;
    record->cylinder_t = t;
    record->component_known |= PC_GX_TEXGEN_SU_KNOWN_CYLINDER_S |
        PC_GX_TEXGEN_SU_KNOWN_CYLINDER_T;
}

/* Map tex matrix ID to slot: raw 0..9, GX enum 30..57 (stride 3), or 60=identity */
static int pc_tex_mtx_id_to_slot(int id) {
    if (id == GX_IDENTITY) return -1;
    if (id >= 0 && id < 10) return id;
    if (id >= GX_TEXMTX0 && id < GX_IDENTITY) return (id - GX_TEXMTX0) / 3;
    return -1;
}

static GLint pc_gx_get_uniform_location_profiled(GLuint shader, const char* name) {
    Uint64 t = pc_profiler_begin_timer();
    GLint loc = glGetUniformLocation(shader, name);
    pc_profiler_add_time(PC_PROF_TIMER_UNIFORM_LOOKUP, t);
    pc_profiler_add_count_uniform_lookup();
    return loc;
}

static void pc_gx_use_program_profiled(GLuint shader) {
    Uint64 t = pc_profiler_begin_timer();
    glUseProgram(shader);
    pc_profiler_add_time(PC_PROF_TIMER_SHADER_SWITCH, t);
    pc_profiler_add_count_shader_switch();
}

#ifndef PC_GX_ENABLE_TEXTURE_BIND_CACHE
#define PC_GX_ENABLE_TEXTURE_BIND_CACHE 0
#endif

#if PC_GX_ENABLE_TEXTURE_BIND_CACHE
#define PC_GX_MAX_TEXTURE_UNITS 8
static int s_active_texture_unit = -1;
static GLuint s_bound_texture_2d[PC_GX_MAX_TEXTURE_UNITS];
#endif

void pc_gx_texture_bind_cache_invalidate(void) {
#if PC_GX_ENABLE_TEXTURE_BIND_CACHE
    s_active_texture_unit = -1;
    memset(s_bound_texture_2d, 0, sizeof(s_bound_texture_2d));
#endif
}

static void pc_gx_active_texture_cached(GLenum texture) {
#if PC_GX_ENABLE_TEXTURE_BIND_CACHE
    int unit = (int)(texture - GL_TEXTURE0);
    if (unit < 0 || unit >= PC_GX_MAX_TEXTURE_UNITS) {
        glActiveTexture(texture);
        s_active_texture_unit = -1;
        return;
    }
    if (s_active_texture_unit == unit) return;
    glActiveTexture(texture);
    s_active_texture_unit = unit;
#else
    glActiveTexture(texture);
#endif
}

static void pc_gx_bind_texture_profiled(GLenum target, GLuint texture) {
#if PC_GX_ENABLE_TEXTURE_BIND_CACHE
    int unit = s_active_texture_unit;
    if (target == GL_TEXTURE_2D && unit >= 0 && unit < PC_GX_MAX_TEXTURE_UNITS &&
        s_bound_texture_2d[unit] == texture) {
        return;
    }

    Uint64 t = pc_profiler_begin_timer();
    glBindTexture(target, texture);
    pc_profiler_add_time(PC_PROF_TIMER_TEXTURE_BIND, t);
    pc_profiler_add_count_texture_bind();

    if (target == GL_TEXTURE_2D && unit >= 0 && unit < PC_GX_MAX_TEXTURE_UNITS) {
        s_bound_texture_2d[unit] = texture;
    }
#else
    Uint64 t = pc_profiler_begin_timer();
    glBindTexture(target, texture);
    pc_profiler_add_time(PC_PROF_TIMER_TEXTURE_BIND, t);
    pc_profiler_add_count_texture_bind();
#endif
}

static void pc_gx_buffer_data_profiled(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    Uint64 t = pc_profiler_begin_timer();
    glBufferData(target, size, data, usage);
    pc_profiler_add_time(PC_PROF_TIMER_BUFFER_UPLOAD, t);
    pc_profiler_add_count_buffer_upload((size_t)size);
}

static uint32_t pc_gx_float_bits(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int pc_gx_transform_exact_slot(u32 id) {
    int slot;

    for (slot = 0; slot < PC_GX_TRANSFORM_POSITION_COUNT; slot++) {
        if (id == (u32)(slot * 3)) {
            return slot;
        }
    }
    return -1;
}

static int pc_gx_transform_word_is_finite(uint32_t word) {
    return (word & UINT32_C(0x7F800000)) != UINT32_C(0x7F800000);
}

static int pc_gx_transform_words_are_finite(
    const uint32_t* words,
    size_t count
) {
    size_t index;

    for (index = 0; index < count; index++) {
        if (!pc_gx_transform_word_is_finite(words[index])) {
            return 0;
        }
    }
    return 1;
}

static int pc_gx_transform_projection_type_is_supported(u32 type) {
    return type == GX_PERSPECTIVE || type == GX_ORTHOGRAPHIC;
}

static int pc_gx_transform_projection_type_from_word(
    uint32_t word,
    u32* type
) {
    if (word == pc_gx_float_bits((float)GX_PERSPECTIVE)) {
        *type = GX_PERSPECTIVE;
        return 1;
    }
    if (word == pc_gx_float_bits((float)GX_ORTHOGRAPHIC)) {
        *type = GX_ORTHOGRAPHIC;
        return 1;
    }
    return 0;
}

static void pc_gx_transform_mark_invalid(void) {
    g_gx.raw_transform.invalid = 1;
}

static void pc_gx_transform_store_projection_words(
    u32 type,
    const uint32_t coefficients[PC_GX_TRANSFORM_PROJECTION_WORDS]
) {
    PCGXRawProjection* shadow = &g_gx.raw_transform.projection;

    shadow->type = type;
    memcpy(
        shadow->coefficients,
        coefficients,
        sizeof(shadow->coefficients)
    );
    shadow->known = (uint8_t)(
        pc_gx_transform_projection_type_is_supported(type) &&
        pc_gx_transform_words_are_finite(
            coefficients,
            PC_GX_TRANSFORM_PROJECTION_WORDS
        )
    );
    memset(shadow->reserved, 0, sizeof(shadow->reserved));
    if (!shadow->known) {
        pc_gx_transform_mark_invalid();
    }
}

static void pc_gx_transform_store_projection(
    const void* mtx,
    u32 type
) {
    uint32_t input[12];
    uint32_t coefficients[PC_GX_TRANSFORM_PROJECTION_WORDS];

    memcpy(input, mtx, sizeof(input));
    coefficients[0] = input[0];
    coefficients[2] = input[5];
    coefficients[4] = input[10];
    coefficients[5] = input[11];
    if (type == GX_ORTHOGRAPHIC) {
        coefficients[1] = input[3];
        coefficients[3] = input[7];
    } else {
        coefficients[1] = input[2];
        coefficients[3] = input[6];
    }
    pc_gx_transform_store_projection_words(type, coefficients);
}

static u32 pc_gx_transform_store_projectionv(
    const float* values
) {
    uint32_t input[7];
    uint32_t coefficients[PC_GX_TRANSFORM_PROJECTION_WORDS];
    u32 type;
    int type_known;

    memcpy(input, values, sizeof(input));
    type_known = pc_gx_transform_projection_type_from_word(input[0], &type);
    if (!type_known) {
        type = UINT32_MAX;
    }
    memcpy(coefficients, &input[1], sizeof(coefficients));
    pc_gx_transform_store_projection_words(type, coefficients);
    if (!type_known || !pc_gx_transform_word_is_finite(input[0])) {
        pc_gx_transform_mark_invalid();
    }
    return type;
}

static void pc_gx_transform_store_position(
    const void* mtx,
    u32 id
) {
    uint32_t words[PC_GX_TRANSFORM_POSITION_WORDS];
    int slot = pc_gx_transform_exact_slot(id);
    PCGXRawPositionMatrix* shadow;

    memcpy(words, mtx, sizeof(words));
    if (slot < 0) {
        pc_gx_transform_mark_invalid();
        return;
    }
    shadow = &g_gx.raw_transform.position[slot];
    memcpy(shadow->words, words, sizeof(shadow->words));
    shadow->known = (uint8_t)pc_gx_transform_words_are_finite(
        words,
        PC_GX_TRANSFORM_POSITION_WORDS
    );
    memset(shadow->reserved, 0, sizeof(shadow->reserved));
    if (shadow->known) {
        g_gx.raw_transform.position_indexed_unresolved[slot] = 0;
    } else {
        pc_gx_transform_mark_invalid();
    }
}

static void pc_gx_transform_store_normal_3x4(
    const void* mtx,
    u32 id
) {
    uint32_t input[PC_GX_TRANSFORM_POSITION_WORDS];
    uint32_t words[PC_GX_TRANSFORM_NORMAL_WORDS];
    int slot = pc_gx_transform_exact_slot(id);
    PCGXRawNormalMatrix* shadow;

    memcpy(input, mtx, sizeof(input));
    if (slot < 0) {
        pc_gx_transform_mark_invalid();
        return;
    }
    words[0] = input[0];
    words[1] = input[1];
    words[2] = input[2];
    words[3] = input[4];
    words[4] = input[5];
    words[5] = input[6];
    words[6] = input[8];
    words[7] = input[9];
    words[8] = input[10];
    shadow = &g_gx.raw_transform.normal[slot];
    memcpy(shadow->words, words, sizeof(shadow->words));
    shadow->known = (uint8_t)pc_gx_transform_words_are_finite(
        words,
        PC_GX_TRANSFORM_NORMAL_WORDS
    );
    memset(shadow->reserved, 0, sizeof(shadow->reserved));
    if (shadow->known) {
        g_gx.raw_transform.normal_indexed_unresolved[slot] = 0;
    } else {
        pc_gx_transform_mark_invalid();
    }
}

static void pc_gx_transform_store_normal_3x3(
    const void* mtx,
    u32 id
) {
    uint32_t words[PC_GX_TRANSFORM_NORMAL_WORDS];
    int slot = pc_gx_transform_exact_slot(id);
    PCGXRawNormalMatrix* shadow;

    memcpy(words, mtx, sizeof(words));
    if (slot < 0) {
        pc_gx_transform_mark_invalid();
        return;
    }
    shadow = &g_gx.raw_transform.normal[slot];
    memcpy(shadow->words, words, sizeof(shadow->words));
    shadow->known = (uint8_t)pc_gx_transform_words_are_finite(
        words,
        PC_GX_TRANSFORM_NORMAL_WORDS
    );
    memset(shadow->reserved, 0, sizeof(shadow->reserved));
    if (shadow->known) {
        g_gx.raw_transform.normal_indexed_unresolved[slot] = 0;
    } else {
        pc_gx_transform_mark_invalid();
    }
}

static void pc_gx_transform_store_current(u32 id) {
    g_gx.raw_transform.current_position_id = id;
    g_gx.raw_transform.current_position_known = (uint8_t)(
        pc_gx_transform_exact_slot(id) >= 0
    );
    if (!g_gx.raw_transform.current_position_known) {
        pc_gx_transform_mark_invalid();
    }
}

static void pc_gx_transform_mark_indexed_unknown(
    int normal,
    u32 id
) {
    int slot = pc_gx_transform_exact_slot(id);

    if (slot < 0) {
        pc_gx_transform_mark_invalid();
    } else if (normal) {
        g_gx.raw_transform.normal_indexed_unresolved[slot] = 1;
        memset(
            g_gx.raw_transform.normal[slot].words,
            0,
            sizeof(g_gx.raw_transform.normal[slot].words)
        );
        g_gx.raw_transform.normal[slot].known = 0;
    } else {
        g_gx.raw_transform.position_indexed_unresolved[slot] = 1;
        memset(
            g_gx.raw_transform.position[slot].words,
            0,
            sizeof(g_gx.raw_transform.position[slot].words)
        );
        g_gx.raw_transform.position[slot].known = 0;
    }
}

static int pc_gx_tev_passes_vertex_color(void) {
    const PCGXTevStage* stage;

    if (g_gx.num_tev_stages != 1) {
        return 0;
    }
    stage = &g_gx.tev_stages[0];
    return stage->color_a == GX_CC_ZERO &&
        stage->color_b == GX_CC_ZERO &&
        stage->color_c == GX_CC_ZERO &&
        stage->color_d == GX_CC_RASC &&
        stage->alpha_a == GX_CA_ZERO &&
        stage->alpha_b == GX_CA_ZERO &&
        stage->alpha_c == GX_CA_ZERO &&
        stage->alpha_d == GX_CA_RASA &&
        stage->color_op == GX_TEV_ADD &&
        stage->color_bias == GX_TB_ZERO &&
        stage->color_scale == GX_CS_SCALE_1 &&
        stage->color_clamp != 0 &&
        stage->color_out == GX_TEVPREV &&
        stage->alpha_op == GX_TEV_ADD &&
        stage->alpha_bias == GX_TB_ZERO &&
        stage->alpha_scale == GX_CS_SCALE_1 &&
        stage->alpha_clamp != 0 &&
        stage->alpha_out == GX_TEVPREV &&
        stage->tex_coord == GX_TEXCOORD_NULL &&
        stage->tex_map == GX_TEXMAP_NULL &&
        stage->color_chan == GX_COLOR0A0 &&
        stage->ras_swap == GX_TEV_SWAP0 &&
        stage->tex_swap == GX_TEV_SWAP0;
}

static int pc_gx_has_active_texture_state(void) {
    int stage;

    if (g_gx.num_tex_gens != 0 || g_gx.num_ind_stages != 0) {
        return 1;
    }
    for (stage = 0; stage < g_gx.num_tev_stages && stage < 16; stage++) {
        if (g_gx.tev_stages[stage].tex_coord != GX_TEXCOORD_NULL ||
            g_gx.tev_stages[stage].tex_map != GX_TEXMAP_NULL) {
            return 1;
        }
    }
    return 0;
}

/*
 * The v1 packet has no GX TEV, lighting, or native texture-object fields.
 * Only the exact pass-through raster-color state can cross this seam without
 * silently changing the draw. Unsupported state remains on the legacy GL path.
 */
static int pc_gx_semantic_handoff_state_is_supported(void) {
    int channel;

    if (!pc_gx_tev_passes_vertex_color() ||
        pc_gx_has_active_texture_state() ||
        g_gx.num_chans != 0 ||
        g_gx.fog_type != GX_FOG_NONE ||
        g_gx.alpha_comp0 != GX_ALWAYS ||
        g_gx.alpha_comp1 != GX_ALWAYS ||
        g_gx.alpha_op != GX_AOP_AND ||
        g_gx.alpha_ref0 != 0 ||
        g_gx.alpha_ref1 != 0 ||
        g_gx.current_mtx < 0 ||
        g_gx.current_mtx >= 10) {
        return 0;
    }

    for (channel = 0; channel < 4; channel++) {
        if (g_gx.chan_ctrl_enable[channel] != 0) {
            return 0;
        }
    }
    /* Resident GL texture objects are harmless until draw state references them. */
    return 1;
}

static int pc_gx_build_semantic_packet_internal(
    int first_vertex,
    int vertex_count,
    AcgcGxSemanticPacket* packet,
    int require_legacy_state
) {
    int vertex_index;
    uint32_t primitive;

    if (packet == NULL) {
        return 0;
    }
    memset(packet, 0, sizeof(*packet));

    if (first_vertex < 0 || vertex_count <= 0 ||
        first_vertex > PC_GX_MAX_VERTS - vertex_count ||
        g_gx.current_vertex_idx < first_vertex + vertex_count ||
        g_gx.in_begin != 0 ||
        g_gx.vertex_pending != 0 ||
        g_gx.expected_vertex_count != vertex_count ||
        vertex_count > (int)ACGC_GX_SEMANTIC_MAX_VERTICES ||
        (require_legacy_state && !pc_gx_semantic_handoff_state_is_supported())) {
        return 0;
    }

    switch (g_gx.current_primitive) {
        case GX_TRIANGLES:
            if ((vertex_count % 3) != 0) {
                return 0;
            }
            primitive = ACGC_GX_SEMANTIC_PRIMITIVE_TRIANGLES;
            break;
        case GX_QUADS:
            if ((vertex_count % 4) != 0) {
                return 0;
            }
            primitive = ACGC_GX_SEMANTIC_PRIMITIVE_QUADS;
            break;
        default:
            return 0;
    }

    if (!acgc_gx_semantic_packet_init(packet)) {
        return 0;
    }
    packet->primitive = primitive;
    packet->vertex_count = (uint32_t)vertex_count;
    packet->material.flags = ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR;

    for (vertex_index = 0; vertex_index < 4; vertex_index++) {
        packet->material.color[vertex_index] = pc_gx_float_bits(1.0f);
    }
    for (vertex_index = 0; vertex_index < 16; vertex_index++) {
        packet->transform.projection[vertex_index] =
            pc_gx_float_bits(((const float*)g_gx.projection_mtx)[vertex_index]);
    }
    for (vertex_index = 0; vertex_index < 12; vertex_index++) {
        packet->transform.modelview[vertex_index] =
            pc_gx_float_bits(((const float*)g_gx.pos_mtx[g_gx.current_mtx])[vertex_index]);
    }
    for (vertex_index = 0; vertex_index < 9; vertex_index++) {
        packet->transform.normal[vertex_index] =
            pc_gx_float_bits(((const float*)g_gx.nrm_mtx[g_gx.current_mtx])[vertex_index]);
    }
    for (vertex_index = 0; vertex_index < vertex_count; vertex_index++) {
        const PCGXVertex* source = &g_gx.vertex_buffer[first_vertex + vertex_index];
        AcgcGxSemanticVertex* destination = &packet->vertices[vertex_index];
        int component;

        for (component = 0; component < 3; component++) {
            destination->position[component] = pc_gx_float_bits(
                source->position[component]
            );
            destination->normal[component] = pc_gx_float_bits(
                source->normal[component]
            );
        }
        destination->color_rgba8 =
            ((uint32_t)source->color0[0] << 24) |
            ((uint32_t)source->color0[1] << 16) |
            ((uint32_t)source->color0[2] << 8) |
            source->color0[3];
        destination->texcoord0[0] = pc_gx_float_bits(source->texcoord[0][0]);
        destination->texcoord0[1] = pc_gx_float_bits(source->texcoord[0][1]);
    }

    if (!acgc_gx_semantic_packet_validate(packet)) {
        memset(packet, 0, sizeof(*packet));
        return 0;
    }
    return 1;
}

static int pc_gx_build_semantic_packet(
    int first_vertex,
    int vertex_count,
    AcgcGxSemanticPacket* packet
) {
    return pc_gx_build_semantic_packet_internal(
        first_vertex,
        vertex_count,
        packet,
        1
    );
}

static int pc_gx_v2_map_color_input(int value, uint32_t* output) {
    if (output == NULL) {
        return 0;
    }
    switch (value) {
        case GX_CC_ZERO:
            *output = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_ZERO;
            return 1;
        case GX_CC_CPREV:
            *output = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_PREVIOUS;
            return 1;
        case GX_CC_C0:
            *output = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_REGISTER0;
            return 1;
        case GX_CC_C1:
            *output = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_REGISTER1;
            return 1;
        case GX_CC_C2:
            *output = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_REGISTER2;
            return 1;
        case GX_CC_TEXC:
            *output = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_TEXTURE;
            return 1;
        case GX_CC_RASC:
            *output = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_RASTER;
            return 1;
        case GX_CC_ONE:
            *output = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_ONE;
            return 1;
        case GX_CC_HALF:
            *output = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_HALF;
            return 1;
        case GX_CC_KONST:
            *output = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_CONSTANT;
            return 1;
        default:
            return 0;
    }
}

static int pc_gx_v2_map_alpha_input(int value, uint32_t* output) {
    if (output == NULL) {
        return 0;
    }
    switch (value) {
        case GX_CA_ZERO:
            *output = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_ZERO;
            return 1;
        case GX_CA_APREV:
            *output = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_PREVIOUS;
            return 1;
        case GX_CA_A0:
            *output = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_REGISTER0;
            return 1;
        case GX_CA_A1:
            *output = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_REGISTER1;
            return 1;
        case GX_CA_A2:
            *output = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_REGISTER2;
            return 1;
        case GX_CA_TEXA:
            *output = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_TEXTURE;
            return 1;
        case GX_CA_RASA:
            *output = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_RASTER;
            return 1;
        case GX_CA_KONST:
            *output = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_CONSTANT;
            return 1;
        default:
            return 0;
    }
}

static int pc_gx_v2_map_tev_operation(int value, uint32_t* output) {
    if (output == NULL) {
        return 0;
    }
    switch (value) {
        case GX_TEV_ADD:
            *output = ACGC_GX_SEMANTIC_V2_TEV_OP_ADD;
            return 1;
        case GX_TEV_SUB:
            *output = ACGC_GX_SEMANTIC_V2_TEV_OP_SUBTRACT;
            return 1;
        default:
            return 0;
    }
}

static int pc_gx_v2_map_kcolor_selector(int value, uint32_t* output) {
    if (output == NULL) {
        return 0;
    }
    switch (value) {
        case GX_TEV_KCSEL_1:
            *output = ACGC_GX_SEMANTIC_V2_TEV_KCOLOR_ONE;
            return 1;
        case GX_TEV_KCSEL_1_4:
            *output = ACGC_GX_SEMANTIC_V2_TEV_KCOLOR_ONE_QUARTER;
            return 1;
        default:
            return 0;
    }
}

static int pc_gx_v2_map_kalpha_selector(int value, uint32_t* output) {
    if (output == NULL) {
        return 0;
    }
    switch (value) {
        case GX_TEV_KASEL_1:
            *output = ACGC_GX_SEMANTIC_V2_TEV_KALPHA_ONE;
            return 1;
        case GX_TEV_KASEL_1_4:
            *output = ACGC_GX_SEMANTIC_V2_TEV_KALPHA_ONE_QUARTER;
            return 1;
        default:
            return 0;
    }
}

static int pc_gx_v2_projection_type(uint32_t* output) {
    if (output == NULL) {
        return 0;
    }
    switch (g_gx.projection_type) {
        case GX_PERSPECTIVE:
            *output = ACGC_GX_SEMANTIC_V2_PROJECTION_PERSPECTIVE;
            return 1;
        case GX_ORTHOGRAPHIC:
            *output = ACGC_GX_SEMANTIC_V2_PROJECTION_ORTHOGRAPHIC;
            return 1;
        default:
            return 0;
    }
}

static int pc_gx_v2_texture_format_is_valid(int format) {
    switch (format) {
        case GX_TF_I4:
        case GX_TF_I8:
        case GX_TF_IA4:
        case GX_TF_IA8:
        case GX_TF_RGB565:
        case GX_TF_RGB5A3:
        case GX_TF_RGBA8:
        case GX_TF_CMPR:
        case GX_TF_C4:
        case GX_TF_C8:
        case GX_TF_C14X2:
            return 1;
        default:
            return 0;
    }
}

static int pc_gx_v2_texture_format_uses_tlut(int format) {
    return format == GX_TF_C4 || format == GX_TF_C8 || format == GX_TF_C14X2;
}

static int pc_gx_v2_texture_is_resolved(int map) {
    return map >= 0 && map < 8 &&
        g_gx.gl_textures[map] != 0 &&
        g_gx.tex_obj_w[map] > 0 && g_gx.tex_obj_w[map] <= 4096 &&
        g_gx.tex_obj_h[map] > 0 && g_gx.tex_obj_h[map] <= 4096 &&
        pc_gx_v2_texture_format_is_valid(g_gx.tex_obj_fmt[map]);
}

static int pc_gx_v2_channel_state_is_supported(uint32_t channel_count) {
    uint32_t index;

    if (channel_count == 0 || channel_count > ACGC_GX_SEMANTIC_MAX_CHANNELS) {
        return 0;
    }
    for (index = 0; index < channel_count; index++) {
        int color = (int)(index * 2);
        int alpha = color + 1;

        if (g_gx.chan_ctrl_enable[color] != 0 ||
            g_gx.chan_ctrl_enable[alpha] != 0 ||
            g_gx.chan_ctrl_amb_src[color] != GX_SRC_REG ||
            g_gx.chan_ctrl_amb_src[alpha] != GX_SRC_REG ||
            g_gx.chan_ctrl_mat_src[color] != GX_SRC_REG ||
            g_gx.chan_ctrl_mat_src[alpha] != GX_SRC_REG ||
            g_gx.chan_ctrl_light_mask[color] != 0 ||
            g_gx.chan_ctrl_light_mask[alpha] != 0 ||
            g_gx.chan_ctrl_diff_fn[color] != GX_DF_NONE ||
            g_gx.chan_ctrl_diff_fn[alpha] != GX_DF_NONE ||
            g_gx.chan_ctrl_attn_fn[color] != GX_AF_NONE ||
            g_gx.chan_ctrl_attn_fn[alpha] != GX_AF_NONE) {
            return 0;
        }
    }
    return 1;
}

static int pc_gx_v2_stage_state_is_supported(uint32_t stage_count) {
    uint32_t index;

    if (stage_count == 0 || stage_count > ACGC_GX_SEMANTIC_MAX_TEV_STAGES) {
        return 0;
    }
    for (index = 0; index < stage_count; index++) {
        const PCGXTevStage* stage = &g_gx.tev_stages[index];

        if (stage->tex_coord < 0 || stage->tex_coord >= (int)g_gx.num_tex_gens ||
            stage->tex_map != (int)index ||
            stage->color_chan != GX_COLOR0A0 ||
            stage->ind_stage != 0 || stage->ind_format != 0 ||
            stage->ind_bias != 0 || stage->ind_mtx != 0 ||
            stage->ind_wrap_s != 0 || stage->ind_wrap_t != 0 ||
            stage->ind_add_prev != 0 || stage->ind_lod != 0 ||
            stage->ind_alpha != 0 ||
            !pc_gx_v2_texture_is_resolved(stage->tex_map)) {
            return 0;
        }
    }
    return 1;
}

static const char* pc_gx_semantic_v2_rejection_reason_name(
    PCGXSemanticV2RejectionReason reason
) {
    switch (reason) {
        case PCGX_SEMANTIC_V2_REJECTION_VERTEX_OR_COUNT:
            return "vertex_or_count";
        case PCGX_SEMANTIC_V2_REJECTION_GLOBAL_COUNT:
            return "global_count";
        case PCGX_SEMANTIC_V2_REJECTION_ALPHA_TEST:
            return "alpha_test";
        case PCGX_SEMANTIC_V2_REJECTION_BLEND:
            return "blend";
        case PCGX_SEMANTIC_V2_REJECTION_DEPTH:
            return "depth";
        case PCGX_SEMANTIC_V2_REJECTION_COLOR_ALPHA_UPDATE:
            return "color_alpha_update";
        case PCGX_SEMANTIC_V2_REJECTION_CULL:
            return "cull";
        case PCGX_SEMANTIC_V2_REJECTION_MATRIX_PROJECTION:
            return "matrix_projection";
        case PCGX_SEMANTIC_V2_REJECTION_CHANNEL:
            return "channel";
        case PCGX_SEMANTIC_V2_REJECTION_STAGE_TEXTURE:
            return "stage_or_texture";
        case PCGX_SEMANTIC_V2_REJECTION_TEXGEN:
            return "texgen";
        case PCGX_SEMANTIC_V2_REJECTION_SUPPORTED:
            return "supported";
        default:
            return "unknown";
    }
}

/* V2 carries no alpha-test payload. When both comparisons are ALWAYS and the
 * operation is AND, the two GX reference values cannot affect the result. */
static int pc_gx_v2_alpha_test_state_is_supported(void) {
    return g_gx.alpha_comp0 == GX_ALWAYS &&
        g_gx.alpha_comp1 == GX_ALWAYS &&
        g_gx.alpha_op == GX_AOP_AND;
}

/* The state half follows pc_gx_semantic_v2_state_is_supported() in its
 * existing short-circuit order. Keep the predicate below as the acceptance
 * authority while this classifier provides only a bounded diagnostic view. */
static PCGXSemanticV2RejectionReason
pc_gx_semantic_v2_state_rejection_reason(void) {
    uint32_t index;

    if (g_gx.num_chans <= 0 ||
        g_gx.num_chans > (int)ACGC_GX_SEMANTIC_MAX_CHANNELS ||
        g_gx.num_tex_gens <= 0 ||
        g_gx.num_tex_gens > (int)ACGC_GX_SEMANTIC_MAX_TEXTURE_GENERATORS ||
        g_gx.num_tev_stages <= 0 ||
        g_gx.num_tev_stages > (int)ACGC_GX_SEMANTIC_MAX_TEV_STAGES ||
        g_gx.num_tex_gens != g_gx.num_tev_stages ||
        g_gx.num_ind_stages != 0 ||
        g_gx.fog_type != GX_FOG_NONE) {
        return PCGX_SEMANTIC_V2_REJECTION_GLOBAL_COUNT;
    }
    if (!pc_gx_v2_alpha_test_state_is_supported()) {
        return PCGX_SEMANTIC_V2_REJECTION_ALPHA_TEST;
    }
    if (g_gx.blend_mode != GX_BM_NONE ||
        g_gx.blend_src != GX_BL_ONE ||
        g_gx.blend_dst != GX_BL_ZERO ||
        g_gx.blend_logic_op != GX_LO_CLEAR) {
        return PCGX_SEMANTIC_V2_REJECTION_BLEND;
    }
    if (g_gx.z_compare_enable == 0 ||
        g_gx.z_compare_func != GX_LEQUAL ||
        g_gx.z_update_enable == 0) {
        return PCGX_SEMANTIC_V2_REJECTION_DEPTH;
    }
    if (g_gx.color_update_enable == 0 ||
        g_gx.alpha_update_enable == 0) {
        return PCGX_SEMANTIC_V2_REJECTION_COLOR_ALPHA_UPDATE;
    }
    if (g_gx.cull_mode != GX_CULL_NONE) {
        return PCGX_SEMANTIC_V2_REJECTION_CULL;
    }
    if (g_gx.current_mtx < 0 || g_gx.current_mtx >= 10 ||
        (g_gx.projection_type != GX_PERSPECTIVE &&
         g_gx.projection_type != GX_ORTHOGRAPHIC)) {
        return PCGX_SEMANTIC_V2_REJECTION_MATRIX_PROJECTION;
    }
    if (!pc_gx_v2_channel_state_is_supported((uint32_t)g_gx.num_chans)) {
        return PCGX_SEMANTIC_V2_REJECTION_CHANNEL;
    }
    if (!pc_gx_v2_stage_state_is_supported((uint32_t)g_gx.num_tev_stages)) {
        return PCGX_SEMANTIC_V2_REJECTION_STAGE_TEXTURE;
    }
    for (index = 0; index < (uint32_t)g_gx.num_tex_gens; index++) {
        if (!s_tex_gen_extended_state_known[index] ||
            s_tex_gen_normalize[index] != GX_FALSE ||
            s_tex_gen_post_mtx[index] != GX_PTIDENTITY ||
            g_gx.tex_gen_type[index] != GX_TG_MTX2x4 ||
            g_gx.tex_gen_src[index] != GX_TG_TEX0 ||
            g_gx.tex_gen_mtx[index] != GX_IDENTITY) {
            return PCGX_SEMANTIC_V2_REJECTION_TEXGEN;
        }
    }
    return PCGX_SEMANTIC_V2_REJECTION_SUPPORTED;
}

static PCGXSemanticV2RejectionReason pc_gx_semantic_v2_rejection_reason(
    int first_vertex,
    int vertex_count,
    int expected_vertex_count
) {
    if (first_vertex < 0 || vertex_count != 3 ||
        first_vertex > PC_GX_MAX_VERTS - vertex_count ||
        g_gx.current_vertex_idx < first_vertex + vertex_count ||
        g_gx.in_begin != 0 || g_gx.vertex_pending != 0 ||
        g_gx.expected_vertex_count != expected_vertex_count) {
        return PCGX_SEMANTIC_V2_REJECTION_VERTEX_OR_COUNT;
    }
    return pc_gx_semantic_v2_state_rejection_reason();
}

static int pc_gx_semantic_v2_state_is_supported(void) {
    uint32_t index;

    if (g_gx.num_chans <= 0 ||
        g_gx.num_chans > (int)ACGC_GX_SEMANTIC_MAX_CHANNELS ||
        g_gx.num_tex_gens <= 0 ||
        g_gx.num_tex_gens > (int)ACGC_GX_SEMANTIC_MAX_TEXTURE_GENERATORS ||
        g_gx.num_tev_stages <= 0 ||
        g_gx.num_tev_stages > (int)ACGC_GX_SEMANTIC_MAX_TEV_STAGES ||
        g_gx.num_tex_gens != g_gx.num_tev_stages ||
        g_gx.num_ind_stages != 0 ||
        g_gx.fog_type != GX_FOG_NONE ||
        !pc_gx_v2_alpha_test_state_is_supported() ||
        g_gx.blend_mode != GX_BM_NONE ||
        g_gx.blend_src != GX_BL_ONE ||
        g_gx.blend_dst != GX_BL_ZERO ||
        g_gx.blend_logic_op != GX_LO_CLEAR ||
        g_gx.z_compare_enable == 0 ||
        g_gx.z_compare_func != GX_LEQUAL ||
        g_gx.z_update_enable == 0 ||
        g_gx.color_update_enable == 0 ||
        g_gx.alpha_update_enable == 0 ||
        g_gx.cull_mode != GX_CULL_NONE ||
        g_gx.current_mtx < 0 || g_gx.current_mtx >= 10 ||
        (g_gx.projection_type != GX_PERSPECTIVE &&
         g_gx.projection_type != GX_ORTHOGRAPHIC)) {
        return 0;
    }

    if (!pc_gx_v2_channel_state_is_supported((uint32_t)g_gx.num_chans) ||
        !pc_gx_v2_stage_state_is_supported((uint32_t)g_gx.num_tev_stages)) {
        return 0;
    }
    for (index = 0; index < (uint32_t)g_gx.num_tex_gens; index++) {
        if (!s_tex_gen_extended_state_known[index] ||
            s_tex_gen_normalize[index] != GX_FALSE ||
            s_tex_gen_post_mtx[index] != GX_PTIDENTITY ||
            g_gx.tex_gen_type[index] != GX_TG_MTX2x4 ||
            g_gx.tex_gen_src[index] != GX_TG_TEX0 ||
            g_gx.tex_gen_mtx[index] != GX_IDENTITY) {
            return 0;
        }
    }
    return 1;
}

static int pc_gx_build_semantic_packet_v2_internal(
    int first_vertex,
    int vertex_count,
    int expected_vertex_count,
    AcgcGxSemanticPacketV2* packet
) {
    int vertex_index;
    uint32_t index;

    if (packet == NULL) {
        return 0;
    }
    memset(packet, 0, sizeof(*packet));

    if (first_vertex < 0 || vertex_count != 3 ||
        first_vertex > PC_GX_MAX_VERTS - vertex_count ||
        g_gx.current_vertex_idx < first_vertex + vertex_count ||
        g_gx.in_begin != 0 || g_gx.vertex_pending != 0 ||
        g_gx.expected_vertex_count != expected_vertex_count ||
        !pc_gx_semantic_v2_state_is_supported()) {
        return 0;
    }
    if (!acgc_gx_semantic_packet_v2_init(packet)) {
        return 0;
    }

    packet->base.primitive = ACGC_GX_SEMANTIC_PRIMITIVE_TRIANGLES;
    packet->base.vertex_count = (uint32_t)vertex_count;
    packet->channel_count = (uint32_t)g_gx.num_chans;
    packet->texture_generator_count = (uint32_t)g_gx.num_tex_gens;
    packet->tev_stage_count = (uint32_t)g_gx.num_tev_stages;
    if (!pc_gx_v2_projection_type(&packet->projection_type)) {
        memset(packet, 0, sizeof(*packet));
        return 0;
    }
    packet->state_mask = ACGC_GX_SEMANTIC_V2_STATE_SUPPORTED;

    for (index = 0; index < 16; index++) {
        packet->base.transform.projection[index] = pc_gx_float_bits(
            ((const float*)g_gx.projection_mtx)[index]);
    }
    for (index = 0; index < 12; index++) {
        packet->base.transform.modelview[index] = pc_gx_float_bits(
            ((const float*)g_gx.pos_mtx[g_gx.current_mtx])[index]);
    }
    for (index = 0; index < 9; index++) {
        packet->base.transform.normal[index] = pc_gx_float_bits(
            ((const float*)g_gx.nrm_mtx[g_gx.current_mtx])[index]);
    }

    for (index = 0; index < packet->channel_count; index++) {
        int color = (int)(index * 2);
        AcgcGxSemanticV2Channel* destination = &packet->channels[index];
        int component;

        destination->ambient_source = ACGC_GX_SEMANTIC_V2_CHANNEL_SOURCE_REGISTER;
        destination->material_source = ACGC_GX_SEMANTIC_V2_CHANNEL_SOURCE_REGISTER;
        destination->diffuse_function = ACGC_GX_SEMANTIC_V2_CHANNEL_DIFFUSE_NONE;
        destination->attenuation_function = ACGC_GX_SEMANTIC_V2_CHANNEL_ATTENUATION_NONE;
        for (component = 0; component < 4; component++) {
            destination->ambient_color[component] = pc_gx_float_bits(
                g_gx.chan_amb_color[index][component]);
            destination->material_color[component] = pc_gx_float_bits(
                g_gx.chan_mat_color[index][component]);
        }
        destination->light_mask = (uint32_t)g_gx.chan_ctrl_light_mask[color];
    }

    for (index = 0; index < 12; index++) {
        packet->tev_register_colors[index / 4][index % 4] =
            pc_gx_float_bits(g_gx.tev_colors[1 + index / 4][index % 4]);
    }
    for (index = 0; index < 16; index++) {
        packet->tev_swap_tables[index / 4][index % 4] = (uint32_t)(
            index % 4 == 0 ? g_gx.tev_swap_table[index / 4].r :
            index % 4 == 1 ? g_gx.tev_swap_table[index / 4].g :
            index % 4 == 2 ? g_gx.tev_swap_table[index / 4].b :
            g_gx.tev_swap_table[index / 4].a);
    }

    for (index = 0; index < packet->texture_generator_count; index++) {
        const PCGXTevStage* stage = &g_gx.tev_stages[index];
        AcgcGxSemanticV2TextureGenerator* destination =
            &packet->texture_generators[index];
        int map = stage->tex_map;

        destination->enabled = 1;
        destination->coordinate_index = index;
        destination->function = ACGC_GX_SEMANTIC_V2_TEXGEN_FUNCTION_MTX2X4;
        destination->source = ACGC_GX_SEMANTIC_V2_TEXGEN_SOURCE_TEX0;
        destination->matrix = ACGC_GX_SEMANTIC_V2_TEXGEN_MATRIX_IDENTITY;
        destination->texture_key = (uint32_t)g_gx.gl_textures[map];
        destination->sampler_key = destination->texture_key;
        destination->width = (uint32_t)g_gx.tex_obj_w[map];
        destination->height = (uint32_t)g_gx.tex_obj_h[map];
        destination->format = (uint32_t)g_gx.tex_obj_fmt[map];
        if (pc_gx_v2_texture_format_uses_tlut(g_gx.tex_obj_fmt[map])) {
            /* The resolved GL key identifies the decoded texture plus TLUT. */
            destination->tlut_key = destination->texture_key;
        }
    }

    for (index = 0; index < packet->tev_stage_count; index++) {
        const PCGXTevStage* source = &g_gx.tev_stages[index];
        AcgcGxSemanticV2TevStage* destination = &packet->tev_stages[index];
        uint32_t input_index;

        for (input_index = 0; input_index < 4; input_index++) {
            if (!pc_gx_v2_map_color_input(
                    input_index == 0 ? source->color_a :
                    input_index == 1 ? source->color_b :
                    input_index == 2 ? source->color_c : source->color_d,
                    &destination->color_input[input_index]) ||
                !pc_gx_v2_map_alpha_input(
                    input_index == 0 ? source->alpha_a :
                    input_index == 1 ? source->alpha_b :
                    input_index == 2 ? source->alpha_c : source->alpha_d,
                    &destination->alpha_input[input_index])) {
                memset(packet, 0, sizeof(*packet));
                return 0;
            }
        }
        if (!pc_gx_v2_map_tev_operation(source->color_op, &destination->color_operation) ||
            !pc_gx_v2_map_tev_operation(source->alpha_op, &destination->alpha_operation) ||
            source->color_bias != GX_TB_ZERO || source->alpha_bias != GX_TB_ZERO ||
            source->color_scale != GX_CS_SCALE_1 || source->alpha_scale != GX_CS_SCALE_1 ||
            source->color_out != GX_TEVPREV || source->alpha_out != GX_TEVPREV ||
            !pc_gx_v2_map_kcolor_selector(
                source->k_color_sel, &destination->constant_color_selector) ||
            !pc_gx_v2_map_kalpha_selector(
                source->k_alpha_sel, &destination->constant_alpha_selector)) {
            memset(packet, 0, sizeof(*packet));
            return 0;
        }
        destination->color_bias = ACGC_GX_SEMANTIC_V2_TEV_BIAS_ZERO;
        destination->alpha_bias = ACGC_GX_SEMANTIC_V2_TEV_BIAS_ZERO;
        destination->color_scale = ACGC_GX_SEMANTIC_V2_TEV_SCALE_ONE;
        destination->alpha_scale = ACGC_GX_SEMANTIC_V2_TEV_SCALE_ONE;
        destination->color_clamp = source->color_clamp != 0;
        destination->alpha_clamp = source->alpha_clamp != 0;
        destination->color_output = ACGC_GX_SEMANTIC_V2_TEV_OUTPUT_PREVIOUS;
        destination->alpha_output = ACGC_GX_SEMANTIC_V2_TEV_OUTPUT_PREVIOUS;
        destination->texture_coordinate_index = (uint32_t)source->tex_coord;
        destination->texture_index = (uint32_t)source->tex_map;
        destination->raster_channel_index = 0;
        destination->raster_swap = (uint32_t)source->ras_swap;
        destination->texture_swap = (uint32_t)source->tex_swap;
    }

    for (vertex_index = 0; vertex_index < vertex_count; vertex_index++) {
        const PCGXVertex* source = &g_gx.vertex_buffer[first_vertex + vertex_index];
        AcgcGxSemanticVertex* destination = &packet->base.vertices[vertex_index];
        int component;

        for (component = 0; component < 3; component++) {
            destination->position[component] = pc_gx_float_bits(
                source->position[component]);
            destination->normal[component] = pc_gx_float_bits(
                source->normal[component]);
        }
        destination->color_rgba8 =
            ((uint32_t)source->color0[0] << 24) |
            ((uint32_t)source->color0[1] << 16) |
            ((uint32_t)source->color0[2] << 8) |
            source->color0[3];
        destination->texcoord0[0] = pc_gx_float_bits(source->texcoord[0][0]);
        destination->texcoord0[1] = pc_gx_float_bits(source->texcoord[0][1]);
    }

    if (!acgc_gx_semantic_packet_v2_validate(packet)) {
        memset(packet, 0, sizeof(*packet));
        return 0;
    }
    return 1;
}

static int pc_gx_build_semantic_packet_v2(
    int first_vertex,
    int vertex_count,
    AcgcGxSemanticPacketV2* packet
) {
    return pc_gx_build_semantic_packet_v2_internal(
        first_vertex,
        vertex_count,
        vertex_count,
        packet
    );
}

static int pc_gx_v3_map_blend_mode(int value, uint32_t* output) {
    if (output == NULL) {
        return 0;
    }
    switch (value) {
        case GX_BM_NONE:
            *output = ACGC_GX_SEMANTIC_V3_BLEND_MODE_NONE;
            return 1;
        case GX_BM_BLEND:
            *output = ACGC_GX_SEMANTIC_V3_BLEND_MODE_BLEND;
            return 1;
        case GX_BM_LOGIC:
            *output = ACGC_GX_SEMANTIC_V3_BLEND_MODE_LOGIC;
            return 1;
        case GX_BM_SUBTRACT:
            *output = ACGC_GX_SEMANTIC_V3_BLEND_MODE_SUBTRACT;
            return 1;
        default:
            return 0;
    }
}

static int pc_gx_v3_map_blend_factor(int value, uint32_t* output) {
    if (output == NULL) {
        return 0;
    }
    switch (value) {
        case GX_BL_ZERO:
            *output = ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_ZERO;
            return 1;
        case GX_BL_ONE:
            *output = ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_ONE;
            return 1;
        case GX_BL_SRCCLR:
            *output = ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_SOURCE_COLOR;
            return 1;
        case GX_BL_INVSRCCLR:
            *output = ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_SOURCE_COLOR;
            return 1;
        case GX_BL_SRCALPHA:
            *output = ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_SOURCE_ALPHA;
            return 1;
        case GX_BL_INVSRCALPHA:
            *output = ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_SOURCE_ALPHA;
            return 1;
        case GX_BL_DSTALPHA:
            *output = ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_DEST_ALPHA;
            return 1;
        case GX_BL_INVDSTALPHA:
            *output = ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_DEST_ALPHA;
            return 1;
        default:
            return 0;
    }
}

static int pc_gx_v3_map_logic_op(int value, uint32_t* output) {
    if (output == NULL || value < GX_LO_CLEAR || value > GX_LO_SET) {
        return 0;
    }
    *output = (uint32_t)(value - GX_LO_CLEAR);
    return 1;
}

static int pc_gx_semantic_v3_v4_common_state_is_supported(void) {
    uint32_t index;
    uint32_t ignored;

    if (g_gx.num_chans <= 0 ||
        g_gx.num_chans > (int)ACGC_GX_SEMANTIC_MAX_CHANNELS ||
        g_gx.num_tex_gens <= 0 ||
        g_gx.num_tex_gens > (int)ACGC_GX_SEMANTIC_MAX_TEXTURE_GENERATORS ||
        g_gx.num_tev_stages <= 0 ||
        g_gx.num_tev_stages > (int)ACGC_GX_SEMANTIC_MAX_TEV_STAGES ||
        g_gx.num_tex_gens != g_gx.num_tev_stages ||
        g_gx.num_ind_stages != 0 ||
        g_gx.fog_type != GX_FOG_NONE ||
        g_gx.color_update_enable == 0 ||
        g_gx.current_mtx < 0 || g_gx.current_mtx >= 10 ||
        (g_gx.projection_type != GX_PERSPECTIVE &&
         g_gx.projection_type != GX_ORTHOGRAPHIC) ||
        !pc_gx_v3_map_blend_mode(g_gx.blend_mode, &ignored) ||
        !pc_gx_v3_map_blend_factor(g_gx.blend_src, &ignored) ||
        !pc_gx_v3_map_blend_factor(g_gx.blend_dst, &ignored) ||
        !pc_gx_v3_map_logic_op(g_gx.blend_logic_op, &ignored) ||
        !pc_gx_v2_channel_state_is_supported((uint32_t)g_gx.num_chans) ||
        !pc_gx_v2_stage_state_is_supported((uint32_t)g_gx.num_tev_stages)) {
        return 0;
    }

    for (index = 0; index < (uint32_t)g_gx.num_tex_gens; index++) {
        int matrix_slot;

        if (!s_tex_gen_extended_state_known[index] ||
            s_tex_gen_post_mtx[index] != GX_PTIDENTITY ||
            g_gx.tex_gen_type[index] != GX_TG_MTX2x4 ||
            g_gx.tex_gen_src[index] != GX_TG_TEX0) {
            return 0;
        }
        matrix_slot = pc_tex_mtx_id_to_slot(g_gx.tex_gen_mtx[index]);
        if (matrix_slot < 0 && g_gx.tex_gen_mtx[index] != GX_IDENTITY) {
            return 0;
        }
    }
    return 1;
}

static int pc_gx_semantic_v3_state_is_supported(void) {
    return pc_gx_semantic_v3_v4_common_state_is_supported() &&
        g_gx.alpha_update_enable != GX_FALSE;
}

static int pc_gx_v4_blend_factor_is_supported(int value) {
    return value == GX_BL_ZERO || value == GX_BL_ONE ||
        value == GX_BL_SRCALPHA || value == GX_BL_INVSRCALPHA;
}

/*
 * V4 deliberately leaves the texture/TEV and alpha-test/depth/cull state
 * unencoded in the Apple consumer, but it still needs to carry the live draw
 * far enough to render the bounded vertex-color/blend subset. Keep the V2
 * stage safety checks and require a resolved texture handle, while allowing a
 * valid GX texture map alias instead of imposing the V2 map==stage-index
 * restriction. V1/V2/V3 continue to use the stricter predicate above; the
 * Apple fixture supplies its own bounded depth/raster defaults.
 */
static int pc_gx_v4_stage_state_is_supported(uint32_t stage_count) {
    uint32_t index;

    if (stage_count == 0 ||
        stage_count > ACGC_GX_SEMANTIC_MAX_TEV_STAGES) {
        return 0;
    }
    for (index = 0; index < stage_count; index++) {
        const PCGXTevStage* stage = &g_gx.tev_stages[index];

        if (stage->tex_coord < 0 ||
            stage->tex_coord >= (int)g_gx.num_tex_gens ||
            stage->tex_map < 0 || stage->tex_map >= 8 ||
            stage->color_chan != GX_COLOR0A0 ||
            stage->ind_stage != 0 || stage->ind_format != 0 ||
            stage->ind_bias != 0 || stage->ind_mtx != 0 ||
            stage->ind_wrap_s != 0 || stage->ind_wrap_t != 0 ||
            stage->ind_add_prev != 0 || stage->ind_lod != 0 ||
            stage->ind_alpha != 0 ||
            !pc_gx_v2_texture_is_resolved(stage->tex_map)) {
            return 0;
        }
    }
    return 1;
}

/*
 * V4 forwards only the value packet's vertex colors; it does not carry GX
 * channel-source state.  The decomp's disabled COLOR0A0 setup commonly uses
 * GX_SRC_VTX for the material source (GXInit/JUTResFont), which is harmless
 * while lighting is disabled. Keep enabled/lighted channels fail-closed and
 * leave the stricter V2 predicate unchanged.
 */
static int pc_gx_v4_channel_state_is_supported(uint32_t channel_count) {
    uint32_t index;

    if (channel_count == 0 ||
        channel_count > ACGC_GX_SEMANTIC_MAX_CHANNELS) {
        return 0;
    }
    for (index = 0; index < channel_count; index++) {
        int color = (int)(index * 2);
        int alpha = color + 1;

        if (g_gx.chan_ctrl_enable[color] != 0 ||
            g_gx.chan_ctrl_enable[alpha] != 0 ||
            g_gx.chan_ctrl_amb_src[color] != GX_SRC_REG ||
            g_gx.chan_ctrl_amb_src[alpha] != GX_SRC_REG ||
            (g_gx.chan_ctrl_mat_src[color] != GX_SRC_REG &&
             g_gx.chan_ctrl_mat_src[color] != GX_SRC_VTX) ||
            (g_gx.chan_ctrl_mat_src[alpha] != GX_SRC_REG &&
             g_gx.chan_ctrl_mat_src[alpha] != GX_SRC_VTX) ||
            g_gx.chan_ctrl_light_mask[color] != 0 ||
            g_gx.chan_ctrl_light_mask[alpha] != 0 ||
            g_gx.chan_ctrl_diff_fn[color] != GX_DF_NONE ||
            g_gx.chan_ctrl_diff_fn[alpha] != GX_DF_NONE ||
            g_gx.chan_ctrl_attn_fn[color] != GX_AF_NONE ||
            g_gx.chan_ctrl_attn_fn[alpha] != GX_AF_NONE) {
            return 0;
        }
    }
    return 1;
}

static int pc_gx_semantic_v4_state_is_supported(void) {
    uint32_t index;
    uint32_t ignored;

    if (g_gx.num_chans <= 0 ||
        g_gx.num_chans > (int)ACGC_GX_SEMANTIC_MAX_CHANNELS ||
        g_gx.num_tex_gens <= 0 ||
        g_gx.num_tex_gens > (int)ACGC_GX_SEMANTIC_MAX_TEXTURE_GENERATORS ||
        g_gx.num_tev_stages <= 0 ||
        g_gx.num_tev_stages > (int)ACGC_GX_SEMANTIC_MAX_TEV_STAGES ||
        g_gx.num_tex_gens != g_gx.num_tev_stages ||
        g_gx.num_ind_stages != 0 ||
        g_gx.fog_type != GX_FOG_NONE ||
        g_gx.current_mtx < 0 || g_gx.current_mtx >= 10 ||
        (g_gx.projection_type != GX_PERSPECTIVE &&
         g_gx.projection_type != GX_ORTHOGRAPHIC) ||
        g_gx.color_update_enable == 0 ||
        (g_gx.blend_mode != GX_BM_NONE &&
         g_gx.blend_mode != GX_BM_BLEND) ||
        !pc_gx_v4_blend_factor_is_supported(g_gx.blend_src) ||
        !pc_gx_v4_blend_factor_is_supported(g_gx.blend_dst) ||
        !pc_gx_v3_map_logic_op(g_gx.blend_logic_op, &ignored) ||
        !pc_gx_v4_channel_state_is_supported((uint32_t)g_gx.num_chans) ||
        !pc_gx_v4_stage_state_is_supported((uint32_t)g_gx.num_tev_stages) ||
        (g_gx.alpha_update_enable != GX_FALSE &&
         g_gx.alpha_update_enable != GX_TRUE)) {
        return 0;
    }

    for (index = 0; index < (uint32_t)g_gx.num_tex_gens; index++) {
        int matrix_slot;

        if (!s_tex_gen_extended_state_known[index] ||
            s_tex_gen_post_mtx[index] != GX_PTIDENTITY ||
            g_gx.tex_gen_type[index] != GX_TG_MTX2x4 ||
            g_gx.tex_gen_src[index] != GX_TG_TEX0) {
            return 0;
        }
        matrix_slot = pc_tex_mtx_id_to_slot(g_gx.tex_gen_mtx[index]);
        if (matrix_slot < 0 && g_gx.tex_gen_mtx[index] != GX_IDENTITY) {
            return 0;
        }
    }
    return 1;
}

#ifdef PC_DARWIN_COMPILE_AUDIT
static const char* pc_gx_v4_rejection_reason(void) {
    uint32_t ignored;
    uint32_t index;

    /* Keep this classifier aligned with the V4 predicate: alpha-test,
     * depth, and cull are intentionally unencoded but no longer reject V4. */
    if (g_gx.num_chans <= 0 ||
        g_gx.num_chans > (int)ACGC_GX_SEMANTIC_MAX_CHANNELS ||
        g_gx.num_tex_gens <= 0 ||
        g_gx.num_tex_gens > (int)ACGC_GX_SEMANTIC_MAX_TEXTURE_GENERATORS ||
        g_gx.num_tev_stages <= 0 ||
        g_gx.num_tev_stages > (int)ACGC_GX_SEMANTIC_MAX_TEV_STAGES ||
        g_gx.num_tex_gens != g_gx.num_tev_stages ||
        g_gx.num_ind_stages != 0 ||
        g_gx.fog_type != GX_FOG_NONE ||
        g_gx.color_update_enable == 0 ||
        g_gx.current_mtx < 0 || g_gx.current_mtx >= 10 ||
        (g_gx.projection_type != GX_PERSPECTIVE &&
         g_gx.projection_type != GX_ORTHOGRAPHIC) ||
        (g_gx.alpha_update_enable != GX_FALSE &&
         g_gx.alpha_update_enable != GX_TRUE)) {
        return "global_state";
    }
    if ((g_gx.blend_mode != GX_BM_NONE &&
         g_gx.blend_mode != GX_BM_BLEND) ||
        !pc_gx_v4_blend_factor_is_supported(g_gx.blend_src) ||
        !pc_gx_v4_blend_factor_is_supported(g_gx.blend_dst) ||
        !pc_gx_v3_map_logic_op(g_gx.blend_logic_op, &ignored)) {
        return "blend";
    }
    if (!pc_gx_v4_channel_state_is_supported((uint32_t)g_gx.num_chans)) {
        return "channel";
    }
    if (!pc_gx_v4_stage_state_is_supported((uint32_t)g_gx.num_tev_stages)) {
        return "stage_or_texture";
    }
    for (index = 0; index < (uint32_t)g_gx.num_tex_gens; index++) {
        int matrix_slot;

        if (!s_tex_gen_extended_state_known[index] ||
            s_tex_gen_post_mtx[index] != GX_PTIDENTITY ||
            g_gx.tex_gen_type[index] != GX_TG_MTX2x4 ||
            g_gx.tex_gen_src[index] != GX_TG_TEX0) {
            return "texgen";
        }
        matrix_slot = pc_tex_mtx_id_to_slot(g_gx.tex_gen_mtx[index]);
        if (matrix_slot < 0 && g_gx.tex_gen_mtx[index] != GX_IDENTITY) {
            return "texgen";
        }
    }
    return "payload_or_validation";
}

static void pc_gx_trace_semantic_packet_v4_rejection(
    int first_vertex,
    int vertex_count
) {
    const char* enabled = getenv("ACGC_METAL_V4_REJECTION_TRACE");
    const PCGXTevStage* stage;
    int texture_map;
    GLuint texture_id = 0;
    int texture_width = 0;
    int texture_height = 0;
    int texture_format = 0;

    if (enabled == NULL || enabled[0] == '\0' ||
        s_semantic_packet_v4_trace_count >= 64) {
        return;
    }
    s_semantic_packet_v4_trace_count++;
    stage = &g_gx.tev_stages[0];
    texture_map = stage->tex_map;
    if (texture_map >= 0 && texture_map < 8) {
        texture_id = g_gx.gl_textures[texture_map];
        texture_width = g_gx.tex_obj_w[texture_map];
        texture_height = g_gx.tex_obj_h[texture_map];
        texture_format = g_gx.tex_obj_fmt[texture_map];
    }
    fprintf(
        stderr,
        "[ACGC_V4_REJECT] n=%u first=%d count=%d reason=%s "
        "alpha_update=%d alpha=%d/%d/%d refs=%d/%d "
        "z=%d/%d/%d/%d blend=%d/%d/%d/%d "
        "chans=%d texgens=%d tev=%d ind=%d fog=%d cull=%d mtx=%d proj=%d "
        "stage0=%d/%d/%d resolved=%d tex=%u/%d/%d/%d "
        "texgen0=%d/%d/%d/%d known=%d post=%d\n",
        s_semantic_packet_v4_trace_count,
        first_vertex,
        vertex_count,
        pc_gx_v4_rejection_reason(),
        g_gx.alpha_update_enable,
        g_gx.alpha_comp0,
        g_gx.alpha_comp1,
        g_gx.alpha_op,
        g_gx.alpha_ref0,
        g_gx.alpha_ref1,
        g_gx.z_compare_enable,
        g_gx.z_compare_func,
        g_gx.z_update_enable,
        g_gx.color_update_enable,
        g_gx.blend_mode,
        g_gx.blend_src,
        g_gx.blend_dst,
        g_gx.blend_logic_op,
        g_gx.num_chans,
        g_gx.num_tex_gens,
        g_gx.num_tev_stages,
        g_gx.num_ind_stages,
        g_gx.fog_type,
        g_gx.cull_mode,
        g_gx.current_mtx,
        g_gx.projection_type,
        stage->tex_coord,
        stage->tex_map,
        stage->color_chan,
        pc_gx_v2_texture_is_resolved(texture_map),
        texture_id,
        texture_width,
        texture_height,
        texture_format,
        g_gx.tex_gen_type[0],
        g_gx.tex_gen_src[0],
        g_gx.tex_gen_mtx[0],
        s_tex_gen_normalize[0],
        s_tex_gen_extended_state_known[0],
        s_tex_gen_post_mtx[0]
    );
}
#endif

static void pc_gx_v3_identity_matrix(uint32_t* words) {
    if (words == NULL) {
        return;
    }
    memset(words, 0, sizeof(uint32_t) * 12);
    words[0] = pc_gx_float_bits(1.0f);
    words[5] = pc_gx_float_bits(1.0f);
    words[10] = pc_gx_float_bits(1.0f);
}

static int pc_gx_build_semantic_packet_v3_payload(
    int first_vertex,
    int vertex_count,
    AcgcGxSemanticPacketV3* packet
) {
    AcgcGxSemanticPacket base;
    uint32_t index;

    if (packet == NULL ||
        !pc_gx_build_semantic_packet_internal(
            first_vertex,
            vertex_count,
            &base,
            0
        ) ||
        !acgc_gx_semantic_packet_v3_init(packet)) {
        return 0;
    }

    packet->base = base;
    packet->texture_matrix_count = (uint32_t)g_gx.num_tex_gens;
    if (!pc_gx_v3_map_blend_mode(g_gx.blend_mode, &packet->blend.mode) ||
        !pc_gx_v3_map_blend_factor(
            g_gx.blend_src,
            &packet->blend.source_factor
        ) ||
        !pc_gx_v3_map_blend_factor(
            g_gx.blend_dst,
            &packet->blend.destination_factor
        ) ||
        !pc_gx_v3_map_logic_op(
            g_gx.blend_logic_op,
            &packet->blend.logic_op
        )) {
        memset(packet, 0, sizeof(*packet));
        return 0;
    }

    for (index = 0; index < packet->texture_matrix_count; index++) {
        AcgcGxSemanticV3TextureMatrix* destination =
            &packet->texture_matrices[index];
        int matrix_slot = pc_tex_mtx_id_to_slot(g_gx.tex_gen_mtx[index]);

        destination->generator_index = index;
        destination->matrix_slot = matrix_slot >= 0 ?
            (uint32_t)matrix_slot : ACGC_GX_SEMANTIC_V3_MATRIX_SLOT_NONE;
        destination->normalize = s_tex_gen_normalize[index] != GX_FALSE;
        destination->post_matrix = ACGC_GX_SEMANTIC_V3_POST_MATRIX_IDENTITY;
        if (matrix_slot >= 0) {
            for (uint32_t word = 0; word < 12; word++) {
                destination->matrix[word] = pc_gx_float_bits(
                    g_gx.tex_mtx[matrix_slot][word / 4][word % 4]
                );
            }
        } else {
            pc_gx_v3_identity_matrix(destination->matrix);
        }
    }

    if (!acgc_gx_semantic_packet_v3_validate(packet)) {
        memset(packet, 0, sizeof(*packet));
        return 0;
    }
    return 1;
}

static int pc_gx_build_semantic_packet_v3(
    int first_vertex,
    int vertex_count,
    AcgcGxSemanticPacketV3* packet
) {
    if (!pc_gx_semantic_v3_state_is_supported()) {
        return 0;
    }
    return pc_gx_build_semantic_packet_v3_payload(
        first_vertex,
        vertex_count,
        packet
    );
}

static int pc_gx_build_semantic_packet_v4(
    int first_vertex,
    int vertex_count,
    AcgcGxSemanticPacketV4* packet
) {
    AcgcGxSemanticPacketV3 v3_payload;

    if (packet == NULL) {
        return 0;
    }
    memset(packet, 0, sizeof(*packet));
    if (!pc_gx_semantic_v4_state_is_supported() ||
        !pc_gx_build_semantic_packet_v3_payload(
            first_vertex,
            vertex_count,
            &v3_payload
        ) ||
        !acgc_gx_semantic_packet_v4_init(packet)) {
        return 0;
    }

    /* V4 is the V3 state payload plus one explicit trailing field. */
    memcpy(packet, &v3_payload, sizeof(v3_payload));
    packet->version = ACGC_GX_SEMANTIC_PACKET_V4_VERSION;
    packet->byte_size = ACGC_GX_SEMANTIC_PACKET_V4_SIZE;
    packet->state_mask = ACGC_GX_SEMANTIC_PACKET_V4_STATE_SUPPORTED;
    packet->alpha_update_enable = (uint32_t)g_gx.alpha_update_enable;
    if (!acgc_gx_semantic_packet_v4_validate(packet)) {
        memset(packet, 0, sizeof(*packet));
        return 0;
    }
    return 1;
}

#ifdef PC_DARWIN_COMPILE_AUDIT
/*
 * The current PC callback is a v1-only boundary. Keep this constructor
 * reachable for the focused audit fixture without sending v2 to that
 * callback until a separate version-aware consumer/API is owned and wired.
 */
int pc_gx_build_semantic_packet_v2_fixture(
    int first_vertex,
    int vertex_count,
    AcgcGxSemanticPacketV2* packet
) {
    return pc_gx_build_semantic_packet_v2(first_vertex, vertex_count, packet);
}

const char* pc_gx_semantic_v2_state_rejection_reason_fixture(void) {
    return pc_gx_semantic_v2_rejection_reason_name(
        pc_gx_semantic_v2_state_rejection_reason()
    );
}

int pc_gx_semantic_v2_state_is_supported_fixture(void) {
    return pc_gx_semantic_v2_state_is_supported();
}

const char* pc_gx_semantic_v2_rejection_reason_fixture(
    int first_vertex,
    int vertex_count,
    int expected_vertex_count
) {
    return pc_gx_semantic_v2_rejection_reason_name(
        pc_gx_semantic_v2_rejection_reason(
            first_vertex,
            vertex_count,
            expected_vertex_count
        )
    );
}

int pc_gx_build_semantic_packet_v3_fixture(
    int first_vertex,
    int vertex_count,
    AcgcGxSemanticPacketV3* packet
) {
    return pc_gx_build_semantic_packet_v3(first_vertex, vertex_count, packet);
}

int pc_gx_build_semantic_packet_v4_fixture(
    int first_vertex,
    int vertex_count,
    AcgcGxSemanticPacketV4* packet
) {
    return pc_gx_build_semantic_packet_v4(first_vertex, vertex_count, packet);
}
#endif

int pc_gx_get_v2_texture_source(int map, PCGXTextureSource* destination) {
    const PCGXTextureSource* source;

    if (destination == NULL) {
        return 0;
    }
    memset(destination, 0, sizeof(*destination));
    if (map < 0 || map >= 8) {
        return 0;
    }

    source = &g_gx.texture_sources[map];
    if (source->generation == 0 || source->image_ptr == NULL ||
        source->image_byte_size == 0 || source->width == 0 ||
        source->width > 1024 || source->height == 0 || source->height > 1024 ||
        !pc_gx_v2_texture_format_is_valid((int)source->format) ||
        ((uintptr_t)source->image_ptr & 0x1Fu) != 0 ||
        source->wrap_s > GX_MIRROR || source->wrap_t > GX_MIRROR ||
        source->min_filter > GX_LIN_MIP_LIN ||
        source->mag_filter > GX_LIN_MIP_LIN ||
        source->effective_filter > GX_LIN_MIP_LIN ||
        source->tlut_is_be > 1 ||
        (source->source_kind != PCGX_TEXTURE_SOURCE_RAW_GUEST &&
         source->source_kind != PCGX_TEXTURE_SOURCE_EMU64_CONVERTED)) {
        return 0;
    }
    if (pc_gx_v2_texture_format_uses_tlut((int)source->format)) {
        if (source->tlut_ptr == NULL || source->tlut_byte_size == 0 ||
            source->tlut_entries == 0 || source->tlut_entries > 0x4000 ||
            source->tlut_byte_size != source->tlut_entries * 2u ||
            source->tlut_name >= 16 ||
            (source->tlut_format != GX_TL_IA8 &&
             source->tlut_format != GX_TL_RGB565 &&
             source->tlut_format != GX_TL_RGB5A3) ||
            ((uintptr_t)source->tlut_ptr & 0x1Fu) != 0 ||
            (source->tlut_source_kind != PCGX_TEXTURE_SOURCE_RAW_GUEST &&
             source->tlut_source_kind != PCGX_TEXTURE_SOURCE_EMU64_CONVERTED)) {
            return 0;
        }
    } else if (source->tlut_ptr != NULL || source->tlut_byte_size != 0 ||
               source->tlut_entries != 0 || source->tlut_source_kind !=
                   PCGX_TEXTURE_SOURCE_NONE || source->tlut_name != UINT32_MAX) {
        return 0;
    }

    /* Metadata-only copy: no image or TLUT byte is touched here. */
    *destination = *source;
    return 1;
}

void pc_gx_set_semantic_packet_v2_handoff(
    PCGXSemanticPacketV2HandoffCallback callback,
    void* context
) {
    /* The v2 callback/context pair is borrowed and never crosses the v1 seam. */
    s_semantic_packet_v2_handoff = callback;
    s_semantic_packet_v2_handoff_context = callback != NULL ? context : NULL;
}

void pc_gx_clear_semantic_packet_v2_handoff(void) {
    s_semantic_packet_v2_handoff = NULL;
    s_semantic_packet_v2_handoff_context = NULL;
}

void pc_gx_set_semantic_packet_v3_handoff(
    PCGXSemanticPacketV3HandoffCallback callback,
    void* context
) {
    s_semantic_packet_v3_handoff = callback;
    s_semantic_packet_v3_handoff_context = callback != NULL ? context : NULL;
}

void pc_gx_clear_semantic_packet_v3_handoff(void) {
    s_semantic_packet_v3_handoff = NULL;
    s_semantic_packet_v3_handoff_context = NULL;
}

void pc_gx_set_semantic_packet_v4_handoff(
    PCGXSemanticPacketV4HandoffCallback callback,
    void* context
) {
    s_semantic_packet_v4_handoff = callback;
    s_semantic_packet_v4_handoff_context = callback != NULL ? context : NULL;
}

void pc_gx_clear_semantic_packet_v4_handoff(void) {
    s_semantic_packet_v4_handoff = NULL;
    s_semantic_packet_v4_handoff_context = NULL;
}

void pc_gx_set_semantic_packet_handoff(
    PCGXSemanticPacketHandoffCallback callback,
    void* context
) {
    /* The callback/context pair is borrowed; replacing it never disposes it. */
    s_semantic_packet_handoff = callback;
    s_semantic_packet_handoff_context = callback != NULL ? context : NULL;
}

void pc_gx_clear_semantic_packet_handoff(void) {
    s_semantic_packet_handoff = NULL;
    s_semantic_packet_handoff_context = NULL;
}

#ifdef PC_GX_ALPHA_RAW_SHADOW_FIXTURE
void pc_gx_set_alpha_flush_fixture_observer(
    PCGXAlphaFlushFixtureObserver observer,
    void* context
) {
    s_alpha_flush_fixture_observer = observer;
    s_alpha_flush_fixture_observer_context =
        observer != NULL ? context : NULL;
}

void pc_gx_clear_alpha_flush_fixture_observer(void) {
    s_alpha_flush_fixture_observer = NULL;
    s_alpha_flush_fixture_observer_context = NULL;
}
#endif

#ifdef PC_GX_RASTER_RAW_SHADOW_FIXTURE
void pc_gx_set_raster_flush_fixture_observer(
    PCGXRasterFlushFixtureObserver observer,
    void* context
) {
    s_raster_flush_fixture_observer = observer;
    s_raster_flush_fixture_observer_context =
        observer != NULL ? context : NULL;
}

void pc_gx_clear_raster_flush_fixture_observer(void) {
    s_raster_flush_fixture_observer = NULL;
    s_raster_flush_fixture_observer_context = NULL;
}
#endif

#ifdef PC_GX_DEPTH_RAW_SHADOW_FIXTURE
void pc_gx_set_depth_flush_fixture_observer(
    PCGXDepthFlushFixtureObserver observer,
    void* context
) {
    s_depth_flush_fixture_observer = observer;
    s_depth_flush_fixture_observer_context =
        observer != NULL ? context : NULL;
}

void pc_gx_clear_depth_flush_fixture_observer(void) {
    s_depth_flush_fixture_observer = NULL;
    s_depth_flush_fixture_observer_context = NULL;
}
#endif

#ifdef PC_GX_TEXGEN_RAW_SHADOW_FIXTURE
void pc_gx_set_texgen_flush_fixture_observer(
    PCGXTexgenFlushFixtureObserver observer,
    void* context
) {
    s_texgen_flush_fixture_observer = observer;
    s_texgen_flush_fixture_observer_context =
        observer != NULL ? context : NULL;
}

void pc_gx_clear_texgen_flush_fixture_observer(void) {
    s_texgen_flush_fixture_observer = NULL;
    s_texgen_flush_fixture_observer_context = NULL;
}
#endif

#ifdef PC_GX_GEOMETRY_RAW_BATCH_FIXTURE
void pc_gx_set_geometry_flush_fixture_observer(
    PCGXGeometryFlushFixtureObserver observer,
    void* context
) {
    s_geometry_flush_fixture_observer = observer;
    s_geometry_flush_fixture_observer_context =
        observer != NULL ? context : NULL;
}

void pc_gx_clear_geometry_flush_fixture_observer(void) {
    s_geometry_flush_fixture_observer = NULL;
    s_geometry_flush_fixture_observer_context = NULL;
}
#endif

int pc_gx_try_handoff_semantic_vertices(
    int first_vertex,
    int vertex_count
) {
    AcgcGxSemanticPacket packet_v1;

    if (s_semantic_packet_handoff == NULL) {
        return 0;
    }

    /*
     * This callback's contract is AcgcGxSemanticPacket v1. The existing
     * Apple consumer validates the exact v1 version/size and must not receive
     * a v2 prefix. The v2 constructor remains an audit fixture until a
     * separate version-aware consumer/API exists.
     */
    if (!pc_gx_build_semantic_packet(first_vertex, vertex_count, &packet_v1)) {
        return 0;
    }
    s_semantic_packet_handoff(
        s_semantic_packet_handoff_context,
        &packet_v1
    );
    return 1;
}

int pc_gx_try_handoff_semantic_packet_v2(
    int first_vertex,
    int vertex_count
) {
    AcgcGxSemanticPacketV2 packet;

    if (s_semantic_packet_v2_handoff == NULL) {
        return 0;
    }
    pc_gx_trace_semantic_packet_v2(
        first_vertex,
        vertex_count,
        vertex_count,
        -1
    );
    if (!pc_gx_build_semantic_packet_v2(first_vertex, vertex_count, &packet)) {
        pc_gx_trace_semantic_packet_v2(
            first_vertex,
            vertex_count,
            vertex_count,
            0
        );
        return 0;
    }
    pc_gx_trace_semantic_packet_v2(
        first_vertex,
        vertex_count,
        vertex_count,
        1
    );
    s_semantic_packet_v2_handoff(
        s_semantic_packet_v2_handoff_context,
        &packet
    );
    return 1;
}

/* Split one eligible GX_TRIANGLES list into exact-three V2 packets. Every
 * packet is built and validated before the first callback is allowed to run. */
static int pc_gx_try_handoff_semantic_packet_v2_batch(
    int first_vertex,
    int vertex_count
) {
    AcgcGxSemanticPacketV2* packets;
    size_t triangle_count;
    size_t index;

    if (s_semantic_packet_v2_handoff == NULL) {
        return 0;
    }
    if (
        g_gx.current_primitive != GX_TRIANGLES ||
        vertex_count <= 0 || (vertex_count % 3) != 0 ||
        vertex_count > (int)ACGC_GX_SEMANTIC_MAX_VERTICES ||
        first_vertex < 0 ||
        first_vertex > PC_GX_MAX_VERTS - vertex_count ||
        g_gx.current_vertex_idx < first_vertex + vertex_count ||
        g_gx.current_vertex_idx > PC_GX_MAX_VERTS ||
        g_gx.in_begin != 0 || g_gx.vertex_pending != 0 ||
        g_gx.expected_vertex_count != vertex_count) {
        pc_gx_trace_semantic_packet_v2(
            first_vertex,
            vertex_count,
            vertex_count,
            0
        );
        return 0;
    }

    triangle_count = (size_t)vertex_count / 3u;
    packets = malloc(triangle_count * sizeof(*packets));
    if (packets == NULL) {
        return 0;
    }

    for (index = 0; index < triangle_count; index++) {
        int triangle_first = first_vertex + (int)(index * 3u);

        pc_gx_trace_semantic_packet_v2(
            triangle_first,
            3,
            vertex_count,
            -1
        );
        if (!pc_gx_build_semantic_packet_v2_internal(
                triangle_first,
                3,
                vertex_count,
                &packets[index])) {
            pc_gx_trace_semantic_packet_v2(
                triangle_first,
                3,
                vertex_count,
                0
            );
            free(packets);
            return 0;
        }
        pc_gx_trace_semantic_packet_v2(
            triangle_first,
            3,
            vertex_count,
            1
        );
    }

    for (index = 0; index < triangle_count; index++) {
        s_semantic_packet_v2_handoff(
            s_semantic_packet_v2_handoff_context,
            &packets[index]
        );
    }
    free(packets);
    return 1;
}

int pc_gx_try_handoff_semantic_packet_v3(
    int first_vertex,
    int vertex_count
) {
    AcgcGxSemanticPacketV3 packet;

    if (s_semantic_packet_v3_handoff == NULL) {
        return 0;
    }
    if (!pc_gx_build_semantic_packet_v3(first_vertex, vertex_count, &packet)) {
#ifdef PC_DARWIN_COMPILE_AUDIT
        {
            const char* enabled = getenv("ACGC_METAL_V3_REJECTION_TRACE");
            static unsigned int trace_count;

            if (enabled != NULL && enabled[0] != '\0' && trace_count < 64) {
                trace_count++;
                fprintf(
                    stderr,
                    "[ACGC_V3_REJECT] n=%u first=%d count=%d "
                    "reason=%s alpha_update_enable=%d "
                    "alpha=%d/%d/%d refs=%d/%d "
                    "z=%d/%d/%d/%d blend=%d/%d/%d/%d "
                    "texgen0=%d/%d/%d known=%d post=%d "
                    "chans=%d texgens=%d tev=%d ind=%d fog=%d cull=%d mtx=%d proj=%d\n",
                    trace_count,
                    first_vertex,
                    vertex_count,
                    g_gx.alpha_update_enable == 0
                        ? "alpha_update_disabled"
                        : "other_v3_predicate",
                    g_gx.alpha_update_enable,
                    g_gx.alpha_comp0,
                    g_gx.alpha_comp1,
                    g_gx.alpha_op,
                    g_gx.alpha_ref0,
                    g_gx.alpha_ref1,
                    g_gx.z_compare_enable,
                    g_gx.z_compare_func,
                    g_gx.z_update_enable,
                    g_gx.color_update_enable,
                    g_gx.blend_mode,
                    g_gx.blend_src,
                    g_gx.blend_dst,
                    g_gx.blend_logic_op,
                    g_gx.tex_gen_type[0],
                    g_gx.tex_gen_src[0],
                    g_gx.tex_gen_mtx[0],
                    s_tex_gen_extended_state_known[0],
                    s_tex_gen_post_mtx[0],
                    g_gx.num_chans,
                    g_gx.num_tex_gens,
                    g_gx.num_tev_stages,
                    g_gx.num_ind_stages,
                    g_gx.fog_type,
                    g_gx.cull_mode,
                    g_gx.current_mtx,
                    g_gx.projection_type
                );
            }
        }
#endif
        return 0;
    }
    s_semantic_packet_v3_handoff(
        s_semantic_packet_v3_handoff_context,
        &packet
    );
    return 1;
}

int pc_gx_try_handoff_semantic_packet_v4(
    int first_vertex,
    int vertex_count
) {
    AcgcGxSemanticPacketV4 packet;

    if (s_semantic_packet_v4_handoff == NULL) {
        return 0;
    }
    if (!pc_gx_build_semantic_packet_v4(first_vertex, vertex_count, &packet)) {
#ifdef PC_DARWIN_COMPILE_AUDIT
        pc_gx_trace_semantic_packet_v4_rejection(first_vertex, vertex_count);
#endif
        return 0;
    }
    s_semantic_packet_v4_handoff(
        s_semantic_packet_v4_handoff_context,
        &packet
    );
    return 1;
}

/* Commit pending vertex + flush batch to GL. Used by GXBegin/GXEnd/GXCopyDisp/etc. */
static void pc_gx_commit_pending_and_flush(void) {
    if (!g_gx.in_begin) return;
    if (g_gx.vertex_pending && g_gx.current_vertex_idx < PC_GX_MAX_VERTS) {
        pc_gx_raw_geometry_commit_current();
        g_gx.vertex_buffer[g_gx.current_vertex_idx] = g_gx.current_vertex;
        g_gx.current_vertex_idx++;
        g_gx.vertex_pending = 0;
    }
    g_gx.in_begin = 0;
    if (g_gx.current_vertex_idx > g_gx.pending_verts)
        pc_gx_flush_vertices();
}

/* emu64 omits GXEnd() — flush when expected vertex count is reached so the
 * batch renders with the state it was built with, not subsequent state changes. */
void pc_gx_flush_if_begin_complete(void) {
    if (!g_gx.in_begin || g_gx.expected_vertex_count <= 0) return;

    int submitted = g_gx.current_vertex_idx + (g_gx.vertex_pending ? 1 : 0);
    if (submitted < g_gx.expected_vertex_count) return;

    pc_gx_commit_pending_and_flush();
}

int pc_emu64_frame_cmds = 0;
int pc_emu64_frame_noop_cmds = 0;
int pc_emu64_frame_tri_cmds = 0;
int pc_emu64_frame_vtx_cmds = 0;
int pc_emu64_frame_dl_cmds = 0;
int pc_emu64_frame_cull_visible = 0;
int pc_emu64_frame_cull_rejected = 0;

const PCGXRawAlpha* pc_gx_raw_alpha_shadow_fixture(void) {
    return &g_gx.raw_alpha;
}

const PCGXRawRaster* pc_gx_raw_raster_shadow_fixture(void) {
    return &g_gx.raw_raster;
}

const PCGXRawDepth* pc_gx_raw_depth_shadow_fixture(void) {
    return &g_gx.raw_depth;
}

const PCGXRawBlend* pc_gx_raw_blend_shadow_fixture(void) {
    return &g_gx.raw_blend;
}

const PCGXRawFog* pc_gx_raw_fog_shadow_fixture(void) {
    return &g_gx.raw_fog;
}

const PCGXRawTexgen* pc_gx_raw_texgen_shadow_fixture(void) {
    return &g_gx.raw_texgen;
}

const PCGXRawGeometry* pc_gx_raw_geometry_shadow_fixture(void) {
    return &g_gx.raw_geometry;
}

int pc_gx_raw_texgen_shadow_valid_fixture(void) {
    return pc_gx_raw_texgen_active_state_is_valid();
}

void pc_gx_raw_texgen_shadow_reset_fixture(void) {
    memset(&g_gx.raw_texgen, 0, sizeof(g_gx.raw_texgen));
    memset(s_tex_gen_extended_state_known, 0,
           sizeof(s_tex_gen_extended_state_known));
    memset(s_tex_gen_normalize, 0, sizeof(s_tex_gen_normalize));
    memset(s_tex_gen_post_mtx, 0, sizeof(s_tex_gen_post_mtx));
    pc_gx_raw_texgen_initialize_matrix_ids();
}

void pc_gx_init(void) {
    memset(&g_gx, 0, sizeof(g_gx));
#ifdef PC_GX_CHANNELS_RAW_PRODUCER
    /* Legacy host defaults below do not establish Channels provenance. */
    pc_gx_raw_channels_initialize();
#endif
#ifdef PC_GX_LIGHTING_RAW_PRODUCER
    /* GX light register state begins as a known empty set. */
    pc_gx_raw_lighting_initialize();
#endif
    /* Host convenience identities below are not real GX provenance. */
    memset(&g_gx.raw_transform, 0, sizeof(g_gx.raw_transform));
    /* Legacy host defaults below do not establish canonical Alpha provenance. */
    memset(&g_gx.raw_alpha, 0, sizeof(g_gx.raw_alpha));
    /* Legacy host defaults below do not establish canonical Raster provenance. */
    memset(&g_gx.raw_raster, 0, sizeof(g_gx.raw_raster));
    /* Legacy host defaults below do not establish canonical Depth provenance. */
    memset(&g_gx.raw_depth, 0, sizeof(g_gx.raw_depth));
    /* Legacy host defaults below do not establish canonical Blend provenance. */
    memset(&g_gx.raw_blend, 0, sizeof(g_gx.raw_blend));
    /* Legacy host defaults below do not establish canonical Fog provenance. */
    memset(&g_gx.raw_fog, 0, sizeof(g_gx.raw_fog));
    /* Host texture identities do not establish Texgen/matrix/SU provenance. */
    memset(&g_gx.raw_texgen, 0, sizeof(g_gx.raw_texgen));
    pc_gx_raw_texgen_initialize_matrix_ids();
    /* Raw TEV/KONST values are unavailable until a bounded setter owns them. */
    memset(g_gx.tev_raw_colors, 0, sizeof(g_gx.tev_raw_colors));
    memset(g_gx.tev_raw_k_colors, 0, sizeof(g_gx.tev_raw_k_colors));
    memset(s_tex_gen_extended_state_known, 0, sizeof(s_tex_gen_extended_state_known));
    memset(s_tex_gen_normalize, 0, sizeof(s_tex_gen_normalize));
    memset(s_tex_gen_post_mtx, 0, sizeof(s_tex_gen_post_mtx));

    g_gx.projection_type = GX_PERSPECTIVE;
    g_gx.num_tev_stages = 1;
    g_gx.num_chans = 1;
    g_gx.num_tex_gens = 0;
    g_gx.cull_mode = GX_CULL_NONE;
    g_gx.z_compare_enable = 1;
    g_gx.z_compare_func = GX_LEQUAL;
    g_gx.z_update_enable = 1;
    g_gx.color_update_enable = 1;
    g_gx.alpha_update_enable = 1;
    g_gx.blend_mode = GX_BM_NONE;
    g_gx.blend_src = GX_BL_ONE;
    g_gx.blend_dst = GX_BL_ZERO;
    g_gx.clear_color[3] = 0.0f;
    g_gx.clear_depth = 1.0f;

    for (int i = 0; i < 4; i++)
        g_gx.projection_mtx[i][i] = 1.0f;

    for (int i = 0; i < 10; i++) {
        g_gx.pos_mtx[i][0][0] = 1.0f;
        g_gx.pos_mtx[i][1][1] = 1.0f;
        g_gx.pos_mtx[i][2][2] = 1.0f;
    }

    for (int i = 0; i < 10; i++) {
        g_gx.nrm_mtx[i][0][0] = 1.0f;
        g_gx.nrm_mtx[i][1][1] = 1.0f;
        g_gx.nrm_mtx[i][2][2] = 1.0f;
    }

    g_gx.tev_swap_table[0] = (PCGXTevSwapTable){0, 1, 2, 3};
    g_gx.tev_swap_table[1] = (PCGXTevSwapTable){0, 1, 2, 3};
    g_gx.tev_swap_table[2] = (PCGXTevSwapTable){0, 1, 2, 3};
    g_gx.tev_swap_table[3] = (PCGXTevSwapTable){0, 1, 2, 3};

    for (int i = 0; i < 2; i++) {
        g_gx.chan_mat_color[i][0] = 1.0f;
        g_gx.chan_mat_color[i][1] = 1.0f;
        g_gx.chan_mat_color[i][2] = 1.0f;
        g_gx.chan_mat_color[i][3] = 1.0f;
    }

    /* Quad-to-triangle index buffer */
    for (int q = 0; q < PC_GX_MAX_VERTS / 4; q++) {
        int base = q * 4;
        quad_index_buf[q * 6 + 0] = base + 0;
        quad_index_buf[q * 6 + 1] = base + 1;
        quad_index_buf[q * 6 + 2] = base + 2;
        quad_index_buf[q * 6 + 3] = base + 0;
        quad_index_buf[q * 6 + 4] = base + 2;
        quad_index_buf[q * 6 + 5] = base + 3;
    }

    glGenVertexArrays(1, &g_gx.vao);
    glGenBuffers(1, &g_gx.vbo);
    glGenBuffers(1, &g_gx.ebo);

    /* VAO setup: attrib pointers persist since PCGXVertex layout and VBO ID never change */
    glBindVertexArray(g_gx.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_gx.vbo);
    glBufferData(GL_ARRAY_BUFFER, PC_GX_MAX_VERTS * sizeof(PCGXVertex), NULL, GL_STREAM_DRAW);
    {
        size_t stride = sizeof(PCGXVertex);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(PCGXVertex, position));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(PCGXVertex, normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)offsetof(PCGXVertex, color0));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(PCGXVertex, texcoord));
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gx.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quad_index_buf), quad_index_buf, GL_STATIC_DRAW);

    glBindVertexArray(0);

    pc_gx_tev_init();
    pc_gx_texture_init();
    pc_gx_texture_bind_cache_invalidate();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    g_gx.dirty = PC_GX_DIRTY_ALL;
}

void pc_gx_begin_frame(void) {
    /* Profiler frame opens in graph_main so game_main is captured */
    Uint64 prof_start = pc_profiler_begin_timer();
    pc_emu64_frame_cmds = 0;
    pc_emu64_frame_noop_cmds = 0;
    pc_emu64_frame_tri_cmds = 0;
    pc_emu64_frame_vtx_cmds = 0;
    pc_emu64_frame_dl_cmds = 0;
    pc_emu64_frame_cull_visible = 0;
    pc_emu64_frame_cull_rejected = 0;
    pc_gx_draw_call_count = 0;
    g_pc_widescreen_stretch = 0;
    pc_gx_draw_pending();
    /* glClear respects write masks — must enable all before clearing */
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    /* Masks were changed behind the dirty system; reapply at first flush */
    DIRTY(PC_GX_DIRTY_DEPTH | PC_GX_DIRTY_COLOR_MASK);
#ifdef PC_ENHANCEMENTS
    pc_gx_update_aspect();
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, g_pc_window_w, g_pc_window_h);
    pc_gx_viewport_state_invalidate();
#endif
    glClearDepth(g_gx.clear_depth);
    glClearColor(g_gx.clear_color[0], g_gx.clear_color[1], g_gx.clear_color[2], g_gx.clear_color[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    pc_profiler_add_time(PC_PROF_TIMER_GX_BEGIN, prof_start);
}

void pc_gx_restore_after_nes(void) {
    /* NES emulator uses its own shader/VAO/state. Rebind the game's GL objects
     * and mark all GX state dirty so uniforms/textures get re-uploaded. */
    glBindVertexArray(g_gx.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_gx.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gx.ebo);
    g_gx.dirty = PC_GX_DIRTY_ALL;
    g_gx.current_shader = 0; /* Force shader rebind on next draw */
    pc_gx_texture_bind_cache_invalidate();
    pc_gx_viewport_state_invalidate();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void pc_gx_shutdown(void) {
    /* Do not retain an Apple runtime context after the GX owner goes away. */
    pc_gx_clear_semantic_packet_handoff();
    pc_gx_clear_semantic_packet_v2_handoff();
    pc_gx_tev_shutdown();
    pc_gx_texture_shutdown();
#ifdef PC_ENHANCEMENTS
    pc_gx_efb_capture_cleanup();
#endif

    if (g_gx.ebo) glDeleteBuffers(1, &g_gx.ebo);
    if (g_gx.vbo) glDeleteBuffers(1, &g_gx.vbo);
    if (g_gx.vao) glDeleteVertexArrays(1, &g_gx.vao);
}

/* --- Vertex Submission --- */
void GXBegin(u32 primitive, u32 vtxfmt, u16 nverts) {
    /* Auto-flush previous batch if GXEnd was omitted (normal on real HW) */
    pc_gx_commit_pending_and_flush();

    if (g_gx.pending_verts > 0 && g_gx.pending_verts + (int)nverts > PC_GX_MAX_VERTS)
        pc_gx_draw_pending();

    g_gx.current_primitive = primitive;
    g_gx.current_vtxfmt = vtxfmt;
    g_gx.expected_vertex_count = nverts;
    g_gx.current_vertex_idx = g_gx.pending_verts;
    g_gx.in_begin = 1;
    g_gx.vertex_pending = 0;
    memset(&g_gx.current_vertex, 0, sizeof(PCGXVertex));
    g_gx.current_vertex.color0[0] = 255;
    g_gx.current_vertex.color0[1] = 255;
    g_gx.current_vertex.color0[2] = 255;
    g_gx.current_vertex.color0[3] = 255;
    pc_gx_raw_geometry_begin_batch(primitive, vtxfmt, nverts);
}

void GXEnd(void) {
    pc_gx_commit_pending_and_flush();
}

void GXPosition3f32(f32 x, f32 y, f32 z) {
    uint32_t words[3];

    pc_gx_raw_geometry_begin_position_call();
    memcpy(&words[0], &x, sizeof(words[0]));
    memcpy(&words[1], &y, sizeof(words[1]));
    memcpy(&words[2], &z, sizeof(words[2]));
    pc_gx_raw_geometry_set_scalar_direct(
        GX_VA_POS, GX_F32, 3, words);

    /* Deferred commit: position call commits the previous vertex */
    if (g_gx.vertex_pending && g_gx.current_vertex_idx < PC_GX_MAX_VERTS) {
        g_gx.vertex_buffer[g_gx.current_vertex_idx] = g_gx.current_vertex;
        g_gx.current_vertex_idx++;
    }

    /* Reset vertex but carry last color forward */
    u8 r = g_gx.current_vertex.color0[0];
    u8 g = g_gx.current_vertex.color0[1];
    u8 b = g_gx.current_vertex.color0[2];
    u8 a = g_gx.current_vertex.color0[3];
    memset(&g_gx.current_vertex, 0, sizeof(PCGXVertex));
    g_gx.current_vertex.color0[0] = r;
    g_gx.current_vertex.color0[1] = g;
    g_gx.current_vertex.color0[2] = b;
    g_gx.current_vertex.color0[3] = a;

    g_gx.current_vertex.position[0] = x;
    g_gx.current_vertex.position[1] = y;
    g_gx.current_vertex.position[2] = z;
    g_gx.vertex_pending = 1;
}

static void pc_gx_position_host_begin(float x, float y, float z) {
    /* Deferred commit: position call commits the previous vertex. */
    if (g_gx.vertex_pending && g_gx.current_vertex_idx < PC_GX_MAX_VERTS) {
        g_gx.vertex_buffer[g_gx.current_vertex_idx] = g_gx.current_vertex;
        g_gx.current_vertex_idx++;
    }

    /* Reset vertex but carry last color forward. */
    {
        u8 r = g_gx.current_vertex.color0[0];
        u8 g = g_gx.current_vertex.color0[1];
        u8 b = g_gx.current_vertex.color0[2];
        u8 a = g_gx.current_vertex.color0[3];
        memset(&g_gx.current_vertex, 0, sizeof(PCGXVertex));
        g_gx.current_vertex.color0[0] = r;
        g_gx.current_vertex.color0[1] = g;
        g_gx.current_vertex.color0[2] = b;
        g_gx.current_vertex.color0[3] = a;
    }
    g_gx.current_vertex.position[0] = x;
    g_gx.current_vertex.position[1] = y;
    g_gx.current_vertex.position[2] = z;
    g_gx.vertex_pending = 1;
}

void GXPosition3u16(u16 x, u16 y, u16 z) {
    uint32_t words[3] = {x, y, z};
    pc_gx_raw_geometry_begin_position_call();
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_POS, GX_U16, 3, words);
    pc_gx_position_host_begin((f32)x, (f32)y, (f32)z);
}
void GXPosition3s16(s16 x, s16 y, s16 z) {
    uint32_t words[3] = {
        (uint32_t)(uint16_t)x, (uint32_t)(uint16_t)y, (uint32_t)(uint16_t)z
    };
    pc_gx_raw_geometry_begin_position_call();
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_POS, GX_S16, 3, words);
    pc_gx_position_host_begin((f32)x, (f32)y, (f32)z);
}
void GXPosition3u8(u8 x, u8 y, u8 z) {
    uint32_t words[3] = {x, y, z};
    pc_gx_raw_geometry_begin_position_call();
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_POS, GX_U8, 3, words);
    pc_gx_position_host_begin((f32)x, (f32)y, (f32)z);
}
void GXPosition3s8(s8 x, s8 y, s8 z) {
    uint32_t words[3] = {
        (uint32_t)(uint8_t)x, (uint32_t)(uint8_t)y, (uint32_t)(uint8_t)z
    };
    pc_gx_raw_geometry_begin_position_call();
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_POS, GX_S8, 3, words);
    pc_gx_position_host_begin((f32)x, (f32)y, (f32)z);
}

void GXPosition2f32(f32 x, f32 y) {
    uint32_t words[3];
    float z = 0.0f;
    memcpy(&words[0], &x, sizeof(words[0]));
    memcpy(&words[1], &y, sizeof(words[1]));
    memcpy(&words[2], &z, sizeof(words[2]));
    pc_gx_raw_geometry_begin_position_call();
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_POS, GX_F32, 2, words);
    pc_gx_position_host_begin(x, y, z);
}
void GXPosition2u16(u16 x, u16 y) {
    uint32_t words[2] = {x, y};
    pc_gx_raw_geometry_begin_position_call();
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_POS, GX_U16, 2, words);
    pc_gx_position_host_begin((f32)x, (f32)y, 0.0f);
}
void GXPosition2s16(s16 x, s16 y) {
    uint32_t words[2] = {(uint32_t)(uint16_t)x, (uint32_t)(uint16_t)y};
    pc_gx_raw_geometry_begin_position_call();
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_POS, GX_S16, 2, words);
    pc_gx_position_host_begin((f32)x, (f32)y, 0.0f);
}
void GXPosition2u8(u8 x, u8 y) {
    uint32_t words[2] = {x, y};
    pc_gx_raw_geometry_begin_position_call();
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_POS, GX_U8, 2, words);
    pc_gx_position_host_begin((f32)x, (f32)y, 0.0f);
}
void GXPosition2s8(s8 x, s8 y) {
    uint32_t words[2] = {(uint32_t)(uint8_t)x, (uint32_t)(uint8_t)y};
    pc_gx_raw_geometry_begin_position_call();
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_POS, GX_S8, 2, words);
    pc_gx_position_host_begin((f32)x, (f32)y, 0.0f);
}

void GXPosition1x16(u16 index) {
    f32 pos[3] = {0.0f, 0.0f, 0.0f};
    int raw_known;

    pc_gx_raw_geometry_begin_position_call();
    raw_known = pc_gx_raw_geometry_set_indexed(
        GX_VA_POS, index, GX_INDEX16);
    if (raw_known)
        (void)pc_gx_raw_geometry_host_array_scalar(
            GX_VA_POS, index, pos, 3);
    /* Invalid/mismatched indexes use a zero position fallback, but still
     * enter the existing deferred host position path. */
    pc_gx_position_host_begin(pos[0], pos[1], pos[2]);
}
void GXPosition1x8(u8 index) {
    f32 pos[3] = {0.0f, 0.0f, 0.0f};
    int raw_known;

    pc_gx_raw_geometry_begin_position_call();
    raw_known = pc_gx_raw_geometry_set_indexed(
        GX_VA_POS, index, GX_INDEX8);
    if (raw_known)
        (void)pc_gx_raw_geometry_host_array_scalar(
            GX_VA_POS, index, pos, 3);
    /* Invalid/mismatched indexes use a zero position fallback, but still
     * enter the existing deferred host position path. */
    pc_gx_position_host_begin(pos[0], pos[1], pos[2]);
}

void GXNormal3f32(f32 x, f32 y, f32 z) {
    uint32_t words[3];
    memcpy(&words[0], &x, sizeof(words[0]));
    memcpy(&words[1], &y, sizeof(words[1]));
    memcpy(&words[2], &z, sizeof(words[2]));
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_NRM, GX_F32, 3, words);
    g_gx.current_vertex.normal[0] = x;
    g_gx.current_vertex.normal[1] = y;
    g_gx.current_vertex.normal[2] = z;
}
void GXNormal3s16(s16 x, s16 y, s16 z) {
    uint32_t words[3] = {
        (uint32_t)(uint16_t)x, (uint32_t)(uint16_t)y, (uint32_t)(uint16_t)z
    };
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_NRM, GX_S16, 3, words);
    g_gx.current_vertex.normal[0] = x / 32767.0f;
    g_gx.current_vertex.normal[1] = y / 32767.0f;
    g_gx.current_vertex.normal[2] = z / 32767.0f;
}
void GXNormal3s8(s8 x, s8 y, s8 z) {
    uint32_t words[3] = {
        (uint32_t)(uint8_t)x, (uint32_t)(uint8_t)y, (uint32_t)(uint8_t)z
    };
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_NRM, GX_S8, 3, words);
    g_gx.current_vertex.normal[0] = x / 127.0f;
    g_gx.current_vertex.normal[1] = y / 127.0f;
    g_gx.current_vertex.normal[2] = z / 127.0f;
}
void GXNormal1x16(u16 index) {
    f32 nrm[3];

    if (!pc_gx_raw_geometry_set_indexed(GX_VA_NRM, index, GX_INDEX16))
        return;
    if (pc_gx_raw_geometry_host_array_scalar(
            GX_VA_NRM, index, nrm, 3)) {
        g_gx.current_vertex.normal[0] = nrm[0];
        g_gx.current_vertex.normal[1] = nrm[1];
        g_gx.current_vertex.normal[2] = nrm[2];
    }
}
void GXNormal1x8(u8 index) {
    f32 nrm[3];

    if (!pc_gx_raw_geometry_set_indexed(GX_VA_NRM, index, GX_INDEX8))
        return;
    if (pc_gx_raw_geometry_host_array_scalar(
            GX_VA_NRM, index, nrm, 3)) {
        g_gx.current_vertex.normal[0] = nrm[0];
        g_gx.current_vertex.normal[1] = nrm[1];
        g_gx.current_vertex.normal[2] = nrm[2];
    }
}

void GXColor4u8(u8 r, u8 g, u8 b, u8 a) {
    pc_gx_raw_geometry_set_color_direct_u8(r, g, b, a, 4);
    g_gx.current_vertex.color0[0] = r;
    g_gx.current_vertex.color0[1] = g;
    g_gx.current_vertex.color0[2] = b;
    g_gx.current_vertex.color0[3] = a;
}
void GXColor3u8(u8 r, u8 g, u8 b) {
    pc_gx_raw_geometry_set_color_direct_u8(r, g, b, 0, 3);
    g_gx.current_vertex.color0[0] = r;
    g_gx.current_vertex.color0[1] = g;
    g_gx.current_vertex.color0[2] = b;
    g_gx.current_vertex.color0[3] = 255;
}
void GXColor1u32(u32 clr) {
    pc_gx_raw_geometry_set_color_direct_u32(clr, 4);
    g_gx.current_vertex.color0[0] = (clr >> 24) & 0xFF;
    g_gx.current_vertex.color0[1] = (clr >> 16) & 0xFF;
    g_gx.current_vertex.color0[2] = (clr >> 8) & 0xFF;
    g_gx.current_vertex.color0[3] = clr & 0xFF;
}
void GXColor1u16(u16 clr) {
    pc_gx_raw_geometry_set_color_direct_u16(clr);
    g_gx.current_vertex.color0[0] = (clr >> 8) & 0xFF;
    g_gx.current_vertex.color0[1] = clr & 0xFF;
    g_gx.current_vertex.color0[2] = 0;
    g_gx.current_vertex.color0[3] = 0;
}
void GXColor1x16(u16 index) {
    const uint8_t* source;

    if (!pc_gx_raw_geometry_set_indexed(GX_VA_CLR0, index, GX_INDEX16))
        return;
    source = pc_gx_raw_geometry_host_color_array_element(index);
    if (source != NULL) {
        memcpy(g_gx.current_vertex.color0, source,
               sizeof(g_gx.current_vertex.color0));
    }
}
void GXColor1x8(u8 index) {
    const uint8_t* source;

    if (!pc_gx_raw_geometry_set_indexed(GX_VA_CLR0, index, GX_INDEX8))
        return;
    source = pc_gx_raw_geometry_host_color_array_element(index);
    if (source != NULL) {
        memcpy(g_gx.current_vertex.color0, source,
               sizeof(g_gx.current_vertex.color0));
    }
}

void GXColor4f32(float r, float g, float b, float a) {
    GXColor4u8((u8)(r * 255.0f + 0.5f), (u8)(g * 255.0f + 0.5f),
               (u8)(b * 255.0f + 0.5f), (u8)(a * 255.0f + 0.5f));
}

void GXTexCoord2f32(f32 s, f32 t) {
    uint32_t words[2];
    memcpy(&words[0], &s, sizeof(words[0]));
    memcpy(&words[1], &t, sizeof(words[1]));
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_TEX0, GX_F32, 2, words);
    /* Channel 0 only — emu64 emits one texcoord; multi-tex uses matrix transforms */
    g_gx.current_vertex.texcoord[0][0] = s;
    g_gx.current_vertex.texcoord[0][1] = t;
}
void GXTexCoord2u16(u16 s, u16 t) {
    uint32_t words[2] = {s, t};
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_TEX0, GX_U16, 2, words);
    g_gx.current_vertex.texcoord[0][0] = (f32)s;
    g_gx.current_vertex.texcoord[0][1] = (f32)t;
}
void GXTexCoord2s16(s16 s, s16 t) {
    /* No frac scaling — emu64 already provides pre-scaled texcoords */
    uint32_t words[2] = {(uint32_t)(uint16_t)s, (uint32_t)(uint16_t)t};
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_TEX0, GX_S16, 2, words);
    g_gx.current_vertex.texcoord[0][0] = (f32)s;
    g_gx.current_vertex.texcoord[0][1] = (f32)t;
}
void GXTexCoord2u8(u8 s, u8 t) {
    uint32_t words[2] = {s, t};
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_TEX0, GX_U8, 2, words);
    g_gx.current_vertex.texcoord[0][0] = (f32)s;
    g_gx.current_vertex.texcoord[0][1] = (f32)t;
}
void GXTexCoord2s8(s8 s, s8 t) {
    uint32_t words[2] = {(uint32_t)(uint8_t)s, (uint32_t)(uint8_t)t};
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_TEX0, GX_S8, 2, words);
    g_gx.current_vertex.texcoord[0][0] = (f32)s;
    g_gx.current_vertex.texcoord[0][1] = (f32)t;
}

void GXTexCoord1f32(f32 s, f32 t) {
    uint32_t words[1];
    memcpy(&words[0], &s, sizeof(words[0]));
    /* The PC compatibility ABI retains t for the host path; the GX FIFO
     * one-component form emits only s, so raw T is canonical zero. */
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_TEX0, GX_F32, 1, words);
    g_gx.current_vertex.texcoord[0][0] = s;
    g_gx.current_vertex.texcoord[0][1] = t;
}
void GXTexCoord1u16(u16 s, u16 t) {
    uint32_t words[1] = {s};
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_TEX0, GX_U16, 1, words);
    g_gx.current_vertex.texcoord[0][0] = (f32)s;
    g_gx.current_vertex.texcoord[0][1] = (f32)t;
}
void GXTexCoord1s16(s16 s, s16 t) {
    uint32_t words[1] = {(uint32_t)(uint16_t)s};
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_TEX0, GX_S16, 1, words);
    g_gx.current_vertex.texcoord[0][0] = (f32)s;
    g_gx.current_vertex.texcoord[0][1] = (f32)t;
}
void GXTexCoord1u8(u8 s, u8 t) {
    uint32_t words[1] = {s};
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_TEX0, GX_U8, 1, words);
    g_gx.current_vertex.texcoord[0][0] = (f32)s;
    g_gx.current_vertex.texcoord[0][1] = (f32)t;
}
void GXTexCoord1s8(s8 s, s8 t) {
    uint32_t words[1] = {(uint32_t)(uint8_t)s};
    pc_gx_raw_geometry_set_scalar_direct(GX_VA_TEX0, GX_S8, 1, words);
    g_gx.current_vertex.texcoord[0][0] = (f32)s;
    g_gx.current_vertex.texcoord[0][1] = (f32)t;
}

void GXTexCoord1x16(u16 index) {
    f32 tc[2];

    if (!pc_gx_raw_geometry_set_indexed(GX_VA_TEX0, index, GX_INDEX16))
        return;
    if (pc_gx_raw_geometry_host_array_scalar(
            GX_VA_TEX0, index, tc, 2)) {
        g_gx.current_vertex.texcoord[0][0] = tc[0];
        g_gx.current_vertex.texcoord[0][1] = tc[1];
    }
}
void GXTexCoord1x8(u8 index) {
    f32 tc[2];

    if (!pc_gx_raw_geometry_set_indexed(GX_VA_TEX0, index, GX_INDEX8))
        return;
    if (pc_gx_raw_geometry_host_array_scalar(
            GX_VA_TEX0, index, tc, 2)) {
        g_gx.current_vertex.texcoord[0][0] = tc[0];
        g_gx.current_vertex.texcoord[0][1] = tc[1];
    }
}

/* --- Uniform Location Cache --- */
void pc_gx_cache_uniform_locations(GLuint shader, PCGXUloc* u) {
    char name[48];
    int i;
    #define UL(n) pc_gx_get_uniform_location_profiled(shader, n)

    u->projection = UL("u_projection");
    u->modelview  = UL("u_modelview");
    u->normal_mtx = UL("u_normal_mtx");

    u->tev_prev = UL("u_tev_prev");
    u->tev_reg0 = UL("u_tev_reg0");
    u->tev_reg1 = UL("u_tev_reg1");
    u->tev_reg2 = UL("u_tev_reg2");

    u->num_tev_stages = UL("u_num_tev_stages");
    for (i = 0; i < PC_GX_MAX_TEV_STAGES; i++) {
        snprintf(name, sizeof(name), "u_tev_color_in[%d]", i);
        u->tev_color_in[i] = UL(name);
        snprintf(name, sizeof(name), "u_tev_alpha_in[%d]", i);
        u->tev_alpha_in[i] = UL(name);
        snprintf(name, sizeof(name), "u_tev_color_op[%d]", i);
        u->tev_color_op[i] = UL(name);
        snprintf(name, sizeof(name), "u_tev_alpha_op[%d]", i);
        u->tev_alpha_op[i] = UL(name);
        snprintf(name, sizeof(name), "u_tev_tc_src[%d]", i);
        u->tev_tc_src[i] = UL(name);
        snprintf(name, sizeof(name), "u_tev_ind_cfg[%d]", i);
        u->tev_ind_cfg[i] = UL(name);
        snprintf(name, sizeof(name), "u_tev_ind_wrap[%d]", i);
        u->tev_ind_wrap[i] = UL(name);
    }

    u->kcolor   = UL("u_kcolor");
    u->tev_ksel = UL("u_tev_ksel");

    u->alpha_ctrl = UL("u_alpha_ctrl");
    u->alpha_refs = UL("u_alpha_refs");

    u->lighting_enabled = UL("u_lighting_cfg0");
    u->mat_color  = UL("u_chan_color[0]");
    u->amb_color  = -1;
    u->chan_mat_src = -1;
    u->chan_amb_src = -1;
    u->num_chans  = -1;
    u->alpha_lighting_enabled = UL("u_lighting_cfg1");
    u->alpha_mat_src = -1;

    u->light_mask = -1;
    for (i = 0; i < 8; i++) {
        snprintf(name, sizeof(name), "u_light_pos[%d]", i);
        u->light_pos[i] = UL(name);
        snprintf(name, sizeof(name), "u_light_color[%d]", i);
        u->light_color[i] = UL(name);
    }

    u->texmtx_enable[0] = UL("u_texmtx_enable[0]");
    u->texmtx_row0[0]  = UL("u_texmtx_row0[0]");
    u->texmtx_row1[0]  = UL("u_texmtx_row1[0]");
    u->texgen_src[0]   = UL("u_texgen_src[0]");
    u->texmtx_enable[1] = -1;
    u->texmtx_row0[1]  = -1;
    u->texmtx_row1[1]  = -1;
    u->texgen_src[1]   = -1;

    u->use_texture0 = UL("u_use_texture[0]");
    u->use_texture1 = -1;
    u->use_texture2 = -1;
    u->texture0 = UL("u_texture0");
    u->texture1 = UL("u_texture1");
    u->texture2 = UL("u_texture2");

    u->num_ind_stages = UL("u_num_ind_stages");
    for (i = 0; i < 4; i++) {
        snprintf(name, sizeof(name), "u_ind_tex%d", i);
        u->ind_tex[i] = UL(name);
        snprintf(name, sizeof(name), "u_ind_scale[%d]", i);
        u->ind_scale[i] = UL(name);
    }
    for (i = 0; i < PC_GX_MAX_TEV_STAGES; i++) {
        snprintf(name, sizeof(name), "u_ind_mtx_r0[%d]", i);
        u->ind_mtx_r0[i] = UL(name);
        snprintf(name, sizeof(name), "u_ind_mtx_r1[%d]", i);
        u->ind_mtx_r1[i] = UL(name);
    }

    u->fog_type   = UL("u_fog_params");
    u->fog_enable = UL("u_fog_enable");
    u->fog_start  = -1;
    u->fog_end    = -1;
    u->fog_color  = UL("u_fog_color");

    /* Per-stage bias/scale/clamp/output */
    for (i = 0; i < PC_GX_MAX_TEV_STAGES; i++) {
        snprintf(name, sizeof(name), "u_tev_bsc[%d]", i);
        u->tev_bsc[i] = UL(name);
        snprintf(name, sizeof(name), "u_tev_out[%d]", i);
        u->tev_out[i] = UL(name);
        snprintf(name, sizeof(name), "u_tev_swap[%d]", i);
        u->tev_swap[i] = UL(name);
    }
    u->swap_table = UL("u_swap_table");

    #undef UL
}

/* Uniform groups this program hasn't seen the latest values of */
static unsigned int pc_gx_variant_stale_groups(const PCGXShaderVariant* v) {
    unsigned int stale = 0;
    for (int b = 0; (1u << b) <= PC_GX_DIRTY_UNIFORM_GROUPS; b++) {
        if (v->uploaded_seq[b] != g_gx.group_seq[b]) stale |= (1u << b);
    }
    return stale;
}

/* --- Vertex Flush --- */
int pc_gx_draw_call_count = 0;

/* Draws the deferred run. Must run before any GL call that bypasses the
 * dirty system (viewport, scissor, readpixels, raw texture binds, swap)
 * so the run renders with the state it was built under. */
void pc_gx_draw_pending(void) {
    int count = g_gx.pending_verts;
    if (count == 0) return;

    glBindVertexArray(g_gx.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_gx.vbo);
    pc_gx_buffer_data_profiled(GL_ARRAY_BUFFER, count * sizeof(PCGXVertex), g_gx.vertex_buffer, GL_STREAM_DRAW);

    Uint64 draw_start = pc_profiler_begin_timer();
    if (g_gx.pending_prim == GX_QUADS) {
        int num_indices = (count / 4) * 6;
        glDrawElements(GL_TRIANGLES, num_indices, GL_UNSIGNED_SHORT, 0);
        pc_profiler_add_count_draw(count, num_indices);
        PC_GL_CHECK("glDrawElements");
    } else {
        glDrawArrays(GL_TRIANGLES, 0, count);
        pc_profiler_add_count_draw(count, 0);
        PC_GL_CHECK("glDrawArrays");
    }
    pc_gx_draw_call_count++;
    pc_profiler_add_time(PC_PROF_TIMER_DRAW_SUBMIT, draw_start);

    /* Shift verts committed after the run down to the buffer start */
    int extra = g_gx.current_vertex_idx - count;
    if (extra > 0) {
        memmove(g_gx.vertex_buffer, g_gx.vertex_buffer + count,
                (size_t)extra * sizeof(PCGXVertex));
        g_gx.current_vertex_idx = extra;
    } else {
        g_gx.current_vertex_idx = 0;
    }
    g_gx.pending_verts = 0;
}

void pc_gx_flush_vertices(void) {
    int count = g_gx.current_vertex_idx - g_gx.pending_verts;
    int v2_handoff = 0;

    if (count <= 0) return;

    /* Freeze the pointer-free Geometry provenance before any packet/GL
     * consumer can observe the completed batch or a later setter can mutate
     * VCD/VAT/array state. */
    pc_gx_raw_geometry_capture_completed(count);
#ifdef PC_GX_TEXTURE_DYNAMIC_PRODUCER
    (void)pc_gx_try_texture_dynamic_snapshot();
#endif

#ifdef PC_GX_GEOMETRY_RAW_BATCH_FIXTURE
    /* Observation-only fixture seam immediately before the existing
     * synchronous packet/GL snapshot boundary. The normal flush continues. */
    if (s_geometry_flush_fixture_observer != NULL) {
        s_geometry_flush_fixture_observer(
            s_geometry_flush_fixture_observer_context
        );
    }
#endif

#ifdef PC_GX_ALPHA_RAW_SHADOW_FIXTURE
    /* Observation-only fixture seam immediately before the existing
     * synchronous packet/GL snapshot boundary. The normal flush continues. */
    if (s_alpha_flush_fixture_observer != NULL) {
        s_alpha_flush_fixture_observer(
            s_alpha_flush_fixture_observer_context
        );
    }
#endif

#ifdef PC_GX_RASTER_RAW_SHADOW_FIXTURE
    /* Observation-only fixture seam immediately before the existing
     * synchronous packet/GL snapshot boundary. The normal flush continues. */
    if (s_raster_flush_fixture_observer != NULL) {
        s_raster_flush_fixture_observer(
            s_raster_flush_fixture_observer_context
        );
    }
#endif

#ifdef PC_GX_DEPTH_RAW_SHADOW_FIXTURE
    /* Observation-only fixture seam immediately before the existing
     * synchronous packet/GL snapshot boundary. The normal flush continues. */
    if (s_depth_flush_fixture_observer != NULL) {
        s_depth_flush_fixture_observer(
            s_depth_flush_fixture_observer_context
        );
    }
#endif

#ifdef PC_GX_TEXGEN_RAW_SHADOW_FIXTURE
    /* Observation-only fixture seam immediately before the existing
     * synchronous packet/GL snapshot boundary. The normal flush continues. */
    if (s_texgen_flush_fixture_observer != NULL) {
        s_texgen_flush_fixture_observer(
            s_texgen_flush_fixture_observer_context
        );
    }
#endif

    /*
     * This is the sole optional Apple handoff boundary. The packet is built
     * and delivered synchronously before any GL state mutation; its storage
     * is valid only for the duration of the call. Legacy GL remains the
     * submission path regardless of whether the observer is registered.
     */
    (void)pc_gx_try_handoff_semantic_vertices(g_gx.pending_verts, count);
    if (g_gx.current_primitive == GX_TRIANGLES && count == 3) {
        v2_handoff = pc_gx_try_handoff_semantic_packet_v2(
            g_gx.pending_verts,
            count
        );
    } else if (g_gx.current_primitive == GX_TRIANGLES &&
        count > 3 && (count % 3) == 0 &&
        count <= (int)ACGC_GX_SEMANTIC_MAX_VERTICES &&
        g_gx.pending_verts >= 0 &&
        g_gx.pending_verts <= PC_GX_MAX_VERTS - count) {
        v2_handoff = pc_gx_try_handoff_semantic_packet_v2_batch(
            g_gx.pending_verts,
            count
        );
    }
    if (!v2_handoff) {
        if (!pc_gx_try_handoff_semantic_packet_v3(
                g_gx.pending_verts,
                count
            )) {
            (void)pc_gx_try_handoff_semantic_packet_v4(
                g_gx.pending_verts,
                count
            );
        }
    }

    Uint64 flush_start = pc_profiler_begin_timer();
    pc_profiler_add_count_flush();

    PCGXShaderVariant* var = pc_gx_tev_get_variant();
    GLuint shader = var->prog;
    int prim = g_gx.current_primitive;
    /* Strips/fans would join across batches */
    int deferrable = (prim == GX_TRIANGLES || prim == GX_QUADS);

    /* Same GL state as the deferred run: absorb the verts, no GL work */
    if (g_gx.pending_verts > 0 && g_gx.dirty == 0 && deferrable &&
        prim == g_gx.pending_prim && shader == g_gx.current_shader) {
        g_gx.pending_verts = g_gx.current_vertex_idx;
        pc_profiler_add_time(PC_PROF_TIMER_GX_FLUSH, flush_start);
        return;
    }

    /* State is changing: draw the deferred run while GL state still matches it */
    pc_gx_draw_pending();

    if (shader && shader != g_gx.current_shader) {
        pc_gx_use_program_profiled(shader);
        PC_GL_CHECK("glUseProgram");
        g_gx.current_shader = shader;
        g_gx.uloc = var->uloc;
        /* Uniform values persist per program: re-upload only groups that
         * changed while another program was bound */
        g_gx.dirty |= pc_gx_variant_stale_groups(var);
    }

    glBindVertexArray(g_gx.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_gx.vbo);

    /* Upload only dirty state groups */
    if (shader) {
        Uint64 uniform_start = pc_profiler_begin_timer();
        GLint loc;
        unsigned int dirty = g_gx.dirty;
        pc_profiler_add_dirty_mask(dirty);
        #define UL(field) g_gx.uloc.field

        if (dirty & PC_GX_DIRTY_PROJECTION) {
            loc = UL(projection);
            if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_TRUE, (float*)g_gx.projection_mtx);
        }

        if (dirty & PC_GX_DIRTY_MODELVIEW) {
            loc = UL(modelview);
            if (loc >= 0) {
                float mv44[16];
                const float* src = (const float*)g_gx.pos_mtx[g_gx.current_mtx];
                mv44[ 0] = src[0]; mv44[ 1] = src[1]; mv44[ 2] = src[2]; mv44[ 3] = src[3];
                mv44[ 4] = src[4]; mv44[ 5] = src[5]; mv44[ 6] = src[6]; mv44[ 7] = src[7];
                mv44[ 8] = src[8]; mv44[ 9] = src[9]; mv44[10] = src[10]; mv44[11] = src[11];
                mv44[12] = 0.0f;   mv44[13] = 0.0f;   mv44[14] = 0.0f;    mv44[15] = 1.0f;
                glUniformMatrix4fv(loc, 1, GL_TRUE, mv44);
            }
            loc = UL(normal_mtx);
            if (loc >= 0) glUniformMatrix3fv(loc, 1, GL_TRUE, (const float*)g_gx.nrm_mtx[g_gx.current_mtx]);
        }

        if (dirty & PC_GX_DIRTY_TEV_COLORS) {
            loc = UL(tev_prev); if (loc >= 0) glUniform4fv(loc, 1, g_gx.tev_colors[0]);
            loc = UL(tev_reg0); if (loc >= 0) glUniform4fv(loc, 1, g_gx.tev_colors[1]);
            loc = UL(tev_reg1); if (loc >= 0) glUniform4fv(loc, 1, g_gx.tev_colors[2]);
            loc = UL(tev_reg2); if (loc >= 0) glUniform4fv(loc, 1, g_gx.tev_colors[3]);
        }

        if (dirty & PC_GX_DIRTY_TEV_STAGES) {
            loc = UL(num_tev_stages); if (loc >= 0) glUniform1i(loc, g_gx.num_tev_stages);
            {
                GLint color_in[PC_GX_MAX_TEV_STAGES][4];
                GLint alpha_in[PC_GX_MAX_TEV_STAGES][4];
                GLint color_op[PC_GX_MAX_TEV_STAGES];
                GLint alpha_op[PC_GX_MAX_TEV_STAGES];
                GLint bsc[PC_GX_MAX_TEV_STAGES][4];
                GLint out_cfg[PC_GX_MAX_TEV_STAGES][4];
                GLint swap[PC_GX_MAX_TEV_STAGES][2];
                GLint ksel[PC_GX_MAX_TEV_STAGES][3];
                GLint tc_src[PC_GX_MAX_TEV_STAGES];

                for (int s = 0; s < PC_GX_MAX_TEV_STAGES; s++) {
                    PCGXTevStage* ts = &g_gx.tev_stages[s];
                    color_in[s][0] = ts->color_a;
                    color_in[s][1] = ts->color_b;
                    color_in[s][2] = ts->color_c;
                    color_in[s][3] = ts->color_d;
                    alpha_in[s][0] = ts->alpha_a;
                    alpha_in[s][1] = ts->alpha_b;
                    alpha_in[s][2] = ts->alpha_c;
                    alpha_in[s][3] = ts->alpha_d;
                    color_op[s] = ts->color_op;
                    alpha_op[s] = ts->alpha_op;
                    bsc[s][0] = ts->color_bias;
                    bsc[s][1] = ts->color_scale;
                    bsc[s][2] = ts->alpha_bias;
                    bsc[s][3] = ts->alpha_scale;
                    out_cfg[s][0] = ts->color_clamp;
                    out_cfg[s][1] = ts->alpha_clamp;
                    out_cfg[s][2] = ts->color_out;
                    out_cfg[s][3] = ts->alpha_out;
                    swap[s][0] = ts->ras_swap;
                    swap[s][1] = ts->tex_swap;
                    ksel[s][0] = ts->k_color_sel;
                    ksel[s][1] = ts->k_alpha_sel;
                    ksel[s][2] = s;
                    if (s < g_gx.num_tev_stages) {
                        tc_src[s] = pc_gx_tc_src_normalize(ts->tex_coord, s);
                    } else {
                        tc_src[s] = 0;
                    }
                }

                loc = UL(tev_color_in[0]); if (loc >= 0) glUniform4iv(loc, PC_GX_MAX_TEV_STAGES, &color_in[0][0]);
                loc = UL(tev_alpha_in[0]); if (loc >= 0) glUniform4iv(loc, PC_GX_MAX_TEV_STAGES, &alpha_in[0][0]);
                loc = UL(tev_color_op[0]); if (loc >= 0) glUniform1iv(loc, PC_GX_MAX_TEV_STAGES, color_op);
                loc = UL(tev_alpha_op[0]); if (loc >= 0) glUniform1iv(loc, PC_GX_MAX_TEV_STAGES, alpha_op);
                loc = UL(tev_bsc[0]);      if (loc >= 0) glUniform4iv(loc, PC_GX_MAX_TEV_STAGES, &bsc[0][0]);
                loc = UL(tev_out[0]);      if (loc >= 0) glUniform4iv(loc, PC_GX_MAX_TEV_STAGES, &out_cfg[0][0]);
                loc = UL(tev_swap[0]);     if (loc >= 0) glUniform2iv(loc, PC_GX_MAX_TEV_STAGES, &swap[0][0]);
                loc = UL(tev_ksel);        if (loc >= 0) glUniform3iv(loc, PC_GX_MAX_TEV_STAGES, &ksel[0][0]);
                loc = UL(tev_tc_src[0]);   if (loc >= 0) glUniform1iv(loc, PC_GX_MAX_TEV_STAGES, tc_src);
            }
        }

        if (dirty & PC_GX_DIRTY_SWAP_TABLES) {
            loc = UL(swap_table);
            if (loc >= 0) {
                int sw[16];
                for (int t = 0; t < 4; t++) {
                    sw[t*4+0] = g_gx.tev_swap_table[t].r;
                    sw[t*4+1] = g_gx.tev_swap_table[t].g;
                    sw[t*4+2] = g_gx.tev_swap_table[t].b;
                    sw[t*4+3] = g_gx.tev_swap_table[t].a;
                }
                glUniform4iv(loc, 4, sw);
            }
        }

        if (dirty & PC_GX_DIRTY_KONST) {
            loc = UL(kcolor); if (loc >= 0) glUniform4fv(loc, 4, (const float*)g_gx.tev_k_colors);
        }

        if (dirty & PC_GX_DIRTY_ALPHA_CMP) {
            GLint ctrl[3] = { g_gx.alpha_comp0, g_gx.alpha_op, g_gx.alpha_comp1 };
            GLint refs[2] = { g_gx.alpha_ref0, g_gx.alpha_ref1 };
            loc = UL(alpha_ctrl); if (loc >= 0) glUniform3iv(loc, 1, ctrl);
            loc = UL(alpha_refs); if (loc >= 0) glUniform2iv(loc, 1, refs);
        }

        if (dirty & PC_GX_DIRTY_LIGHTING) {
            GLint lighting_cfg0[4] = {
                g_gx.chan_ctrl_enable[0],
                g_gx.chan_ctrl_mat_src[0],
                g_gx.chan_ctrl_amb_src[0],
                g_gx.num_chans
            };
            GLint lighting_cfg1[4] = {
                g_gx.chan_ctrl_enable[1],
                g_gx.chan_ctrl_mat_src[1],
                g_gx.chan_ctrl_light_mask[0],
                0
            };
            GLfloat chan_color[2][4];

            memcpy(chan_color[0], g_gx.chan_mat_color[0], sizeof(chan_color[0]));
            memcpy(chan_color[1], g_gx.chan_amb_color[0], sizeof(chan_color[1]));

            loc = UL(lighting_enabled); if (loc >= 0) glUniform4iv(loc, 1, lighting_cfg0);
            loc = UL(alpha_lighting_enabled); if (loc >= 0) glUniform4iv(loc, 1, lighting_cfg1);
            loc = UL(mat_color); if (loc >= 0) glUniform4fv(loc, 2, &chan_color[0][0]);
            {
                float lpos[8][3], lcol[8][4];
                for (int i = 0; i < 8; i++) {
                    memcpy(lpos[i], g_gx.lights[i].pos, sizeof(lpos[i]));
                    memcpy(lcol[i], g_gx.lights[i].color, sizeof(lcol[i]));
                }
                loc = UL(light_pos[0]);   if (loc >= 0) glUniform3fv(loc, 8, &lpos[0][0]);
                loc = UL(light_color[0]); if (loc >= 0) glUniform4fv(loc, 8, &lcol[0][0]);
            }
        }

        if (dirty & PC_GX_DIRTY_TEXGEN) {
            GLint texmtx_enable[2];
            GLint texgen_src[2];
            GLfloat texmtx_row0[2][4];
            GLfloat texmtx_row1[2][4];

            for (int tg = 0; tg < 2; tg++) {
                int slot = pc_tex_mtx_id_to_slot(g_gx.tex_gen_mtx[tg]);
                texmtx_enable[tg] = (slot >= 0 && slot < 10);
                texgen_src[tg] = g_gx.tex_gen_src[tg];
                memset(texmtx_row0[tg], 0, sizeof(texmtx_row0[tg]));
                memset(texmtx_row1[tg], 0, sizeof(texmtx_row1[tg]));
                texmtx_row0[tg][0] = 1.0f;
                texmtx_row1[tg][1] = 1.0f;
                if (texmtx_enable[tg]) {
                    const GLfloat* tm = (const GLfloat*)g_gx.tex_mtx[slot];
                    memcpy(texmtx_row0[tg], &tm[0], sizeof(texmtx_row0[tg]));
                    memcpy(texmtx_row1[tg], &tm[4], sizeof(texmtx_row1[tg]));
                }
            }

            loc = g_gx.uloc.texmtx_enable[0]; if (loc >= 0) glUniform1iv(loc, 2, texmtx_enable);
            loc = g_gx.uloc.texmtx_row0[0];   if (loc >= 0) glUniform4fv(loc, 2, &texmtx_row0[0][0]);
            loc = g_gx.uloc.texmtx_row1[0];   if (loc >= 0) glUniform4fv(loc, 2, &texmtx_row1[0][0]);
            loc = g_gx.uloc.texgen_src[0];    if (loc >= 0) glUniform1iv(loc, 2, texgen_src);
        }

        if (dirty & (PC_GX_DIRTY_TEXTURES | PC_GX_DIRTY_TEV_STAGES)) {
            int use_tex_stage[PC_GX_MAX_TEV_STAGES] = { 0 };
            GLuint tex_obj_stage[PC_GX_MAX_TEV_STAGES] = { 0 };
            for (int s = 0; s < PC_GX_MAX_TEV_STAGES; s++) {
                if (s < g_gx.num_tev_stages) {
                    int tex_map = g_gx.tev_stages[s].tex_map;
                    if (tex_map >= 0 && tex_map < 8)
                        tex_obj_stage[s] = g_gx.gl_textures[tex_map];
                }
                if (tex_obj_stage[s] != 0) {
                    use_tex_stage[s] = 1;
                    pc_gx_active_texture_cached(GL_TEXTURE0 + s);
                    pc_gx_bind_texture_profiled(GL_TEXTURE_2D, tex_obj_stage[s]);
                }
            }
            loc = UL(use_texture0); if (loc >= 0) glUniform1iv(loc, PC_GX_MAX_TEV_STAGES, use_tex_stage);
        }

        /* Indirect textures on units 3-6 */
        if (dirty & (PC_GX_DIRTY_INDIRECT | PC_GX_DIRTY_TEXTURES)) {
            for (int i = 0; i < g_gx.num_ind_stages && i < 4; i++) {
                int ind_tex_map = g_gx.ind_order[i].tex_map;
                if (ind_tex_map >= 0 && ind_tex_map < 8) {
                    GLuint ind_tex = g_gx.gl_textures[ind_tex_map];
                    if (ind_tex) {
                        pc_gx_active_texture_cached(GL_TEXTURE3 + i);
                        pc_gx_bind_texture_profiled(GL_TEXTURE_2D, ind_tex);
                    }
                }
            }
        }

        if (dirty & PC_GX_DIRTY_INDIRECT) {
            loc = UL(num_ind_stages); if (loc >= 0) glUniform1i(loc, g_gx.num_ind_stages);
            for (int i = 0; i < g_gx.num_ind_stages && i < 4; i++) {
                loc = UL(ind_scale[i]);
                if (loc >= 0) {
                    float s_scale = 1.0f / (float)(1 << g_gx.ind_order[i].scale_s);
                    float t_scale = 1.0f / (float)(1 << g_gx.ind_order[i].scale_t);
                    glUniform2f(loc, s_scale, t_scale);
                }
            }
            for (int i = 0; i < 3; i++) {
                float scale_val = ldexpf(1.0f, g_gx.ind_mtx_scale[i] + 17) / 1024.0f;
                float packed[6];
                for (int j = 0; j < 6; j++)
                    packed[j] = ((float*)g_gx.ind_mtx[i])[j] * scale_val;
                loc = UL(ind_mtx_r0[i]); if (loc >= 0) glUniform3f(loc, packed[0], packed[1], packed[2]);
                loc = UL(ind_mtx_r1[i]); if (loc >= 0) glUniform3f(loc, packed[3], packed[4], packed[5]);
            }
            for (int s = 0; s < g_gx.num_tev_stages && s < PC_GX_MAX_TEV_STAGES; s++) {
                PCGXTevStage* ts = &g_gx.tev_stages[s];
                loc = UL(tev_ind_cfg[s]);
                if (loc >= 0) glUniform4i(loc, ts->ind_stage, ts->ind_mtx, ts->ind_bias, ts->ind_alpha);
                loc = UL(tev_ind_wrap[s]);
                if (loc >= 0) glUniform3i(loc, ts->ind_wrap_s, ts->ind_wrap_t, ts->ind_add_prev);
            }
        }
        pc_gx_active_texture_cached(GL_TEXTURE0);

        if (dirty & PC_GX_DIRTY_FOG) {
            GLfloat fog_params[4] = {
                (GLfloat)g_gx.fog_type, g_gx.fog_start, g_gx.fog_end, 0.0f
            };
            loc = UL(fog_type);   if (loc >= 0) glUniform4fv(loc, 1, fog_params);
            loc = UL(fog_enable); if (loc >= 0) glUniform1i(loc, g_gx.fog_type != 0);
            loc = UL(fog_color);  if (loc >= 0) glUniform4fv(loc, 1, g_gx.fog_color);
        }

        #undef UL
        pc_profiler_add_time(PC_PROF_TIMER_UNIFORM_UPLOAD, uniform_start);

        /* Record what this program has now seen for stale tracking */
        {
            unsigned int groups = dirty & PC_GX_DIRTY_UNIFORM_GROUPS;
            for (int b = 0; groups; b++, groups >>= 1) {
                if (groups & 1) var->uploaded_seq[b] = g_gx.group_seq[b];
            }
        }
    }

    GLenum gl_prim;
    switch (g_gx.current_primitive) {
        case GX_QUADS:         gl_prim = GL_TRIANGLES; break;
        case GX_TRIANGLES:     gl_prim = GL_TRIANGLES; break;
        case GX_TRIANGLESTRIP: gl_prim = GL_TRIANGLE_STRIP; break;
        case GX_TRIANGLEFAN:   gl_prim = GL_TRIANGLE_FAN; break;
        case GX_LINES:         gl_prim = GL_LINES; break;
        case GX_LINESTRIP:     gl_prim = GL_LINE_STRIP; break;
        case GX_POINTS:        gl_prim = GL_POINTS; break;
        default:               gl_prim = GL_TRIANGLES; break;
    }

    Uint64 state_start = pc_profiler_begin_timer();

    if (g_gx.dirty & PC_GX_DIRTY_DEPTH) {
        pc_profiler_add_count_state_change();
        if (g_gx.z_compare_enable) {
            glEnable(GL_DEPTH_TEST);
            GLenum zfunc;
            switch (g_gx.z_compare_func) {
                case GX_NEVER:   zfunc = GL_NEVER; break;
                case GX_LESS:    zfunc = GL_LESS; break;
                case GX_EQUAL:   zfunc = GL_EQUAL; break;
                case GX_LEQUAL:  zfunc = GL_LEQUAL; break;
                case GX_GREATER: zfunc = GL_GREATER; break;
                case GX_NEQUAL:  zfunc = GL_NOTEQUAL; break;
                case GX_GEQUAL:  zfunc = GL_GEQUAL; break;
                case GX_ALWAYS:  zfunc = GL_ALWAYS; break;
                default:         zfunc = GL_LEQUAL; break;
            }
            glDepthFunc(zfunc);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        glDepthMask(g_gx.z_update_enable ? GL_TRUE : GL_FALSE);
    }

    if (g_gx.dirty & PC_GX_DIRTY_COLOR_MASK) {
        pc_profiler_add_count_state_change();
        glColorMask(
            g_gx.color_update_enable ? GL_TRUE : GL_FALSE,
            g_gx.color_update_enable ? GL_TRUE : GL_FALSE,
            g_gx.color_update_enable ? GL_TRUE : GL_FALSE,
            g_gx.alpha_update_enable ? GL_TRUE : GL_FALSE
        );
    }

    if (g_gx.dirty & PC_GX_DIRTY_CULL) {
        pc_profiler_add_count_state_change();
        switch (g_gx.cull_mode) {
            case GX_CULL_NONE:  glDisable(GL_CULL_FACE); break;
            case GX_CULL_FRONT: glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); break;
            case GX_CULL_BACK:  glEnable(GL_CULL_FACE); glCullFace(GL_BACK); break;
            case GX_CULL_ALL:   glEnable(GL_CULL_FACE); glCullFace(GL_FRONT_AND_BACK); break;
        }
    }

    if (g_gx.dirty & PC_GX_DIRTY_BLEND) {
        pc_profiler_add_count_state_change();
        /* Equation is state-driven: no hidden post-draw reset, so an
         * early-out on unchanged blend state stays correct */
        glBlendEquation(g_gx.blend_mode == GX_BM_SUBTRACT ? GL_FUNC_REVERSE_SUBTRACT
                                                          : GL_FUNC_ADD);
        switch (g_gx.blend_mode) {
            case GX_BM_NONE:
                glDisable(GL_BLEND);
                break;
            case GX_BM_BLEND:
                glEnable(GL_BLEND);
                {
                    GLenum src, dst;
                    switch (g_gx.blend_src) {
                        case GX_BL_ZERO:        src = GL_ZERO; break;
                        case GX_BL_ONE:         src = GL_ONE; break;
                        case GX_BL_DSTCLR:      src = GL_DST_COLOR; break;
                        case GX_BL_INVDSTCLR:   src = GL_ONE_MINUS_DST_COLOR; break;
                        case GX_BL_SRCALPHA:    src = GL_SRC_ALPHA; break;
                        case GX_BL_INVSRCALPHA: src = GL_ONE_MINUS_SRC_ALPHA; break;
                        case GX_BL_DSTALPHA:    src = GL_DST_ALPHA; break;
                        case GX_BL_INVDSTALPHA: src = GL_ONE_MINUS_DST_ALPHA; break;
                        default:                src = GL_ONE; break;
                    }
                    switch (g_gx.blend_dst) {
                        case GX_BL_ZERO:        dst = GL_ZERO; break;
                        case GX_BL_ONE:         dst = GL_ONE; break;
                        case GX_BL_SRCCLR:      dst = GL_SRC_COLOR; break;
                        case GX_BL_INVSRCCLR:   dst = GL_ONE_MINUS_SRC_COLOR; break;
                        case GX_BL_SRCALPHA:    dst = GL_SRC_ALPHA; break;
                        case GX_BL_INVSRCALPHA: dst = GL_ONE_MINUS_SRC_ALPHA; break;
                        case GX_BL_DSTALPHA:    dst = GL_DST_ALPHA; break;
                        case GX_BL_INVDSTALPHA: dst = GL_ONE_MINUS_DST_ALPHA; break;
                        default:                dst = GL_ZERO; break;
                    }
                    /* DST_ALPHA→SRC_ALPHA: GC EFB alpha semantics differ from GL */
                    if (g_gx.blend_src == GX_BL_DSTALPHA && g_gx.blend_dst == GX_BL_INVDSTALPHA) {
                        src = GL_SRC_ALPHA;
                        dst = GL_ONE_MINUS_SRC_ALPHA;
                    }
                    glBlendFunc(src, dst);
                }
                break;
            case GX_BM_LOGIC:
                glDisable(GL_BLEND);
                break;
            case GX_BM_SUBTRACT:
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);
                break;
        }
    }
    pc_profiler_add_time(PC_PROF_TIMER_GL_STATE, state_start);

    g_gx.dirty = 0;

    if (deferrable) {
        /* Defer the draw: the next batch may merge into it */
        g_gx.pending_prim = prim;
        g_gx.pending_verts = count;
        pc_profiler_add_time(PC_PROF_TIMER_GX_FLUSH, flush_start);
        return;
    }

    pc_gx_buffer_data_profiled(GL_ARRAY_BUFFER, count * sizeof(PCGXVertex), g_gx.vertex_buffer, GL_STREAM_DRAW);

    Uint64 draw_start = pc_profiler_begin_timer();
    if (prim == GX_QUADS) {
        int num_quads = count / 4;
        int num_indices = num_quads * 6;
        glDrawElements(GL_TRIANGLES, num_indices, GL_UNSIGNED_SHORT, 0);
        pc_profiler_add_count_draw(count, num_indices);
        PC_GL_CHECK("glDrawElements");
    } else {
        glDrawArrays(gl_prim, 0, count);
        pc_profiler_add_count_draw(count, 0);
        PC_GL_CHECK("glDrawArrays");
    }
    pc_gx_draw_call_count++;
    pc_profiler_add_time(PC_PROF_TIMER_DRAW_SUBMIT, draw_start);

    g_gx.current_vertex_idx = 0;
    pc_profiler_add_time(PC_PROF_TIMER_GX_FLUSH, flush_start);
}

/* --- Vertex Descriptor / Format --- */
void GXSetVtxDesc(u32 attr, u32 type) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_geometry_note_mid_begin_mutation();
    pc_gx_raw_geometry_set_vcd((int)attr, type);
    if (attr < PC_GX_MAX_ATTR) g_gx.vtx_desc[attr] = type;
}
void GXSetVtxDescv(const void* list) {
    const u32* p = (const u32*)list;
    uint32_t count = 0;

    pc_gx_flush_if_begin_complete();
    pc_gx_raw_geometry_note_mid_begin_mutation();
    if (p == NULL) {
        pc_gx_raw_geometry_mark_invalid();
        return;
    }
    while (p[0] != GX_VA_NULL && count++ <= PC_GX_MAX_ATTR) {
        pc_gx_raw_geometry_set_vcd((int)p[0], p[1]);
        if (p[0] < PC_GX_MAX_ATTR) g_gx.vtx_desc[p[0]] = p[1];
        p += 2;
    }
    if (p[0] != GX_VA_NULL) pc_gx_raw_geometry_mark_invalid();
}
void GXClearVtxDesc(void) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_geometry_note_mid_begin_mutation();
    pc_gx_raw_geometry_clear_vcd();
    memset(g_gx.vtx_desc, 0, sizeof(g_gx.vtx_desc));
}

void GXSetVtxAttrFmt(u32 vtxfmt, u32 attr, u32 cnt, u32 type, u8 frac) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_geometry_note_mid_begin_mutation();
    pc_gx_raw_geometry_set_vat(vtxfmt, (int)attr, cnt, type, frac);
    (void)cnt; (void)type;
    if (vtxfmt < GX_MAX_VTXFMT) {
        if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
            int tc = (int)attr - GX_VA_TEX0;
            g_gx.vtx_fmt[vtxfmt].has_texcoord[tc] = 1;
            g_gx.vtx_fmt[vtxfmt].texcoord_frac[tc] = frac;
        }
    }
}

void GXSetArray(u32 attr, const void* data, u32 size, u8 stride) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_geometry_note_mid_begin_mutation();
    pc_gx_raw_geometry_set_array((int)attr, data, size, stride);
    if (attr < GX_VA_MAX_ATTR) {
        g_gx.array_base[attr] = data;
        g_gx.array_size[attr] = size;
        g_gx.array_stride[attr] = stride;
    }
}

void GXInvalidateVtxCache(void) { }

/* --- Transforms --- */
static void pc_gx_apply_projection_host(const void* mtx, u32 type) {
    /* Stored matrix gets aspect-scaled below, so filter on a shadow of the
     * raw input plus everything that feeds the final matrix */
    static float last_in[12];
    static int last_type = -1;
    int same = 0;
#ifdef PC_ENHANCEMENTS
    static int last_stretch = -1, last_aspect_active = -1;
    static float last_aspect_factor;
#endif

    same = (int)type == last_type && memcmp(last_in, mtx, sizeof(last_in)) == 0;
#ifdef PC_ENHANCEMENTS
    same = same && g_pc_widescreen_stretch == last_stretch &&
           g_aspect_active == last_aspect_active &&
           g_aspect_factor == last_aspect_factor;
    last_stretch = g_pc_widescreen_stretch;
    last_aspect_active = g_aspect_active;
    last_aspect_factor = g_aspect_factor;
#endif
    if (same) return;
    memcpy(last_in, mtx, sizeof(last_in));
    last_type = (int)type;

    DIRTY(PC_GX_DIRTY_PROJECTION);
    g_gx.projection_type = type;
    memcpy(g_gx.projection_mtx, mtx, sizeof(float) * 12);
    /* GX only stores 3 rows — 4th row is implicit based on projection type */
    if (type == GX_PERSPECTIVE) {
        g_gx.projection_mtx[3][0] = 0.0f;
        g_gx.projection_mtx[3][1] = 0.0f;
        g_gx.projection_mtx[3][2] = -1.0f;
        g_gx.projection_mtx[3][3] = 0.0f;
    } else { /* GX_ORTHOGRAPHIC */
        g_gx.projection_mtx[3][0] = 0.0f;
        g_gx.projection_mtx[3][1] = 0.0f;
        g_gx.projection_mtx[3][2] = 0.0f;
        g_gx.projection_mtx[3][3] = 1.0f;
    }

#ifdef PC_ENHANCEMENTS
    /* Widescreen: 0=hor+ (both), 1=stretch (none), 2=UI (ortho only) */
    if (g_pc_widescreen_stretch == 0 ||
        (g_pc_widescreen_stretch == 2 && type == GX_ORTHOGRAPHIC)) {
        if (g_aspect_active) {
            g_gx.projection_mtx[0][0] *= g_aspect_factor;
        }
    }
#endif
}

void GXSetProjection(const void* mtx, u32 type) {
    pc_gx_flush_if_begin_complete();
    pc_gx_transform_store_projection(mtx, type);
    pc_gx_apply_projection_host(mtx, type);
}

void GXSetProjectionv(const float* values) {
    uint32_t input[7];
    float coefficients[PC_GX_TRANSFORM_PROJECTION_WORDS];
    float host_projection[4][4];
    u32 type;

    pc_gx_flush_if_begin_complete();
    memcpy(input, values, sizeof(input));
    type = pc_gx_transform_store_projectionv(values);
    if (!pc_gx_transform_projection_type_is_supported(type)) {
        return;
    }

    memcpy(coefficients, &input[1], sizeof(coefficients));
    memset(host_projection, 0, sizeof(host_projection));
    host_projection[0][0] = coefficients[0];
    host_projection[1][1] = coefficients[2];
    host_projection[2][2] = coefficients[4];
    host_projection[2][3] = coefficients[5];
    if (type == GX_ORTHOGRAPHIC) {
        host_projection[0][3] = coefficients[1];
        host_projection[1][3] = coefficients[3];
        host_projection[3][3] = 1.0f;
    } else {
        host_projection[0][2] = coefficients[1];
        host_projection[1][2] = coefficients[3];
        host_projection[3][2] = -1.0f;
    }
    pc_gx_apply_projection_host(host_projection, type);
}

void GXLoadPosMtxImm(const void* mtx, u32 id) {
    pc_gx_flush_if_begin_complete();
    pc_gx_transform_store_position(mtx, id);
    int slot = id / 3;
    if (slot >= 10) return;
    if (memcmp(g_gx.pos_mtx[slot], mtx, sizeof(float) * 12) == 0) return;
    DIRTY(PC_GX_DIRTY_MODELVIEW);
    memcpy(g_gx.pos_mtx[slot], mtx, sizeof(float) * 12);
}

void GXLoadNrmMtxImm(const void* mtx, u32 id) {
    pc_gx_flush_if_begin_complete();
    pc_gx_transform_store_normal_3x4(mtx, id);
    int slot = id / 3;
    if (slot >= 10) return;

    /* Extract upper-left 3x3 from 3x4 row-major Mtx (stride 4, not contiguous) */
    const float* src = (const float*)mtx;
    if (g_gx.nrm_mtx[slot][0][0] == src[0] && g_gx.nrm_mtx[slot][0][1] == src[1] &&
        g_gx.nrm_mtx[slot][0][2] == src[2] && g_gx.nrm_mtx[slot][1][0] == src[4] &&
        g_gx.nrm_mtx[slot][1][1] == src[5] && g_gx.nrm_mtx[slot][1][2] == src[6] &&
        g_gx.nrm_mtx[slot][2][0] == src[8] && g_gx.nrm_mtx[slot][2][1] == src[9] &&
        g_gx.nrm_mtx[slot][2][2] == src[10]) {
        return;
    }

    DIRTY(PC_GX_DIRTY_MODELVIEW);
    g_gx.nrm_mtx[slot][0][0] = src[0]; g_gx.nrm_mtx[slot][0][1] = src[1]; g_gx.nrm_mtx[slot][0][2] = src[2];
    g_gx.nrm_mtx[slot][1][0] = src[4]; g_gx.nrm_mtx[slot][1][1] = src[5]; g_gx.nrm_mtx[slot][1][2] = src[6];
    g_gx.nrm_mtx[slot][2][0] = src[8]; g_gx.nrm_mtx[slot][2][1] = src[9]; g_gx.nrm_mtx[slot][2][2] = src[10];
}

void GXLoadNrmMtxImm3x3(const void* mtx, u32 id) {
    pc_gx_flush_if_begin_complete();
    pc_gx_transform_store_normal_3x3(mtx, id);
    int slot = id / 3;
    if (slot >= 10) return;

    /* Match the existing renderer's element-wise float equality behavior. */
    const float* src = (const float*)mtx;
    if (g_gx.nrm_mtx[slot][0][0] == src[0] &&
        g_gx.nrm_mtx[slot][0][1] == src[1] &&
        g_gx.nrm_mtx[slot][0][2] == src[2] &&
        g_gx.nrm_mtx[slot][1][0] == src[3] &&
        g_gx.nrm_mtx[slot][1][1] == src[4] &&
        g_gx.nrm_mtx[slot][1][2] == src[5] &&
        g_gx.nrm_mtx[slot][2][0] == src[6] &&
        g_gx.nrm_mtx[slot][2][1] == src[7] &&
        g_gx.nrm_mtx[slot][2][2] == src[8]) {
        return;
    }

    DIRTY(PC_GX_DIRTY_MODELVIEW);
    memcpy(g_gx.nrm_mtx[slot], mtx, sizeof(float) * 9);
}

void GXLoadTexMtxImm(const void* mtx, u32 id, u32 type) {
    pc_gx_flush_if_begin_complete();
    /* Capture after the old complete batch has crossed the snapshot point. */
    pc_gx_raw_texgen_matrix_store_immediate(mtx, id, type);
    if (mtx == NULL) return;
    int slot = pc_tex_mtx_id_to_slot((int)id);
    if (slot < 0 || slot >= 10) return;
    /* Preserve the existing host renderer's unconditional 3x4 mirror copy;
     * the raw shadow above retains the GX command's actual 2x4/3x4 range. */
    if (memcmp(g_gx.tex_mtx[slot], mtx, sizeof(float) * 12u) == 0) return;
    DIRTY(PC_GX_DIRTY_TEXGEN);
    memcpy(g_gx.tex_mtx[slot], mtx, sizeof(float) * 12u);
}

void GXLoadTexMtxIndx(u16 mtx_indx, u32 id, u32 type) {
    pc_gx_flush_if_begin_complete();
    /* The PC port has no guest-memory owner for indexed matrix data.  Keep
     * the target provenance and clear only the attempted logical range. */
    pc_gx_raw_texgen_matrix_mark_unresolved(mtx_indx, id, type);
}

void GXSetCurrentMtx(u32 id) {
    pc_gx_flush_if_begin_complete();
    pc_gx_transform_store_current(id);
    u32 slot = id / 3;
    if (slot >= 10 || g_gx.current_mtx == (int)slot) return;
    DIRTY(PC_GX_DIRTY_MODELVIEW);
    g_gx.current_mtx = slot;
}

void GXLoadPosMtxIndx(u16 mtx_indx, u32 id) {
    (void)mtx_indx;
    pc_gx_flush_if_begin_complete();
    pc_gx_transform_mark_indexed_unknown(0, id);
}

void GXLoadNrmMtxIndx3x3(u16 mtx_indx, u32 id) {
    (void)mtx_indx;
    pc_gx_flush_if_begin_complete();
    pc_gx_transform_mark_indexed_unknown(1, id);
}

/* Last GL viewport/scissor actually applied. J2D setPort re-sends both every
 * frame with unchanged values; comparing applied GL state (which folds in
 * window size and aspect mode) lets those calls skip the batch drain.
 * Invalidated wherever raw GL calls bypass the setters. */
static struct { int valid, x, y, w, h; double n, f; } s_gl_viewport;
static struct { int valid, x, y, w, h; } s_gl_scissor;

void pc_gx_viewport_state_invalidate(void) {
    s_gl_viewport.valid = 0;
    s_gl_scissor.valid = 0;
}

void GXSetViewport(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz) {
    int gl_x, gl_y, gl_w, gl_h;

    pc_gx_flush_if_begin_complete();
    pc_gx_raw_raster_store_viewport(left, top, wd, ht, nearz, farz);
    g_gx.viewport[0] = left;
    g_gx.viewport[1] = top;
    g_gx.viewport[2] = wd;
    g_gx.viewport[3] = ht;
    g_gx.viewport[4] = nearz;
    g_gx.viewport[5] = farz;
#ifdef PC_ENHANCEMENTS
    {
        float sx = (float)g_pc_window_w / (float)PC_GC_WIDTH;
        float sy = (float)g_pc_window_h / (float)PC_GC_HEIGHT;
        float adj_left = left;
        float adj_wd = wd;

        /* UI mode: remap sub-viewports to match aspect-corrected content */
        if (g_pc_widescreen_stretch == 2 && g_aspect_active) {
            int is_full = (left < 1.0f && top < 1.0f &&
                           wd > (float)(PC_GC_WIDTH - 1) &&
                           ht > (float)(PC_GC_HEIGHT - 1));
            if (!is_full) {
                adj_left = g_aspect_offset + left * g_aspect_factor;
                adj_wd = wd * g_aspect_factor;
            }
        }

        gl_x = (int)(adj_left * sx);
        gl_w = (int)(adj_wd * sx);
        gl_h = (int)(ht * sy);
        gl_y = g_pc_window_h - (int)(top * sy) - gl_h;
    }
#else
    /* GX is Y-down, GL is Y-up */
    gl_x = (int)left;
    gl_w = (int)wd;
    gl_h = (int)ht;
    gl_y = PC_GC_HEIGHT - (int)top - gl_h;
#endif

    if (s_gl_viewport.valid && gl_x == s_gl_viewport.x && gl_y == s_gl_viewport.y &&
        gl_w == s_gl_viewport.w && gl_h == s_gl_viewport.h &&
        (double)nearz == s_gl_viewport.n && (double)farz == s_gl_viewport.f)
        return;

    pc_gx_draw_pending(); /* glViewport is not dirty-tracked */
    glViewport(gl_x, gl_y, gl_w, gl_h);
    glDepthRange((double)nearz, (double)farz);
    s_gl_viewport.valid = 1;
    s_gl_viewport.x = gl_x;
    s_gl_viewport.y = gl_y;
    s_gl_viewport.w = gl_w;
    s_gl_viewport.h = gl_h;
    s_gl_viewport.n = (double)nearz;
    s_gl_viewport.f = (double)farz;
}

void GXSetViewportJitter(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz, u32 field) {
    if (field == 0) top -= 0.5f;
    GXSetViewport(left, top, wd, ht, nearz, farz);
}

void GXSetScissor(u32 left, u32 top, u32 wd, u32 ht) {
    int gl_x, gl_y, gl_w, gl_h;

    pc_gx_flush_if_begin_complete();
    pc_gx_raw_raster_store_scissor(left, top, wd, ht);
    g_gx.scissor[0] = left;
    g_gx.scissor[1] = top;
    g_gx.scissor[2] = wd;
    g_gx.scissor[3] = ht;
#ifdef PC_ENHANCEMENTS
    {
        float sx = (float)g_pc_window_w / (float)PC_GC_WIDTH;
        float sy = (float)g_pc_window_h / (float)PC_GC_HEIGHT;
        gl_x = (int)(left * sx);
        gl_w = (int)(wd * sx);
        gl_h = (int)(ht * sy);
        gl_y = g_pc_window_h - (int)(top * sy) - gl_h;
    }
#else
    /* GX is Y-down, GL is Y-up */
    gl_x = (int)left;
    gl_w = (int)wd;
    gl_h = (int)ht;
    gl_y = PC_GC_HEIGHT - (int)top - (int)ht;
#endif

    /* Cache validity implies GL_SCISSOR_TEST is enabled */
    if (s_gl_scissor.valid && gl_x == s_gl_scissor.x && gl_y == s_gl_scissor.y &&
        gl_w == s_gl_scissor.w && gl_h == s_gl_scissor.h)
        return;

    pc_gx_draw_pending(); /* glScissor is not dirty-tracked */
    glEnable(GL_SCISSOR_TEST);
    glScissor(gl_x, gl_y, gl_w, gl_h);
    s_gl_scissor.valid = 1;
    s_gl_scissor.x = gl_x;
    s_gl_scissor.y = gl_y;
    s_gl_scissor.w = gl_w;
    s_gl_scissor.h = gl_h;
}

void GXSetScissorBoxOffset(s32 x, s32 y) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_raster_store_scissor_offset(x, y);
}
void GXSetClipMode(u32 mode) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_raster_store_bounded(
        mode,
        ACGC_GX_CANONICAL_RASTER_CLIP_MODE_ENABLE,
        ACGC_GX_CANONICAL_RASTER_CLIP_MODE_DISABLE,
        PC_GX_RAW_RASTER_KNOWN_CLIP_MODE,
        &g_gx.raw_raster.value.clip_mode
    );
}

void GXGetProjectionv(f32* p) {
    if (p) memcpy(p, g_gx.projection_mtx, sizeof(float) * 16);
}

void GXGetVtxAttrFmt(u32 idx, u32 attr, u32* compCnt, u32* compType, u8* shift) {
    if (compCnt) *compCnt = 0;
    if (compType) *compType = 0;
    if (shift) *shift = 0;
}

/* --- TEV Configuration --- */
void GXSetNumTevStages(u8 nStages) {
    pc_gx_flush_if_begin_complete();
    if (!pc_gx_raw_tev_set_num_stages(nStages)) return;
    if (g_gx.num_tev_stages == nStages) return;
    DIRTY(PC_GX_DIRTY_TEV_STAGES);
    g_gx.num_tev_stages = nStages;
}

void GXSetTevOp(u32 stage, u32 mode) {
    pc_gx_flush_if_begin_complete();
    if (!pc_gx_raw_tev_set_op(stage, mode)) return;

    /* TEV formula: out = (d + ((1-c)*a + c*b) + bias) * scale */
    switch (mode) {
    case GX_MODULATE:
        GXSetTevColorIn(stage, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
        GXSetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
        break;
    case GX_DECAL:
        GXSetTevColorIn(stage, GX_CC_RASC, GX_CC_TEXC, GX_CC_TEXA, GX_CC_ZERO);
        GXSetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
        break;
    case GX_BLEND:
        GXSetTevColorIn(stage, GX_CC_ONE, GX_CC_RASC, GX_CC_TEXC, GX_CC_ZERO);
        GXSetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
        break;
    case GX_REPLACE:
        GXSetTevColorIn(stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
        GXSetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
        break;
    case GX_PASSCLR:
        GXSetTevColorIn(stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
        GXSetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
        break;
    default:
        return;
    }
    GXSetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    /* The nested legacy setters intentionally keep the PC expansion.  Restore
     * the source-faithful guest expansion in the sideband afterward. */
    (void)pc_gx_raw_tev_set_op(stage, mode);
}

void GXSetTevColorIn(u32 stage, u32 a, u32 b, u32 c, u32 d) {
    pc_gx_flush_if_begin_complete();
    if (!pc_gx_raw_tev_stage_set_color_in(stage, a, b, c, d)) return;
    if (stage < 16) {
        PCGXTevStage* ts = &g_gx.tev_stages[stage];
        if (ts->color_a == (int)a && ts->color_b == (int)b &&
            ts->color_c == (int)c && ts->color_d == (int)d) return;
        DIRTY(PC_GX_DIRTY_TEV_STAGES);
        ts->color_a = a;
        ts->color_b = b;
        ts->color_c = c;
        ts->color_d = d;
    }
}

void GXSetTevAlphaIn(u32 stage, u32 a, u32 b, u32 c, u32 d) {
    pc_gx_flush_if_begin_complete();
    if (!pc_gx_raw_tev_stage_set_alpha_in(stage, a, b, c, d)) return;
    if (stage < 16) {
        PCGXTevStage* ts = &g_gx.tev_stages[stage];
        if (ts->alpha_a == (int)a && ts->alpha_b == (int)b &&
            ts->alpha_c == (int)c && ts->alpha_d == (int)d) return;
        DIRTY(PC_GX_DIRTY_TEV_STAGES);
        ts->alpha_a = a;
        ts->alpha_b = b;
        ts->alpha_c = c;
        ts->alpha_d = d;
    }
}

void GXSetTevColorOp(u32 stage, u32 op, u32 bias, u32 scale, GXBool clamp, u32 out_reg) {
    pc_gx_flush_if_begin_complete();
    if (!pc_gx_raw_tev_stage_set_color_op(
        stage, op, bias, scale, clamp, out_reg
    )) return;
    if (stage < 16) {
        PCGXTevStage* ts = &g_gx.tev_stages[stage];
        if (ts->color_op == (int)op && ts->color_bias == (int)bias &&
            ts->color_scale == (int)scale && ts->color_clamp == (int)clamp &&
            ts->color_out == (int)out_reg) return;
        DIRTY(PC_GX_DIRTY_TEV_STAGES);
        ts->color_op = op;
        ts->color_bias = bias;
        ts->color_scale = scale;
        ts->color_clamp = clamp;
        ts->color_out = out_reg;
    }
}

void GXSetTevAlphaOp(u32 stage, u32 op, u32 bias, u32 scale, GXBool clamp, u32 out_reg) {
    pc_gx_flush_if_begin_complete();
    if (!pc_gx_raw_tev_stage_set_alpha_op(
        stage, op, bias, scale, clamp, out_reg
    )) return;
    if (stage < 16) {
        PCGXTevStage* ts = &g_gx.tev_stages[stage];
        if (ts->alpha_op == (int)op && ts->alpha_bias == (int)bias &&
            ts->alpha_scale == (int)scale && ts->alpha_clamp == (int)clamp &&
            ts->alpha_out == (int)out_reg) return;
        DIRTY(PC_GX_DIRTY_TEV_STAGES);
        ts->alpha_op = op;
        ts->alpha_bias = bias;
        ts->alpha_scale = scale;
        ts->alpha_clamp = clamp;
        ts->alpha_out = out_reg;
    }
}

void GXSetTevOrder(u32 stage, u32 coord, u32 map, u32 color) {
    pc_gx_flush_if_begin_complete();
    if (!pc_gx_raw_tev_stage_set_order(stage, coord, map, color)) return;
    if (stage < 16) {
        PCGXTevStage* ts = &g_gx.tev_stages[stage];
        if (ts->tex_coord == (int)coord && ts->tex_map == (int)map &&
            ts->color_chan == (int)color) return;
        DIRTY(PC_GX_DIRTY_TEV_STAGES | PC_GX_DIRTY_TEXTURES);
        ts->tex_coord = coord;
        ts->tex_map = map;
        ts->color_chan = color;
    }
}

void GXSetTevColor(u32 id, u32 color_packed) {
    pc_gx_flush_if_begin_complete();
    if (id >= ACGC_GX_CANONICAL_TEV_REGISTER_COUNT) {
        (void)pc_gx_raw_tev_set_color(
            id, NULL, 1, PCGX_TEV_RAW_SOURCE_COLOR_U8
        );
        return;
    }
    /* TEVREG0 uses GXColor fields (byte unpack), others come from EmuColor.raw (shift unpack) */
    if (id < GX_MAX_TEVREG) {
        float c[4];
        int32_t raw[4];
        if (id == GX_TEVREG0) {
            pc_unpack_gxcolor_f(color_packed, c);
            pc_unpack_gxcolor_raw(color_packed, raw);
        } else {
            pc_unpack_rgba8f(color_packed, c);
            pc_unpack_rgba8_raw(color_packed, raw);
        }
        if (!pc_gx_raw_tev_set_color(
            id, raw, 1, PCGX_TEV_RAW_SOURCE_COLOR_U8
        )) return;
        pc_gx_tev_raw_store(
            &g_gx.tev_raw_colors[id],
            raw,
            1,
            PCGX_TEV_RAW_SOURCE_COLOR_U8
        );
        if (memcmp(g_gx.tev_colors[id], c, sizeof(c)) == 0) return;
        DIRTY(PC_GX_DIRTY_TEV_COLORS);
        memcpy(g_gx.tev_colors[id], c, sizeof(c));
    }
}

void GXSetTevColorS10(u32 id, s16 r, s16 g, s16 b, s16 a) {
    pc_gx_flush_if_begin_complete();
    if (id >= ACGC_GX_CANONICAL_TEV_REGISTER_COUNT) {
        (void)pc_gx_raw_tev_set_color(
            id, NULL, 0, PCGX_TEV_RAW_SOURCE_MALFORMED
        );
        return;
    }
    if (id < GX_MAX_TEVREG) {
        int32_t raw[4] = { r, g, b, a };
        int valid = r >= -1024 && r <= 1023 &&
            g >= -1024 && g <= 1023 &&
            b >= -1024 && b <= 1023 &&
            a >= -1024 && a <= 1023;
        float c[4] = { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
        if (!pc_gx_raw_tev_set_color(
            id,
            raw,
            valid,
            valid ? PCGX_TEV_RAW_SOURCE_COLOR_S10 :
                PCGX_TEV_RAW_SOURCE_MALFORMED
        )) return;
        pc_gx_tev_raw_store(
            &g_gx.tev_raw_colors[id],
            raw,
            valid,
            valid ? PCGX_TEV_RAW_SOURCE_COLOR_S10 :
                PCGX_TEV_RAW_SOURCE_MALFORMED
        );
        if (memcmp(g_gx.tev_colors[id], c, sizeof(c)) == 0) return;
        DIRTY(PC_GX_DIRTY_TEV_COLORS);
        memcpy(g_gx.tev_colors[id], c, sizeof(c));
    }
}

void GXSetTevKColor(u32 id, u32 color_packed) {
    pc_gx_flush_if_begin_complete();
    if (id >= ACGC_GX_CANONICAL_TEV_KONST_COUNT) {
        (void)pc_gx_raw_tev_set_k_color(id, NULL);
        return;
    }
    if (id < GX_MAX_KCOLOR) {
        float c[4];
        int32_t raw[4];
        pc_unpack_rgba8f(color_packed, c);
        pc_unpack_rgba8_raw(color_packed, raw);
        if (!pc_gx_raw_tev_set_k_color(id, raw)) return;
        pc_gx_tev_raw_store(
            &g_gx.tev_raw_k_colors[id],
            raw,
            1,
            PCGX_TEV_RAW_SOURCE_KCOLOR_U8
        );
        if (memcmp(g_gx.tev_k_colors[id], c, sizeof(c)) == 0) return;
        DIRTY(PC_GX_DIRTY_KONST);
        memcpy(g_gx.tev_k_colors[id], c, sizeof(c));
    }
}

void GXSetTevKColorSel(u32 stage, u32 sel) {
    pc_gx_flush_if_begin_complete();
    if (!pc_gx_raw_tev_stage_set_k_color(stage, sel)) return;
    if (stage < 16 && g_gx.tev_stages[stage].k_color_sel != (int)sel) {
        DIRTY(PC_GX_DIRTY_TEV_STAGES);
        g_gx.tev_stages[stage].k_color_sel = sel;
    }
}
void GXSetTevKAlphaSel(u32 stage, u32 sel) {
    pc_gx_flush_if_begin_complete();
    if (!pc_gx_raw_tev_stage_set_k_alpha(stage, sel)) return;
    if (stage < 16 && g_gx.tev_stages[stage].k_alpha_sel != (int)sel) {
        DIRTY(PC_GX_DIRTY_TEV_STAGES);
        g_gx.tev_stages[stage].k_alpha_sel = sel;
    }
}

void GXSetTevSwapMode(u32 stage, u32 ras_sel, u32 tex_sel) {
    pc_gx_flush_if_begin_complete();
    if (!pc_gx_raw_tev_stage_set_swap(stage, ras_sel, tex_sel)) return;
    if (stage < 16) {
        PCGXTevStage* ts = &g_gx.tev_stages[stage];
        if (ts->ras_swap == (int)ras_sel && ts->tex_swap == (int)tex_sel) return;
        DIRTY(PC_GX_DIRTY_TEV_STAGES);
        ts->ras_swap = ras_sel;
        ts->tex_swap = tex_sel;
    }
}

void GXSetTevSwapModeTable(u32 table, u32 red, u32 green, u32 blue, u32 alpha) {
    pc_gx_flush_if_begin_complete();
    if (!pc_gx_raw_tev_set_swap_table(table, red, green, blue, alpha)) return;
    if (table < 4) {
        PCGXTevSwapTable* t = &g_gx.tev_swap_table[table];
        if (t->r == (int)red && t->g == (int)green &&
            t->b == (int)blue && t->a == (int)alpha) return;
        DIRTY(PC_GX_DIRTY_SWAP_TABLES);
        t->r = red;
        t->g = green;
        t->b = blue;
        t->a = alpha;
    }
}

/* --- Alpha / Depth / Blend --- */
void GXSetAlphaCompare(u32 comp0, u8 ref0, u32 op, u32 comp1, u8 ref1) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_alpha_store_compare(
        comp0, (uint32_t)ref0, op, comp1, (uint32_t)ref1
    );
    if (g_gx.alpha_comp0 == (int)comp0 && g_gx.alpha_ref0 == (int)ref0 &&
        g_gx.alpha_op == (int)op && g_gx.alpha_comp1 == (int)comp1 &&
        g_gx.alpha_ref1 == (int)ref1) return;
    DIRTY(PC_GX_DIRTY_ALPHA_CMP);
    g_gx.alpha_comp0 = comp0;
    g_gx.alpha_ref0 = ref0;
    g_gx.alpha_op = op;
    g_gx.alpha_comp1 = comp1;
    g_gx.alpha_ref1 = ref1;
}

void GXSetBlendMode(u32 type, u32 src, u32 dst, u32 logic_op) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_blend_store(type, src, dst, logic_op);
    if (g_gx.blend_mode == (int)type && g_gx.blend_src == (int)src &&
        g_gx.blend_dst == (int)dst && g_gx.blend_logic_op == (int)logic_op) return;
    DIRTY(PC_GX_DIRTY_BLEND);
    g_gx.blend_mode = type;
    g_gx.blend_src = src;
    g_gx.blend_dst = dst;
    g_gx.blend_logic_op = logic_op;
}

void GXSetZMode(GXBool compare_enable, u32 func, GXBool update_enable) {
    /* Drain a complete batch before either setter-owned state representation
     * changes, so the existing synchronous snapshot observes the old state. */
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_depth_store(
        compare_enable != GX_FALSE ? 1u : 0u,
        func,
        update_enable != GX_FALSE ? 1u : 0u
    );
    if (g_gx.z_compare_enable == (int)compare_enable &&
        g_gx.z_compare_func == (int)func &&
        g_gx.z_update_enable == (int)update_enable) return;
    DIRTY(PC_GX_DIRTY_DEPTH);
    g_gx.z_compare_enable = compare_enable;
    g_gx.z_compare_func = func;
    g_gx.z_update_enable = update_enable;
}

void GXSetColorUpdate(GXBool enable) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_alpha_store_boolean(
        enable != GX_FALSE ? 1u : 0u,
        PC_GX_RAW_ALPHA_KNOWN_COLOR_UPDATE,
        &g_gx.raw_alpha.value.color_update_enable
    );
    if (g_gx.color_update_enable == (int)enable) return;
    DIRTY(PC_GX_DIRTY_COLOR_MASK);
    g_gx.color_update_enable = enable;
}
void GXSetAlphaUpdate(GXBool enable) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_alpha_store_boolean(
        enable != GX_FALSE ? 1u : 0u,
        PC_GX_RAW_ALPHA_KNOWN_ALPHA_UPDATE,
        &g_gx.raw_alpha.value.alpha_update_enable
    );
    if (g_gx.alpha_update_enable == (int)enable) return;
    DIRTY(PC_GX_DIRTY_COLOR_MASK);
    g_gx.alpha_update_enable = enable;
}
void GXSetZCompLoc(GXBool before_tex) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_alpha_store_boolean(
        before_tex != GX_FALSE ? 1u : 0u,
        PC_GX_RAW_ALPHA_KNOWN_Z_COMP_LOC,
        &g_gx.raw_alpha.value.z_comp_loc_before_tex
    );
}
void GXSetDither(GXBool dither) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_raster_store_bounded(
        (uint32_t)dither,
        ACGC_GX_CANONICAL_RASTER_BOOLEAN_MIN,
        ACGC_GX_CANONICAL_RASTER_BOOLEAN_MAX,
        PC_GX_RAW_RASTER_KNOWN_DITHER,
        &g_gx.raw_raster.value.dither
    );
}
void GXSetDstAlpha(GXBool enable, u8 alpha) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_raster_store_dst_alpha(enable, alpha);
}
void GXSetFieldMask(GXBool odd, GXBool even) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_raster_store_field_mask(odd, even);
}
void GXSetFieldMode(GXBool field_mode, GXBool half_aspect) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_raster_store_field_mode(field_mode, half_aspect);
}
void GXSetPixelFmt(u32 pix_fmt, u32 z_fmt) { (void)pix_fmt; (void)z_fmt; }

void GXSetCullMode(u32 mode) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_raster_store_bounded(
        mode,
        ACGC_GX_CANONICAL_RASTER_CULL_MODE_NONE,
        ACGC_GX_CANONICAL_RASTER_CULL_MODE_ALL,
        PC_GX_RAW_RASTER_KNOWN_CULL_MODE,
        &g_gx.raw_raster.value.cull_mode
    );
    if (g_pc_model_viewer_no_cull) mode = GX_CULL_NONE;
    if (g_gx.cull_mode == (int)mode) return;
    DIRTY(PC_GX_DIRTY_CULL);
    g_gx.cull_mode = mode;
}
void GXSetCoPlanar(GXBool enable) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_raster_store_bounded(
        (uint32_t)enable,
        ACGC_GX_CANONICAL_RASTER_BOOLEAN_MIN,
        ACGC_GX_CANONICAL_RASTER_BOOLEAN_MAX,
        PC_GX_RAW_RASTER_KNOWN_CO_PLANAR,
        &g_gx.raw_raster.value.co_planar_enable
    );
}

/* --- Fog --- */
void GXSetFog(u32 type, f32 startz, f32 endz, f32 nearz, f32 farz, GXColor color) {
    if (pc_gx_raw_fog_flush_leaves_incomplete_begin()) {
        /* Decomp CHECK_GXBEGIN rejects the whole setter. Keep the raw epoch
         * and legacy host fields unchanged so no partial state is published. */
        return;
    }
    pc_gx_raw_fog_store(type, startz, endz, nearz, farz, color);
    float c[4] = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f };
    if (g_gx.fog_type == (int)type && g_gx.fog_start == startz &&
        g_gx.fog_end == endz && g_gx.fog_near == nearz && g_gx.fog_far == farz &&
        memcmp(g_gx.fog_color, c, sizeof(c)) == 0) return;
    DIRTY(PC_GX_DIRTY_FOG);
    g_gx.fog_type = type;
    g_gx.fog_start = startz;
    g_gx.fog_end = endz;
    g_gx.fog_near = nearz;
    g_gx.fog_far = farz;
    memcpy(g_gx.fog_color, c, sizeof(c));
}

void GXInitFogAdjTable(void* table, u16 width, f32 projmtx[4][4]) {
    (void)pc_gx_raw_fog_init_adj_table(table, width, projmtx);
}
void GXSetFogRangeAdj(GXBool enable, u16 center, void* table) {
    if (pc_gx_raw_fog_flush_leaves_incomplete_begin()) {
        /* Decomp CHECK_GXBEGIN rejects the whole setter; do not inspect or
         * copy the caller table or close the raw epoch from this call. */
        return;
    }
    pc_gx_raw_fog_store_range((uint32_t)enable, center, table);
}

/* --- Lighting --- */
static int pc_gx_chan_index(u32 chan) {
    switch (chan) {
        case GX_COLOR0:
        case GX_ALPHA0:
        case GX_COLOR0A0:
            return 0;
        case GX_COLOR1:
        case GX_ALPHA1:
        case GX_COLOR1A1:
            return 1;
        default:
            return -1;
    }
}

void GXSetNumChans(u8 nChans) {
    pc_gx_flush_if_begin_complete();
#ifdef PC_GX_CHANNELS_RAW_PRODUCER
    /* Raw provenance is captured after the completed batch and before the
     * legacy equality/default path can discard an equal setter call. */
    pc_gx_raw_channels_set_num((uint32_t)nChans);
#endif
    if (g_gx.num_chans == nChans) return;
    DIRTY(PC_GX_DIRTY_LIGHTING);
    g_gx.num_chans = nChans;
}

static int pc_gx_chan_ctrl_same(int i, GXBool enable, u32 amb_src, u32 mat_src,
                                u32 light_mask, u32 diff_fn, u32 attn_fn) {
    return g_gx.chan_ctrl_enable[i] == (int)enable &&
           g_gx.chan_ctrl_amb_src[i] == (int)amb_src &&
           g_gx.chan_ctrl_mat_src[i] == (int)mat_src &&
           g_gx.chan_ctrl_light_mask[i] == (int)light_mask &&
           g_gx.chan_ctrl_diff_fn[i] == (int)diff_fn &&
           g_gx.chan_ctrl_attn_fn[i] == (int)attn_fn;
}

void GXSetChanCtrl(u32 chan, GXBool enable, u32 amb_src, u32 mat_src,
                   u32 light_mask, u32 diff_fn, u32 attn_fn) {
    pc_gx_flush_if_begin_complete();
#ifdef PC_GX_CHANNELS_RAW_PRODUCER
    pc_gx_raw_channels_set_control(
        chan,
        (uint32_t)enable,
        amb_src,
        mat_src,
        light_mask,
        diff_fn,
        attn_fn
    );
#endif
    int idx = pc_gx_chan_index(chan);
    if (idx >= 0) {
        int is_combined = (chan >= GX_COLOR0A0);
        int is_alpha = (chan == GX_ALPHA0 || chan == GX_ALPHA1);
        int set_color = !is_alpha || is_combined;
        int set_alpha = is_alpha || is_combined;

        if ((!set_color || pc_gx_chan_ctrl_same(idx * 2, enable, amb_src, mat_src,
                                                light_mask, diff_fn, attn_fn)) &&
            (!set_alpha || pc_gx_chan_ctrl_same(idx * 2 + 1, enable, amb_src, mat_src,
                                                light_mask, diff_fn, attn_fn))) {
            return;
        }
        DIRTY(PC_GX_DIRTY_LIGHTING);
        if (set_color) {
            g_gx.chan_ctrl_enable[idx * 2] = enable;
            g_gx.chan_ctrl_amb_src[idx * 2] = amb_src;
            g_gx.chan_ctrl_mat_src[idx * 2] = mat_src;
            g_gx.chan_ctrl_light_mask[idx * 2] = light_mask;
            g_gx.chan_ctrl_diff_fn[idx * 2] = diff_fn;
            g_gx.chan_ctrl_attn_fn[idx * 2] = attn_fn;
        }
        if (set_alpha) {
            g_gx.chan_ctrl_enable[idx * 2 + 1] = enable;
            g_gx.chan_ctrl_amb_src[idx * 2 + 1] = amb_src;
            g_gx.chan_ctrl_mat_src[idx * 2 + 1] = mat_src;
            g_gx.chan_ctrl_light_mask[idx * 2 + 1] = light_mask;
            g_gx.chan_ctrl_diff_fn[idx * 2 + 1] = diff_fn;
            g_gx.chan_ctrl_attn_fn[idx * 2 + 1] = attn_fn;
        }
    }
}

void GXSetChanAmbColor(u32 chan, u32 color_packed) {
    pc_gx_flush_if_begin_complete();
#ifdef PC_GX_CHANNELS_RAW_PRODUCER
    pc_gx_raw_channels_set_color(chan, color_packed, 0);
#endif
    int idx = pc_gx_chan_index(chan);
    if (idx >= 0 && idx < 2) {
        float c[4];
        pc_unpack_gxcolor_f(color_packed, c);
        if (memcmp(g_gx.chan_amb_color[idx], c, sizeof(c)) == 0) return;
        DIRTY(PC_GX_DIRTY_LIGHTING);
        memcpy(g_gx.chan_amb_color[idx], c, sizeof(c));
    }
}

void GXSetChanMatColor(u32 chan, u32 color_packed) {
    pc_gx_flush_if_begin_complete();
#ifdef PC_GX_CHANNELS_RAW_PRODUCER
    pc_gx_raw_channels_set_color(chan, color_packed, 1);
#endif
    int idx = pc_gx_chan_index(chan);
    if (idx >= 0 && idx < 2) {
        float c[4];
        pc_unpack_gxcolor_f(color_packed, c);
        if (memcmp(g_gx.chan_mat_color[idx], c, sizeof(c)) == 0) return;
        DIRTY(PC_GX_DIRTY_LIGHTING);
        memcpy(g_gx.chan_mat_color[idx], c, sizeof(c));
    }
}

/* GXLightObj internal layout (from GXPriv.h) */
typedef struct {
    u32 padding[3];
    u32 color;
    f32 a0, a1, a2;
    f32 k0, k1, k2;
    f32 px, py, pz;
    f32 nx, ny, nz;
} PCGXLightObjInternal;

void GXInitLightSpot(void* lt, f32 cutoff, u32 spot_func) {
    PCGXLightObjInternal* l = (PCGXLightObjInternal*)lt;
    f32 a0, a1, a2, r, cr, d;

    if (cutoff <= 0.0f || cutoff > 90.0f)
        spot_func = GX_SP_OFF;

    r = PC_PIf * cutoff / 180.0f;
    cr = cosf(r);
    switch (spot_func) {
    case GX_SP_FLAT:
        a0 = -1000.0f * cr;
        a1 = 1000.0f;
        a2 = 0.0f;
        break;
    case GX_SP_COS:
        a0 = -cr / (1.0f - cr);
        a1 = 1.0f / (1.0f - cr);
        a2 = 0.0f;
        break;
    case GX_SP_COS2:
        a0 = 0.0f;
        a1 = -cr / (1.0f - cr);
        a2 = 1.0f / (1.0f - cr);
        break;
    case GX_SP_SHARP:
        d = (1.0f - cr) * (1.0f - cr);
        a0 = (cr * (cr - 2.0f)) / d;
        a1 = 2.0f / d;
        a2 = -1.0f / d;
        break;
    case GX_SP_RING1:
        d = (1.0f - cr) * (1.0f - cr);
        a0 = (-4.0f * cr) / d;
        a1 = (4.0f * (1.0f + cr)) / d;
        a2 = -4.0f / d;
        break;
    case GX_SP_RING2:
        d = (1.0f - cr) * (1.0f - cr);
        a0 = 1.0f - ((2.0f * cr * cr) / d);
        a1 = (4.0f * cr) / d;
        a2 = -2.0f / d;
        break;
    case GX_SP_OFF:
    default:
        a0 = 1.0f;
        a1 = 0.0f;
        a2 = 0.0f;
        break;
    }
    l->a0 = a0; l->a1 = a1; l->a2 = a2;
}
void GXInitLightDistAttn(void* lt, f32 ref_dist, f32 ref_bright, u32 dist_func) {
    PCGXLightObjInternal* l = (PCGXLightObjInternal*)lt;
    f32 k0, k1, k2;

    if (ref_dist < 0.0f)
        dist_func = GX_DA_OFF;
    if (ref_bright <= 0.0f || ref_bright >= 1.0f)
        dist_func = GX_DA_OFF;

    switch (dist_func) {
    case GX_DA_GENTLE:
        k0 = 1.0f;
        k1 = (1.0f - ref_bright) / (ref_bright * ref_dist);
        k2 = 0.0f;
        break;
    case GX_DA_MEDIUM:
        k0 = 1.0f;
        k1 = (0.5f * (1.0f - ref_bright)) / (ref_bright * ref_dist);
        k2 = (0.5f * (1.0f - ref_bright)) / (ref_bright * ref_dist * ref_dist);
        break;
    case GX_DA_STEEP:
        k0 = 1.0f;
        k1 = 0.0f;
        k2 = (1.0f - ref_bright) / (ref_bright * ref_dist * ref_dist);
        break;
    case GX_DA_OFF:
    default:
        k0 = 1.0f;
        k1 = 0.0f;
        k2 = 0.0f;
        break;
    }
    l->k0 = k0; l->k1 = k1; l->k2 = k2;
}
void GXInitLightPos(void* lt, f32 x, f32 y, f32 z) {
    PCGXLightObjInternal* l = (PCGXLightObjInternal*)lt;
    l->px = x; l->py = y; l->pz = z;
}
void GXInitLightDir(void* lt, f32 nx, f32 ny, f32 nz) {
    PCGXLightObjInternal* l = (PCGXLightObjInternal*)lt;
    /* GXLightObj stores the final register direction, opposite the API
     * direction.  GXLoadLightObjImm and the raw producer copy these words
     * without applying a second sign conversion. */
    l->nx = -nx; l->ny = -ny; l->nz = -nz;
}
void GXInitSpecularDir(void* lt, f32 nx, f32 ny, f32 nz) {
    PCGXLightObjInternal* l = (PCGXLightObjInternal*)lt;
    f32 vx = -nx;
    f32 vy = -ny;
    f32 vz = -nz + 1.0f;
    f32 mag = 1.0f / sqrtf((vx * vx) + (vy * vy) + (vz * vz));

    l->nx = vx * mag;
    l->ny = vy * mag;
    l->nz = vz * mag;
    l->px = -nx * 1048576.0f;
    l->py = -ny * 1048576.0f;
    l->pz = -nz * 1048576.0f;
}
void GXInitSpecularDirHA(
    void* lt,
    f32 nx,
    f32 ny,
    f32 nz,
    f32 hx,
    f32 hy,
    f32 hz
) {
    PCGXLightObjInternal* l = (PCGXLightObjInternal*)lt;

    l->nx = hx; l->ny = hy; l->nz = hz;
    l->px = -nx * 1048576.0f;
    l->py = -ny * 1048576.0f;
    l->pz = -nz * 1048576.0f;
}
void GXInitLightColor(void* lt, u32 color) {
    PCGXLightObjInternal* l = (PCGXLightObjInternal*)lt;
    l->color = color;
}
void GXInitLightAttn(void* lt, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2) {
    PCGXLightObjInternal* l = (PCGXLightObjInternal*)lt;
    l->a0 = a0; l->a1 = a1; l->a2 = a2;
    l->k0 = k0; l->k1 = k1; l->k2 = k2;
}
void GXInitLightAttnA(void* lt, f32 a0, f32 a1, f32 a2) {
    PCGXLightObjInternal* l = (PCGXLightObjInternal*)lt;
    l->a0 = a0; l->a1 = a1; l->a2 = a2;
}
void GXInitLightAttnK(void* lt, f32 k0, f32 k1, f32 k2) {
    PCGXLightObjInternal* l = (PCGXLightObjInternal*)lt;
    l->k0 = k0; l->k1 = k1; l->k2 = k2;
}
void GXLoadLightObjImm(void* lt, u32 light) {
    pc_gx_flush_if_begin_complete();
#ifdef PC_GX_LIGHTING_RAW_PRODUCER
    /* The completed batch is already observable.  The raw helper validates
     * and commits its local register copy before this legacy equality path. */
    pc_gx_raw_lighting_load_immediate(lt, light);
#endif
    if (lt == NULL) return;
    PCGXLightObjInternal* l = (PCGXLightObjInternal*)lt;
    int slot = -1;
    for (int i = 0; i < 8; i++) {
        if (light == (1u << i)) { slot = i; break; }
    }
    if (slot < 0) return;

    float c[4];
    pc_unpack_gxcolor_f(l->color, c);
    if (g_gx.lights[slot].pos[0] == l->px && g_gx.lights[slot].pos[1] == l->py &&
        g_gx.lights[slot].pos[2] == l->pz && g_gx.lights[slot].dir[0] == l->nx &&
        g_gx.lights[slot].dir[1] == l->ny && g_gx.lights[slot].dir[2] == l->nz &&
        g_gx.lights[slot].a0 == l->a0 && g_gx.lights[slot].a1 == l->a1 &&
        g_gx.lights[slot].a2 == l->a2 && g_gx.lights[slot].k0 == l->k0 &&
        g_gx.lights[slot].k1 == l->k1 && g_gx.lights[slot].k2 == l->k2 &&
        memcmp(g_gx.lights[slot].color, c, sizeof(c)) == 0) {
        return;
    }
    DIRTY(PC_GX_DIRTY_LIGHTING);
    g_gx.lights[slot].pos[0] = l->px;
    g_gx.lights[slot].pos[1] = l->py;
    g_gx.lights[slot].pos[2] = l->pz;
    g_gx.lights[slot].dir[0] = l->nx;
    g_gx.lights[slot].dir[1] = l->ny;
    g_gx.lights[slot].dir[2] = l->nz;
    g_gx.lights[slot].a0 = l->a0;
    g_gx.lights[slot].a1 = l->a1;
    g_gx.lights[slot].a2 = l->a2;
    g_gx.lights[slot].k0 = l->k0;
    g_gx.lights[slot].k1 = l->k1;
    g_gx.lights[slot].k2 = l->k2;
    memcpy(g_gx.lights[slot].color, c, sizeof(c));
}
void GXLoadLightObjIndx(u32 lt_obj_indx, u32 light) {
    pc_gx_flush_if_begin_complete();
#ifdef PC_GX_LIGHTING_RAW_PRODUCER
    pc_gx_raw_lighting_load_indexed(lt_obj_indx, light);
#else
    (void)lt_obj_indx;
    (void)light;
#endif
}
void GXGetLightPos(void* lt, f32* x, f32* y, f32* z) {
    PCGXLightObjInternal* l = (PCGXLightObjInternal*)lt;
    *x = l->px; *y = l->py; *z = l->pz;
}
void GXGetLightColor(void* lt, void* color) {
    PCGXLightObjInternal* l = (PCGXLightObjInternal*)lt;
    memcpy(color, &l->color, 4);
}

/* --- Texture Coordinate Generation --- */
void GXSetNumTexGens(u8 n) {
    pc_gx_flush_if_begin_complete();
    if (n <= PC_GX_TEXGEN_COUNT) {
        g_gx.raw_texgen.active_texgen_count = n;
        g_gx.raw_texgen.active_texgen_count_known = 1;
    } else {
        g_gx.raw_texgen.active_texgen_count = 0;
        g_gx.raw_texgen.active_texgen_count_known = 0;
        pc_gx_raw_texgen_mark_invalid();
    }
    if (g_gx.num_tex_gens == n) return;
    DIRTY(PC_GX_DIRTY_TEXGEN);
    g_gx.num_tex_gens = n;
}
void GXSetTexCoordGen2(u32 dst, u32 func, u32 src, u32 mtx, GXBool normalize, u32 postmtx) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_texgen_store(dst, func, src, mtx, normalize, postmtx);
    if (dst < 8) {
        if (g_gx.tex_gen_type[dst] == (int)func &&
            g_gx.tex_gen_src[dst] == (int)src &&
            g_gx.tex_gen_mtx[dst] == (int)mtx &&
            s_tex_gen_extended_state_known[dst] != 0 &&
            s_tex_gen_normalize[dst] == normalize &&
            s_tex_gen_post_mtx[dst] == postmtx) {
            return;
        }
        DIRTY(PC_GX_DIRTY_TEXGEN);
        g_gx.tex_gen_type[dst] = func;
        g_gx.tex_gen_src[dst] = src;
        g_gx.tex_gen_mtx[dst] = mtx;
        s_tex_gen_extended_state_known[dst] = 1;
        s_tex_gen_normalize[dst] = normalize;
        s_tex_gen_post_mtx[dst] = postmtx;
    }
}
void GXSetLineWidth(u8 width, u32 texOffsets) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_raster_store_size_and_tex_offset(
        width,
        texOffsets,
        PC_GX_RAW_RASTER_KNOWN_LINE_WIDTH,
        PC_GX_RAW_RASTER_KNOWN_LINE_TEX_OFFSET,
        &g_gx.raw_raster.value.line_width,
        &g_gx.raw_raster.value.line_tex_offsets
    );
    glLineWidth(width / 16.0f);
}
void GXSetPointSize(u8 size, u32 texOffsets) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_raster_store_size_and_tex_offset(
        size,
        texOffsets,
        PC_GX_RAW_RASTER_KNOWN_POINT_SIZE,
        PC_GX_RAW_RASTER_KNOWN_POINT_TEX_OFFSET,
        &g_gx.raw_raster.value.point_size,
        &g_gx.raw_raster.value.point_tex_offsets
    );
    glPointSize(size / 16.0f);
}
void GXEnableTexOffsets(u32 coord, GXBool line, GXBool point) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_raster_store_texcoord_offsets(coord, line, point);
}
void GXSetTexCoordScaleManually(u32 coord, GXBool enable, u16 ss, u16 ts) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_texgen_su_store_manual(coord, enable, ss, ts);
    (void)coord; (void)enable; (void)ss; (void)ts;
}
void GXSetTexCoordCylWrap(u32 coord, u8 s, u8 t) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_texgen_su_store_cylinder(coord, s, t);
}
void GXSetTexCoordBias(u32 coord, u8 s, u8 t) {
    pc_gx_flush_if_begin_complete();
    pc_gx_raw_texgen_su_store_bias(coord, s, t);
}

/* --- Framebuffer / Copy --- */
void GXSetCopyClear(GXColor clear_clr, u32 clear_z) {
    g_gx.clear_color[0] = clear_clr.r / 255.0f;
    g_gx.clear_color[1] = clear_clr.g / 255.0f;
    g_gx.clear_color[2] = clear_clr.b / 255.0f;
    g_gx.clear_color[3] = clear_clr.a / 255.0f;
    g_gx.clear_depth = clear_z / (float)0x00FFFFFF;
}

void GXCopyDisp(void* dest, GXBool clear) {
    /* On PC we render to the back buffer directly; swap happens in VIWaitForRetrace.
     * Just flush pending geometry — do NOT swap or clear here. */
    pc_gx_commit_pending_and_flush();
    pc_gx_draw_pending();
    (void)dest;
    (void)clear;
}

void GXSetDispCopyGamma(u32 gamma) { (void)gamma; }
void GXSetDispCopySrc(u16 left, u16 top, u16 wd, u16 ht) {
    g_gx.copy_src[0] = left; g_gx.copy_src[1] = top;
    g_gx.copy_src[2] = wd; g_gx.copy_src[3] = ht;
}
void GXSetDispCopyDst(u16 wd, u16 ht) { g_gx.copy_dst[0] = wd; g_gx.copy_dst[1] = ht; }
f32 GXGetYScaleFactor(u16 efbHeight, u16 xfbHeight) {
    return (f32)xfbHeight / (f32)efbHeight;
}
u32 GXSetDispCopyYScale(f32 vscale) { return (u32)(vscale * 256.0f); }
u16 GXGetNumXfbLines(u16 efbHeight, f32 yScale) { return (u16)(efbHeight * yScale); }
void GXSetCopyFilter(GXBool aa, const void* pattern, GXBool vf, const void* vfilter) {
    if (g_pc_gx_dl.active) {
        u32 op = PCGX_DL_OP_COPY_FILTER;
        pc_gx_dl_write(&op, sizeof(op));
        return;
    }
    (void)aa; (void)pattern; (void)vf; (void)vfilter;
}
void GXAdjustForOverscan(void* rmin, void* rmout, u16 hor, u16 ver) {
    memcpy(rmout, rmin, sizeof(u32) * 16);
}

static void pc_gx_copy_tex_execute_impl(void* dest, GXBool clear) {
    pc_gx_commit_pending_and_flush();
    pc_gx_draw_pending(); /* everything must hit the framebuffer before glReadPixels */

    if (!dest) return;

    int out_wd = g_gx.tex_copy_src[2];
    int out_ht = g_gx.tex_copy_src[3];
    if (out_wd <= 0 || out_ht <= 0) return;
    if (out_wd > 4096 || out_ht > 4096) return;

#ifdef PC_ENHANCEMENTS
    /* Scale readback coordinates from GC coords to window resolution */
    float sx = (float)g_pc_window_w / (float)PC_GC_WIDTH;
    float sy = (float)g_pc_window_h / (float)PC_GC_HEIGHT;
    int read_left = (int)(g_gx.tex_copy_src[0] * sx);
    int read_top  = (int)(g_gx.tex_copy_src[1] * sy);
    int read_wd   = (int)(out_wd * sx);
    int read_ht   = (int)(out_ht * sy);
#else
    int read_left = g_gx.tex_copy_src[0];
    int read_top  = g_gx.tex_copy_src[1];
    int read_wd   = out_wd;
    int read_ht   = out_ht;
#endif

    if (read_left < 0) { read_wd += read_left; read_left = 0; }
    if (read_top < 0)  { read_ht += read_top;  read_top = 0; }
    if (read_left + read_wd > g_pc_window_w) read_wd = g_pc_window_w - read_left;
    if (read_top + read_ht > g_pc_window_h)  read_ht = g_pc_window_h - read_top;
    if (read_wd <= 0 || read_ht <= 0) return;

    int gl_y = g_pc_window_h - (read_top + read_ht);
    if (gl_y < 0) return;

    size_t rgba_size = (size_t)read_wd * (size_t)read_ht * 4;
    u8* rgba = (u8*)malloc(rgba_size);
    if (!rgba) return;

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(read_left, gl_y, read_wd, read_ht, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

#ifdef PC_ENHANCEMENTS
    /* Store as full-res GL texture; GXLoadTexObj will substitute it for the RGB565 buffer */
    {
        /* Flip: glReadPixels is bottom-up, textures are top-down */
        int row_bytes = read_wd * 4;
        for (int y = 0; y < read_ht / 2; y++) {
            u8* top_row = &rgba[y * row_bytes];
            u8* bot_row = &rgba[(read_ht - 1 - y) * row_bytes];
            for (int b = 0; b < row_bytes; b++) {
                u8 tmp = top_row[b];
                top_row[b] = bot_row[b];
                bot_row[b] = tmp;
            }
        }

        GLuint efb_tex;
        glGenTextures(1, &efb_tex);
        glBindTexture(GL_TEXTURE_2D, efb_tex);
        pc_profiler_add_count_texture_bind();
        pc_gx_texture_bind_cache_invalidate();
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, read_wd, read_ht, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        pc_gx_efb_capture_store((u32)(uintptr_t)dest, efb_tex);
        glBindTexture(GL_TEXTURE_2D, 0);
        pc_profiler_add_count_texture_bind();
        pc_gx_texture_bind_cache_invalidate();
        /* Unit 0 binding was clobbered; force rebind at next flush */
        DIRTY(PC_GX_DIRTY_TEXTURES);
    }
#else
    if (g_gx.tex_copy_fmt == 0x4) {
        u8* out = (u8*)dest;
        int bw = (out_wd + 3) / 4;
        int bh = (out_ht + 3) / 4;

        for (int by = 0; by < bh; by++) {
            for (int bx = 0; bx < bw; bx++) {
                for (int y = 0; y < 4; y++) {
                    for (int x = 0; x < 4; x++) {
                        int px = bx * 4 + x;
                        int py = by * 4 + y;
                        u16 rgb565 = 0;

                        if (px < out_wd && py < out_ht) {
                            int src_x = px * read_wd / out_wd;
                            int src_y = py * read_ht / out_ht;
                            src_y = read_ht - 1 - src_y;
                            const u8* p = &rgba[(src_y * read_wd + src_x) * 4];
                            rgb565 = (u16)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
                        }

                        out[0] = (u8)((rgb565 >> 8) & 0xFF);
                        out[1] = (u8)(rgb565 & 0xFF);
                        out += 2;
                    }
                }
            }
        }
    }
#endif

    free(rgba);
    (void)clear;
}

/* Times the synchronous glReadPixels stall (called via GXCopyDisp and DL replay) */
static void pc_gx_copy_tex_execute(void* dest, GXBool clear) {
    Uint64 prof_start = pc_profiler_begin_timer();
    pc_gx_copy_tex_execute_impl(dest, clear);
    pc_profiler_add_time(PC_PROF_TIMER_EFB_COPY, prof_start);
}

void GXSetTexCopySrc(u16 left, u16 top, u16 wd, u16 ht) {
    if (g_pc_gx_dl.active) {
        u32 pkt[5] = { PCGX_DL_OP_TEXCOPY_SRC, left, top, wd, ht };
        pc_gx_dl_write(pkt, sizeof(pkt));
        return;
    }
    g_gx.tex_copy_src[0] = left;
    g_gx.tex_copy_src[1] = top;
    g_gx.tex_copy_src[2] = wd;
    g_gx.tex_copy_src[3] = ht;
}
void GXSetTexCopyDst(u16 wd, u16 ht, u32 fmt, GXBool mipmap) {
    if (g_pc_gx_dl.active) {
        u32 pkt[5] = { PCGX_DL_OP_TEXCOPY_DST, wd, ht, fmt, mipmap ? 1u : 0u };
        pc_gx_dl_write(pkt, sizeof(pkt));
        return;
    }
    g_gx.tex_copy_dst[0] = wd;
    g_gx.tex_copy_dst[1] = ht;
    g_gx.tex_copy_fmt = fmt;
    g_gx.tex_copy_mipmap = mipmap ? 1 : 0;
}
void GXCopyTex(void* dest, GXBool clear) {
    if (g_pc_gx_dl.active) {
        u32 op = PCGX_DL_OP_COPY_TEX;
        u64 dest64 = (u64)(uintptr_t)dest;
        u32 clear_u32 = clear ? 1u : 0u;
        pc_gx_dl_write(&op, sizeof(op));
        pc_gx_dl_write(&dest64, sizeof(dest64));
        pc_gx_dl_write(&clear_u32, sizeof(clear_u32));
        return;
    }

    pc_gx_copy_tex_execute(dest, clear);
}
void GXSetCopyClamp(u32 clamp) { (void)clamp; }

/* --- GX Init / Management --- */
void* GXInit(void* base, u32 size) {
    (void)base; (void)size;
    return base;
}

void GXSetMisc(u32 token, u32 val) { (void)token; (void)val; }
void GXFlush(void) { pc_gx_draw_pending(); glFlush(); }
void GXResetWriteGatherPipe(void) {}
void GXAbortFrame(void) {}
void GXSetDrawSync(u16 token) { (void)token; }
u16  GXReadDrawSync(void) { return 0; }
void GXSetDrawDone(void) {}
void GXWaitDrawDone(void) {}
void GXDrawDone(void) {}
void GXPixModeSync(void) {}
void GXTexModeSync(void) {}

void* GXSetDrawSyncCallback(void* cb) { return NULL; }
void* GXSetDrawDoneCallback(void* cb) { return NULL; }

/* --- FIFO --- */
typedef struct { u8 pad[128]; } GXFifoObj;
void GXInitFifoBase(GXFifoObj* fifo, void* base, u32 size) { (void)fifo; (void)base; (void)size; }
void GXInitFifoPtrs(GXFifoObj* fifo, void* rp, void* wp) { (void)fifo; (void)rp; (void)wp; }
void GXInitFifoLimits(GXFifoObj* fifo, u32 hi, u32 lo) { (void)fifo; (void)hi; (void)lo; }
void GXSetCPUFifo(GXFifoObj* fifo) { (void)fifo; }
void GXSetGPFifo(GXFifoObj* fifo) { (void)fifo; }
void GXSaveCPUFifo(GXFifoObj* fifo) { (void)fifo; }
void GXSaveGPFifo(GXFifoObj* fifo) { (void)fifo; }
void GXGetGPStatus(GXBool* a, GXBool* b, GXBool* c, GXBool* d, GXBool* e) {
    if (a) *a = 0;
    if (b) *b = 0;
    if (c) *c = 1;
    if (d) *d = 1;
    if (e) *e = 0;
}
void GXGetFifoStatus(GXFifoObj* f, GXBool* a, GXBool* b, u32* c, GXBool* d, GXBool* e, GXBool* g) {
    if (a) *a = 0;
    if (b) *b = 0;
    if (c) *c = 0;
    if (d) *d = 0;
    if (e) *e = 0;
    if (g) *g = 0;
}
void GXGetFifoPtrs(GXFifoObj* f, void** rp, void** wp) { if (rp) *rp = NULL; if (wp) *wp = NULL; }
void* GXGetFifoBase(GXFifoObj* f) { return NULL; }
u32 GXGetFifoSize(GXFifoObj* f) { return 0; }
void GXGetFifoLimits(GXFifoObj* f, u32* hi, u32* lo) { if (hi) *hi = 0; if (lo) *lo = 0; }
void* GXSetBreakPtCallback(void* cb) { return NULL; }
void GXEnableBreakPt(void* bp) { (void)bp; }
void GXDisableBreakPt(void) {}
void* GXSetCurrentGXThread(void) { return NULL; }
void* GXGetCurrentGXThread(void) { return NULL; }
GXFifoObj* GXGetCPUFifo(void) { static GXFifoObj f; return &f; }
GXFifoObj* GXGetGPFifo(void) { static GXFifoObj f; return &f; }
u32 GXGetOverflowCount(void) { return 0; }
u32 GXResetOverflowCount(void) { return 0; }
volatile void* GXRedirectWriteGatherPipe(void* ptr) { return ptr; }
void GXRestoreWriteGatherPipe(void) {}
int IsWriteGatherBufferEmpty(void) { return 1; }

/* --- Display List --- */
void GXBeginDisplayList(void* list, u32 size) {
    g_pc_gx_dl.active = 1;
    g_pc_gx_dl.buf = (u8*)list;
    g_pc_gx_dl.size = size;
    g_pc_gx_dl.off = 0;
    g_pc_gx_dl.overflow = 0;
}
u32 GXEndDisplayList(void) {
    u32 nbytes = 0;
    if (g_pc_gx_dl.active && !g_pc_gx_dl.overflow) {
        nbytes = g_pc_gx_dl.off;
    }
    g_pc_gx_dl.active = 0;
    g_pc_gx_dl.buf = NULL;
    g_pc_gx_dl.size = 0;
    g_pc_gx_dl.off = 0;
    g_pc_gx_dl.overflow = 0;
    return nbytes;
}
void GXCallDisplayList(void* list, u32 nbytes) {
    if (!list || nbytes == 0) return;

    Uint64 prof_start = pc_profiler_begin_timer();
#define PC_GX_DL_RETURN() do { pc_profiler_add_time(PC_PROF_TIMER_DISPLAY_LIST, prof_start); return; } while (0)

    const u8* p = (const u8*)list;
    u32 off = 0;

    while (off + sizeof(u32) <= nbytes) {
        u32 op = 0;
        memcpy(&op, p + off, sizeof(op));
        off += sizeof(op);

        switch (op) {
            case PCGX_DL_OP_TEXCOPY_SRC: {
                u32 v[4];
                if (off + sizeof(v) > nbytes) PC_GX_DL_RETURN();
                memcpy(v, p + off, sizeof(v));
                off += sizeof(v);
                GXSetTexCopySrc((u16)v[0], (u16)v[1], (u16)v[2], (u16)v[3]);
                break;
            }
            case PCGX_DL_OP_TEXCOPY_DST: {
                u32 v[4];
                if (off + sizeof(v) > nbytes) PC_GX_DL_RETURN();
                memcpy(v, p + off, sizeof(v));
                off += sizeof(v);
                GXSetTexCopyDst((u16)v[0], (u16)v[1], v[2], (GXBool)(v[3] ? 1 : 0));
                break;
            }
            case PCGX_DL_OP_COPY_FILTER:
                break;
            case PCGX_DL_OP_COPY_TEX: {
                u64 dest64 = 0;
                u32 clear = 0;
                if (off + sizeof(dest64) + sizeof(clear) > nbytes) PC_GX_DL_RETURN();
                memcpy(&dest64, p + off, sizeof(dest64));
                off += sizeof(dest64);
                memcpy(&clear, p + off, sizeof(clear));
                off += sizeof(clear);
                pc_gx_copy_tex_execute((void*)(uintptr_t)dest64, (GXBool)(clear ? 1 : 0));
                break;
            }
            default:
                PC_GX_DL_RETURN();
        }
    }

    pc_profiler_add_time(PC_PROF_TIMER_DISPLAY_LIST, prof_start);
#undef PC_GX_DL_RETURN
}

/* --- Indirect Texture --- */
void GXSetTevIndirect(u32 stage, u32 ind_stage, u32 fmt, u32 bias_sel,
                      u32 mtx_sel, u32 wrap_s, u32 wrap_t, GXBool add_prev,
                      GXBool ind_lod, u32 alpha_sel);

void GXSetTevDirect(u32 stage) {
    GXSetTevIndirect(stage, 0/*GX_INDTEXSTAGE0*/, 0/*GX_ITF_8*/, 0/*GX_ITB_NONE*/,
                     0/*GX_ITM_OFF*/, 0/*GX_ITW_OFF*/, 0/*GX_ITW_OFF*/, 0, 0, 0/*GX_ITBA_OFF*/);
}
void GXSetNumIndStages(u8 n) {
    pc_gx_flush_if_begin_complete();
    if (!pc_gx_raw_indirect_set_num_stages(n)) return;
    DIRTY(PC_GX_DIRTY_INDIRECT);
    g_gx.num_ind_stages = n;
}

void GXSetIndTexMtx(u32 mtx_sel, const void* offset, s8 scale) {
    pc_gx_flush_if_begin_complete();
    if (!pc_gx_raw_indirect_set_matrix(mtx_sel, offset, scale)) return;
    DIRTY(PC_GX_DIRTY_INDIRECT);
    int id;
    switch (mtx_sel) {
        case 1: case 2: case 3:   id = mtx_sel - 1; break;
        case 5: case 6: case 7:   id = mtx_sel - 5; break;
        case 9: case 10: case 11: id = mtx_sel - 9; break;
        default: return;
    }
    if (id < 0 || id >= 3 || offset == NULL) return;
    const float* mtx = (const float*)offset;
    g_gx.ind_mtx[id][0][0] = mtx[0];
    g_gx.ind_mtx[id][0][1] = mtx[1];
    g_gx.ind_mtx[id][0][2] = mtx[2];
    g_gx.ind_mtx[id][1][0] = mtx[3];
    g_gx.ind_mtx[id][1][1] = mtx[4];
    g_gx.ind_mtx[id][1][2] = mtx[5];
    g_gx.ind_mtx_scale[id] = scale;
}

void GXSetIndTexOrder(u32 ind_stage, u32 tex_coord, u32 tex_map) {
    pc_gx_flush_if_begin_complete();
    if (!pc_gx_raw_indirect_set_order(ind_stage, tex_coord, tex_map)) return;
    DIRTY(PC_GX_DIRTY_INDIRECT);
    if (ind_stage >= 4) return;
    g_gx.ind_order[ind_stage].tex_coord = tex_coord;
    g_gx.ind_order[ind_stage].tex_map = tex_map;
}

void GXSetTevIndirect(u32 stage, u32 ind_stage, u32 fmt, u32 bias_sel,
                      u32 mtx_sel, u32 wrap_s, u32 wrap_t, GXBool add_prev,
                      GXBool ind_lod, u32 alpha_sel) {
    pc_gx_flush_if_begin_complete();
    if (!pc_gx_raw_tev_stage_set_indirect(
        stage,
        ind_stage,
        fmt,
        bias_sel,
        mtx_sel,
        wrap_s,
        wrap_t,
        add_prev,
        ind_lod,
        alpha_sel
    )) return;
    DIRTY(PC_GX_DIRTY_INDIRECT);
    if (stage >= 16) return;
    PCGXTevStage* s = &g_gx.tev_stages[stage];
    s->ind_stage  = ind_stage;
    s->ind_format = fmt;
    s->ind_bias   = bias_sel;
    s->ind_mtx    = mtx_sel;
    s->ind_wrap_s = wrap_s;
    s->ind_wrap_t = wrap_t;
    s->ind_add_prev = add_prev;
    s->ind_lod    = ind_lod;
    s->ind_alpha  = alpha_sel;
}

void GXSetTevIndWarp(u32 stage, u32 ind_stage, GXBool signed_ofs, GXBool replace, u32 mtx_sel) {
    u32 wrap = replace ? 6/*GX_ITW_0*/ : 0/*GX_ITW_OFF*/;
    u32 bias = signed_ofs ? 7/*GX_ITB_STU*/ : 0/*GX_ITB_NONE*/;
    GXSetTevIndirect(stage, ind_stage, 0/*GX_ITF_8*/, bias, mtx_sel, wrap, wrap, 0, 0, 0);
}

void GXSetIndTexCoordScale(u32 ind_stage, u32 scale_s, u32 scale_t) {
    pc_gx_flush_if_begin_complete();
    if (!pc_gx_raw_indirect_set_scale(ind_stage, scale_s, scale_t)) return;
    DIRTY(PC_GX_DIRTY_INDIRECT);
    if (ind_stage >= 4) return;
    g_gx.ind_order[ind_stage].scale_s = scale_s;
    g_gx.ind_order[ind_stage].scale_t = scale_t;
}

void __GXSetIndirectMask(u32 mask) { (void)mask; }

/* --- Z Texture --- */
void GXSetZTexture(u32 op, u32 fmt, u32 bias) { (void)op; (void)fmt; (void)bias; }

/* --- Draw Utility --- */
void GXDrawSphere(u8 numMajor, u8 numMinor) { (void)numMajor; (void)numMinor; }

/* --- Perf --- */
void GXReadXfRasMetric(u32* xf_wait_in, u32* xf_wait_out, u32* ras_busy, u32* clocks) {
    if (xf_wait_in) *xf_wait_in = 0;
    if (xf_wait_out) *xf_wait_out = 0;
    if (ras_busy) *ras_busy = 0;
    if (clocks) *clocks = 0;
}

/* --- Verify --- */
void GXSetVerifyLevel(u32 level) { (void)level; }
void* GXSetVerifyCallback(void* cb) { return NULL; }
