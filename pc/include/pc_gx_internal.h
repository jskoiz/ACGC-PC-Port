/* pc_gx_internal.h - GX state machine, vertex format, TEV config, GL objects */
#ifndef PC_GX_INTERNAL_H
#define PC_GX_INTERNAL_H

#include "pc_platform.h"
#include "acgc/gx_canonical_alpha_state.h"
#include "acgc/gx_canonical_channel_state.h"
#include "acgc/gx_canonical_indirect_state.h"
#include "acgc/gx_canonical_lighting_state.h"
#include "acgc/gx_canonical_raster_state.h"
#include "acgc/gx_canonical_tev_state.h"
#include "acgc/gx_semantic_packet.h"

/* Define PC_GL_DEBUG to check for GL errors after significant calls */
#ifdef PC_GL_DEBUG
#define PC_GL_CHECK(label) do { \
    GLenum err_ = glGetError(); \
    if (err_ != GL_NO_ERROR) \
        printf("[GL ERR] %s: 0x%04X at %s:%d\n", label, err_, __FILE__, __LINE__); \
} while(0)
#else
#define PC_GL_CHECK(label) ((void)0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* --- Dirty flags for conditional uniform upload --- */
#define PC_GX_DIRTY_PROJECTION  (1u << 0)
#define PC_GX_DIRTY_MODELVIEW   (1u << 1)
#define PC_GX_DIRTY_TEV_COLORS  (1u << 2)
#define PC_GX_DIRTY_TEV_STAGES  (1u << 3)
#define PC_GX_DIRTY_SWAP_TABLES (1u << 4)
#define PC_GX_DIRTY_KONST       (1u << 5)
#define PC_GX_DIRTY_ALPHA_CMP   (1u << 6)
#define PC_GX_DIRTY_LIGHTING    (1u << 7)
#define PC_GX_DIRTY_TEXGEN      (1u << 8)
#define PC_GX_DIRTY_TEXTURES    (1u << 9)
#define PC_GX_DIRTY_INDIRECT    (1u << 10)
#define PC_GX_DIRTY_FOG         (1u << 11)
#define PC_GX_DIRTY_DEPTH       (1u << 12)
#define PC_GX_DIRTY_COLOR_MASK  (1u << 13)
#define PC_GX_DIRTY_CULL        (1u << 14)
#define PC_GX_DIRTY_BLEND       (1u << 15)
#define PC_GX_DIRTY_ALL         0xFFFFu
/* Groups that map to per-program uniforms (bits 0-11). DEPTH..BLEND are
 * global GL state and need no per-program stale tracking. */
#define PC_GX_DIRTY_UNIFORM_GROUPS 0x0FFFu
#define PC_GX_NUM_DIRTY_GROUPS 16
/* DIRTY() is defined below g_gx - it also bumps per-group sequence counters */

/* --- Vertex buffer --- */
#define PC_GX_MAX_VERTS       65536
#define PC_GX_MAX_ATTRIB_SIZE 64
#define PC_GX_MAX_ATTR        26
#define PC_GX_MAX_VTXFMT      8
#define PC_GX_MAX_TEV_STAGES  3

/* The canonical Geometry section is intentionally bounded independently of
 * the legacy host vertex cache.  These records are setter-owned provenance,
 * not a producer ABI and never contain a caller pointer. */
#define PC_GX_GEOMETRY_MAX_VERTICES       128
#define PC_GX_GEOMETRY_MAX_VALUE_WORDS    9
#define PC_GX_GEOMETRY_ARRAY_KNOWN        UINT32_C(1)
#define PC_GX_GEOMETRY_ARRAY_DATA_KNOWN   UINT32_C(2)

typedef struct {
    int has_position;
    int has_normal;
    int has_color0;
    int has_color1;
    int has_texcoord[8];
    int texcoord_frac[8];
    int position_size;
    int color_size;
    int texcoord_size;
    int stride;
} PCGXVertexFormat;

typedef struct {
    float position[3];
    float normal[3];
    unsigned char color0[4];
    unsigned char color1[4];
    float texcoord[8][2];
} PCGXVertex;

typedef struct {
    int color_a, color_b, color_c, color_d;
    int alpha_a, alpha_b, alpha_c, alpha_d;
    int color_op, color_bias, color_scale, color_clamp, color_out;
    int alpha_op, alpha_bias, alpha_scale, alpha_clamp, alpha_out;
    int tex_coord, tex_map, color_chan;
    int k_color_sel, k_alpha_sel;
    int ras_swap, tex_swap;
    int ind_stage, ind_format, ind_bias, ind_mtx, ind_wrap_s, ind_wrap_t;
    int ind_add_prev, ind_lod, ind_alpha;
} PCGXTevStage;

typedef struct {
    int r, g, b, a;  /* channel indices: 0=R, 1=G, 2=B, 3=A */
} PCGXTevSwapTable;

/* Setter-owned raw TEV/KONST provenance.  Components are widened logical
 * values: GXSetTevColor keeps u8 values, GXSetTevColorS10 keeps signed S10
 * inputs, and GXSetTevKColor keeps u8 values.  The validity bit is separate
 * from the source so malformed S10 input cannot look like an unavailable or
 * valid canonical value. */
typedef enum {
    PCGX_TEV_RAW_SOURCE_UNAVAILABLE = 0,
    PCGX_TEV_RAW_SOURCE_COLOR_U8 = 1,
    PCGX_TEV_RAW_SOURCE_COLOR_S10 = 2,
    PCGX_TEV_RAW_SOURCE_KCOLOR_U8 = 3,
    PCGX_TEV_RAW_SOURCE_MALFORMED = 4
} PCGXTevRawSource;

typedef struct {
    int32_t components[4];
    uint8_t valid;
    uint8_t source;
    uint8_t reserved[2];
} PCGXTevRawColor;

/* Setter-owned raw TEV/Indirect provenance.  The legacy float and integer
 * mirrors remain in PCGXState; this sideband records the exact logical input
 * values without borrowing caller storage or reconstructing matrix values
 * from host floats.  A zero known mask means unavailable, not zero. */
#define PC_GX_RAW_TEV_COMPONENT_KNOWN_MASK UINT8_C(0x0F)
#define PC_GX_RAW_TEV_RECORD_KNOWN_MASK UINT32_C(0x0F)
#define PC_GX_RAW_INDIRECT_MATRIX_S0_KNOWN UINT32_C(1) << 0
#define PC_GX_RAW_INDIRECT_MATRIX_T0_KNOWN UINT32_C(1) << 1
#define PC_GX_RAW_INDIRECT_MATRIX_S1_KNOWN UINT32_C(1) << 2
#define PC_GX_RAW_INDIRECT_MATRIX_T1_KNOWN UINT32_C(1) << 3
#define PC_GX_RAW_INDIRECT_MATRIX_S2_KNOWN UINT32_C(1) << 4
#define PC_GX_RAW_INDIRECT_MATRIX_T2_KNOWN UINT32_C(1) << 5
#define PC_GX_RAW_INDIRECT_MATRIX_SCALE_KNOWN UINT32_C(1) << 6
#define PC_GX_RAW_INDIRECT_MATRIX_KNOWN_MASK \
    (PC_GX_RAW_INDIRECT_MATRIX_S0_KNOWN | \
     PC_GX_RAW_INDIRECT_MATRIX_T0_KNOWN | \
     PC_GX_RAW_INDIRECT_MATRIX_S1_KNOWN | \
     PC_GX_RAW_INDIRECT_MATRIX_T1_KNOWN | \
     PC_GX_RAW_INDIRECT_MATRIX_S2_KNOWN | \
     PC_GX_RAW_INDIRECT_MATRIX_T2_KNOWN | \
     PC_GX_RAW_INDIRECT_MATRIX_SCALE_KNOWN)

typedef struct {
    int32_t components[4];
    uint8_t valid;
    uint8_t source;
    uint8_t known_mask;
    uint8_t reserved;
} PCGXRawTevColor;

#define PC_GX_RAW_TEV_STAGE_COLOR_A UINT64_C(1) << 0
#define PC_GX_RAW_TEV_STAGE_COLOR_B UINT64_C(1) << 1
#define PC_GX_RAW_TEV_STAGE_COLOR_C UINT64_C(1) << 2
#define PC_GX_RAW_TEV_STAGE_COLOR_D UINT64_C(1) << 3
#define PC_GX_RAW_TEV_STAGE_ALPHA_A UINT64_C(1) << 4
#define PC_GX_RAW_TEV_STAGE_ALPHA_B UINT64_C(1) << 5
#define PC_GX_RAW_TEV_STAGE_ALPHA_C UINT64_C(1) << 6
#define PC_GX_RAW_TEV_STAGE_ALPHA_D UINT64_C(1) << 7
#define PC_GX_RAW_TEV_STAGE_COLOR_OP UINT64_C(1) << 8
#define PC_GX_RAW_TEV_STAGE_COLOR_BIAS UINT64_C(1) << 9
#define PC_GX_RAW_TEV_STAGE_COLOR_SCALE UINT64_C(1) << 10
#define PC_GX_RAW_TEV_STAGE_COLOR_CLAMP UINT64_C(1) << 11
#define PC_GX_RAW_TEV_STAGE_COLOR_OUT UINT64_C(1) << 12
#define PC_GX_RAW_TEV_STAGE_ALPHA_OP UINT64_C(1) << 13
#define PC_GX_RAW_TEV_STAGE_ALPHA_BIAS UINT64_C(1) << 14
#define PC_GX_RAW_TEV_STAGE_ALPHA_SCALE UINT64_C(1) << 15
#define PC_GX_RAW_TEV_STAGE_ALPHA_CLAMP UINT64_C(1) << 16
#define PC_GX_RAW_TEV_STAGE_ALPHA_OUT UINT64_C(1) << 17
#define PC_GX_RAW_TEV_STAGE_TEX_COORD UINT64_C(1) << 18
#define PC_GX_RAW_TEV_STAGE_TEX_MAP UINT64_C(1) << 19
#define PC_GX_RAW_TEV_STAGE_COLOR_CHAN UINT64_C(1) << 20
#define PC_GX_RAW_TEV_STAGE_K_COLOR_SEL UINT64_C(1) << 21
#define PC_GX_RAW_TEV_STAGE_K_ALPHA_SEL UINT64_C(1) << 22
#define PC_GX_RAW_TEV_STAGE_RAS_SWAP UINT64_C(1) << 23
#define PC_GX_RAW_TEV_STAGE_TEX_SWAP UINT64_C(1) << 24
#define PC_GX_RAW_TEV_STAGE_IND_STAGE UINT64_C(1) << 25
#define PC_GX_RAW_TEV_STAGE_IND_FORMAT UINT64_C(1) << 26
#define PC_GX_RAW_TEV_STAGE_IND_BIAS UINT64_C(1) << 27
#define PC_GX_RAW_TEV_STAGE_IND_MTX UINT64_C(1) << 28
#define PC_GX_RAW_TEV_STAGE_IND_WRAP_S UINT64_C(1) << 29
#define PC_GX_RAW_TEV_STAGE_IND_WRAP_T UINT64_C(1) << 30
#define PC_GX_RAW_TEV_STAGE_IND_ADD_PREV UINT64_C(1) << 31
#define PC_GX_RAW_TEV_STAGE_IND_LOD UINT64_C(1) << 32
#define PC_GX_RAW_TEV_STAGE_IND_ALPHA UINT64_C(1) << 33
#define PC_GX_RAW_TEV_STAGE_KNOWN_MASK UINT64_C(0x00000003FFFFFFFF)

typedef struct {
    AcgcGxCanonicalTevStage value;
    uint64_t known_mask;
} PCGXRawTevStage;

typedef struct {
    AcgcGxCanonicalTevSwapTable value;
    uint32_t known_mask;
} PCGXRawTevSwapTable;

typedef struct {
    AcgcGxCanonicalIndirectOrder value;
    uint32_t known_mask;
} PCGXRawIndirectOrder;

typedef struct {
    AcgcGxCanonicalIndirectMatrix value;
    uint32_t known_mask;
} PCGXRawIndirectMatrix;

typedef struct {
    uint32_t active_tev_stage_count;
    uint32_t active_tev_stage_count_known;
    uint32_t active_indirect_stage_count;
    uint32_t active_indirect_stage_count_known;
    uint32_t invalid;
    PCGXRawTevStage stages[ACGC_GX_CANONICAL_TEV_STAGE_COUNT];
    PCGXRawTevColor registers[ACGC_GX_CANONICAL_TEV_REGISTER_COUNT];
    PCGXRawTevColor konst[ACGC_GX_CANONICAL_TEV_KONST_COUNT];
    PCGXRawTevSwapTable swap_tables[
        ACGC_GX_CANONICAL_TEV_SWAP_TABLE_COUNT];
    PCGXRawIndirectOrder orders[
        ACGC_GX_CANONICAL_INDIRECT_ORDER_CAPACITY];
    PCGXRawIndirectMatrix matrices[
        ACGC_GX_CANONICAL_INDIRECT_MATRIX_CAPACITY];
} PCGXRawTevIndirect;

/*
 * Borrowed CPU texture source metadata for a future synchronous V2 binder.
 * The image/TLUT pointers are host pointers (never packed u32 handles), but
 * this record owns neither the pointed-to bytes nor their lifetime.  A
 * consumer may only use a record during the synchronous GX handoff that
 * established its generation token.
 */
typedef enum {
    PCGX_TEXTURE_SOURCE_NONE = 0,
    PCGX_TEXTURE_SOURCE_RAW_GUEST = 1,
    PCGX_TEXTURE_SOURCE_EMU64_CONVERTED = 2
} PCGXTextureSourceKind;

typedef struct {
    const void* image_ptr;
    uint32_t image_byte_size;
    const void* tlut_ptr;
    uint32_t tlut_byte_size;
    uint32_t tlut_format;
    uint32_t tlut_entries;
    uint32_t tlut_name;
    uint32_t tlut_is_be;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t wrap_s;
    uint32_t wrap_t;
    uint32_t min_filter;
    uint32_t mag_filter;
    uint32_t effective_filter;
    uint32_t source_kind;
    uint32_t tlut_source_kind;
    uint64_t generation;
} PCGXTextureSource;

/* Setter-owned raw Transform provenance.  These records are deliberately
 * separate from the host-oriented float matrices below: the future 0x0002
 * producer must consume the six GX projection words and exact matrix words,
 * never a widescreen-adjusted reconstruction. */
#define PC_GX_TRANSFORM_POSITION_COUNT 10
#define PC_GX_TRANSFORM_POSITION_WORDS 12
#define PC_GX_TRANSFORM_NORMAL_WORDS 9
#define PC_GX_TRANSFORM_PROJECTION_WORDS 6

typedef struct {
    uint32_t type;
    uint32_t coefficients[PC_GX_TRANSFORM_PROJECTION_WORDS];
    uint8_t known;
    uint8_t reserved[3];
} PCGXRawProjection;

typedef struct {
    uint32_t words[PC_GX_TRANSFORM_POSITION_WORDS];
    uint8_t known;
    uint8_t reserved[3];
} PCGXRawPositionMatrix;

typedef struct {
    uint32_t words[PC_GX_TRANSFORM_NORMAL_WORDS];
    uint8_t known;
    uint8_t reserved[3];
} PCGXRawNormalMatrix;

typedef struct {
    PCGXRawProjection projection;
    PCGXRawPositionMatrix position[PC_GX_TRANSFORM_POSITION_COUNT];
    PCGXRawNormalMatrix normal[PC_GX_TRANSFORM_POSITION_COUNT];
    uint32_t current_position_id;
    uint8_t current_position_known;
    /* Indexed uncertainty is slot-owned and clears only on finite immediate overwrite. */
    uint8_t position_indexed_unresolved[PC_GX_TRANSFORM_POSITION_COUNT];
    uint8_t normal_indexed_unresolved[PC_GX_TRANSFORM_POSITION_COUNT];
    uint8_t invalid; /* sticky until pc_gx_init */
    uint8_t reserved[3];
} PCGXRawTransform;

/* Setter-owned raw Depth provenance.  The host-facing z_* fields below may
 * retain legacy defaults or OpenGL-oriented values; this sideband is known
 * only after a valid GXSetZMode call owns the logical triple. */
typedef struct {
    uint32_t compare_enable;
    uint32_t compare_func;
    uint32_t update_enable;
    uint8_t known;
    uint8_t reserved[3];
} PCGXRawDepth;

/* Setter-owned raw Alpha provenance.  The value uses the existing canonical
 * Alpha field order; knownness is tracked per setter-owned word so a
 * partially observed sequence fails closed instead of inheriting host
 * defaults. */
#define PC_GX_RAW_ALPHA_KNOWN_COMP0                 (UINT32_C(1) << 0)
#define PC_GX_RAW_ALPHA_KNOWN_REF0                  (UINT32_C(1) << 1)
#define PC_GX_RAW_ALPHA_KNOWN_OP                    (UINT32_C(1) << 2)
#define PC_GX_RAW_ALPHA_KNOWN_COMP1                 (UINT32_C(1) << 3)
#define PC_GX_RAW_ALPHA_KNOWN_REF1                  (UINT32_C(1) << 4)
#define PC_GX_RAW_ALPHA_KNOWN_COLOR_UPDATE          (UINT32_C(1) << 5)
#define PC_GX_RAW_ALPHA_KNOWN_ALPHA_UPDATE          (UINT32_C(1) << 6)
#define PC_GX_RAW_ALPHA_KNOWN_Z_COMP_LOC            (UINT32_C(1) << 7)
#define PC_GX_RAW_ALPHA_KNOWN_ALL                   UINT32_C(0xFF)

typedef struct {
    AcgcGxCanonicalAlphaState value;
    uint32_t known_mask;
    uint32_t invalid; /* sticky until pc_gx_init */
} PCGXRawAlpha;

/* Setter-owned raw Raster provenance.  The canonical value is kept separate
 * from the host viewport/scissor/cull state so a later producer can preserve
 * logical caller values and reject an incomplete or invalid setter epoch. */
#define PC_GX_RAW_RASTER_KNOWN_VIEWPORT_LEFT       (UINT32_C(1) << 0)
#define PC_GX_RAW_RASTER_KNOWN_VIEWPORT_TOP        (UINT32_C(1) << 1)
#define PC_GX_RAW_RASTER_KNOWN_VIEWPORT_WIDTH      (UINT32_C(1) << 2)
#define PC_GX_RAW_RASTER_KNOWN_VIEWPORT_HEIGHT     (UINT32_C(1) << 3)
#define PC_GX_RAW_RASTER_KNOWN_VIEWPORT_NEAR       (UINT32_C(1) << 4)
#define PC_GX_RAW_RASTER_KNOWN_VIEWPORT_FAR        (UINT32_C(1) << 5)
#define PC_GX_RAW_RASTER_KNOWN_SCISSOR_LEFT        (UINT32_C(1) << 6)
#define PC_GX_RAW_RASTER_KNOWN_SCISSOR_TOP         (UINT32_C(1) << 7)
#define PC_GX_RAW_RASTER_KNOWN_SCISSOR_WIDTH       (UINT32_C(1) << 8)
#define PC_GX_RAW_RASTER_KNOWN_SCISSOR_HEIGHT      (UINT32_C(1) << 9)
#define PC_GX_RAW_RASTER_KNOWN_SCISSOR_OFFSET_X    (UINT32_C(1) << 10)
#define PC_GX_RAW_RASTER_KNOWN_SCISSOR_OFFSET_Y    (UINT32_C(1) << 11)
#define PC_GX_RAW_RASTER_KNOWN_CLIP_MODE           (UINT32_C(1) << 12)
#define PC_GX_RAW_RASTER_KNOWN_CULL_MODE           (UINT32_C(1) << 13)
#define PC_GX_RAW_RASTER_KNOWN_CO_PLANAR           (UINT32_C(1) << 14)
#define PC_GX_RAW_RASTER_KNOWN_LINE_WIDTH          (UINT32_C(1) << 15)
#define PC_GX_RAW_RASTER_KNOWN_LINE_TEX_OFFSET     (UINT32_C(1) << 16)
#define PC_GX_RAW_RASTER_KNOWN_POINT_SIZE          (UINT32_C(1) << 17)
#define PC_GX_RAW_RASTER_KNOWN_POINT_TEX_OFFSET    (UINT32_C(1) << 18)
#define PC_GX_RAW_RASTER_KNOWN_LINE_TEXCOORD_MASK  (UINT32_C(1) << 19)
#define PC_GX_RAW_RASTER_KNOWN_POINT_TEXCOORD_MASK (UINT32_C(1) << 20)
#define PC_GX_RAW_RASTER_KNOWN_DITHER              (UINT32_C(1) << 21)
#define PC_GX_RAW_RASTER_KNOWN_DST_ALPHA_ENABLE    (UINT32_C(1) << 22)
#define PC_GX_RAW_RASTER_KNOWN_DST_ALPHA           (UINT32_C(1) << 23)
#define PC_GX_RAW_RASTER_KNOWN_FIELD_MODE          (UINT32_C(1) << 24)
#define PC_GX_RAW_RASTER_KNOWN_HALF_ASPECT         (UINT32_C(1) << 25)
#define PC_GX_RAW_RASTER_KNOWN_FIELD_ODD_MASK      (UINT32_C(1) << 26)
#define PC_GX_RAW_RASTER_KNOWN_FIELD_EVEN_MASK     (UINT32_C(1) << 27)
#define PC_GX_RAW_RASTER_KNOWN_ALL                  UINT32_C(0x0FFFFFFF)
#define PC_GX_RAW_RASTER_TEXCOORD_KNOWN_ALL        UINT32_C(0x000000FF)

typedef struct {
    AcgcGxCanonicalRasterState value;
    uint32_t known_mask;
    uint32_t line_texcoord_known_mask;
    uint32_t point_texcoord_known_mask;
    uint32_t invalid; /* sticky until pc_gx_init */
} PCGXRawRaster;

/* Setter-owned raw Channels provenance.  The legacy float/int lighting
 * arrays below remain the Windows/OpenGL host state; these records are the
 * only source for a future cumulative canonical Channels section. */
#define PC_GX_RAW_CHANNEL_RECORD_COUNT UINT32_C(2)

#define PC_GX_RAW_CHANNEL_CONTROL_KNOWN_ENABLE       (UINT32_C(1) << 0)
#define PC_GX_RAW_CHANNEL_CONTROL_KNOWN_AMBIENT_SRC  (UINT32_C(1) << 1)
#define PC_GX_RAW_CHANNEL_CONTROL_KNOWN_MATERIAL_SRC (UINT32_C(1) << 2)
#define PC_GX_RAW_CHANNEL_CONTROL_KNOWN_LIGHT_MASK   (UINT32_C(1) << 3)
#define PC_GX_RAW_CHANNEL_CONTROL_KNOWN_DIFFUSE     (UINT32_C(1) << 4)
#define PC_GX_RAW_CHANNEL_CONTROL_KNOWN_ATTENUATION (UINT32_C(1) << 5)
#define PC_GX_RAW_CHANNEL_CONTROL_KNOWN_ALL          UINT32_C(0x3F)
#define PC_GX_RAW_CHANNEL_CONTROL_TARGET_COLOR       UINT32_C(1)
#define PC_GX_RAW_CHANNEL_CONTROL_TARGET_ALPHA       UINT32_C(2)
#define PC_GX_RAW_CHANNEL_CONTROL_TARGET_BOTH        UINT32_C(3)

#define PC_GX_RAW_CHANNEL_COMPONENT_R (UINT32_C(1) << 0)
#define PC_GX_RAW_CHANNEL_COMPONENT_G (UINT32_C(1) << 1)
#define PC_GX_RAW_CHANNEL_COMPONENT_B (UINT32_C(1) << 2)
#define PC_GX_RAW_CHANNEL_COMPONENT_A (UINT32_C(1) << 3)
#define PC_GX_RAW_CHANNEL_COMPONENT_ALL UINT32_C(0x0F)

typedef struct {
    uint32_t enable;
    uint32_t ambient_source;
    uint32_t material_source;
    uint32_t light_mask;
    uint32_t diffuse_function;
    uint32_t attenuation_function;
    uint32_t known_mask;
} PCGXRawChannelControl;

typedef struct {
    PCGXRawChannelControl color;
    PCGXRawChannelControl alpha;
    uint32_t ambient_rgba8;
    uint32_t ambient_known_mask;
    uint32_t material_rgba8;
    uint32_t material_known_mask;
} PCGXRawChannelRecord;

typedef struct {
    uint32_t active_count;
    uint32_t active_count_known;
    uint32_t invalid; /* sticky until pc_gx_init */
    PCGXRawChannelRecord records[PC_GX_RAW_CHANNEL_RECORD_COUNT];
} PCGXRawChannels;

/* Setter-owned raw Lighting provenance.  These eight records are complete GX
 * register-order values, never GXLightObj pointers or object indices.  A
 * complete immediate load owns a slot; indexed loads keep it unresolved until
 * a later immediate load overwrites that exact slot. */
#define PC_GX_RAW_LIGHTING_SLOT_COUNT UINT32_C(8)

#define PC_GX_RAW_LIGHTING_KNOWN_COLOR      (UINT32_C(1) << 0)
#define PC_GX_RAW_LIGHTING_KNOWN_ANGULAR    (UINT32_C(1) << 1)
#define PC_GX_RAW_LIGHTING_KNOWN_DISTANCE   (UINT32_C(1) << 2)
#define PC_GX_RAW_LIGHTING_KNOWN_POSITION   (UINT32_C(1) << 3)
#define PC_GX_RAW_LIGHTING_KNOWN_DIRECTION  (UINT32_C(1) << 4)
#define PC_GX_RAW_LIGHTING_KNOWN_ALL       UINT32_C(0x1F)

typedef struct {
    AcgcGxCanonicalLightingRecord value;
    uint8_t known_mask;
    uint8_t invalid_mask;
    uint8_t reserved[2];
    uint64_t generation;
} PCGXRawLightingSlot;

typedef struct {
    uint64_t next_generation;
    uint8_t loaded_mask;
    uint8_t unresolved_indexed_mask;
    uint8_t known;
    uint8_t invalid; /* sticky until pc_gx_init */
    uint32_t reserved;
    PCGXRawLightingSlot slots[PC_GX_RAW_LIGHTING_SLOT_COUNT];
} PCGXRawLighting;

/* Setter-owned raw Texgen/SU provenance.  These records are deliberately
 * independent of the host OpenGL texture-generator and matrix arrays below.
 * A later producer may therefore distinguish a guest value that was owned by
 * a setter from a host default, and may reject an unresolved indexed range
 * without losing unrelated words in the same logical matrix slot. */
#define PC_GX_TEXGEN_COUNT                 8
#define PC_GX_TEXGEN_ORDINARY_MATRIX_COUNT 11
#define PC_GX_TEXGEN_POST_MATRIX_COUNT     21
#define PC_GX_TEXGEN_MATRIX_WORD_COUNT     12

#define PC_GX_TEXGEN_KNOWN_FUNCTION        (1u << 0)
#define PC_GX_TEXGEN_KNOWN_SOURCE          (1u << 1)
#define PC_GX_TEXGEN_KNOWN_ORDINARY_MTX    (1u << 2)
#define PC_GX_TEXGEN_KNOWN_NORMALIZE       (1u << 3)
#define PC_GX_TEXGEN_KNOWN_POST_MTX        (1u << 4)
#define PC_GX_TEXGEN_KNOWN_ALL             0x1Fu

#define PC_GX_TEXGEN_SU_KNOWN_MANUAL       (1u << 0)
#define PC_GX_TEXGEN_SU_KNOWN_SCALE_S      (1u << 1)
#define PC_GX_TEXGEN_SU_KNOWN_SCALE_T      (1u << 2)
#define PC_GX_TEXGEN_SU_KNOWN_BIAS_S       (1u << 3)
#define PC_GX_TEXGEN_SU_KNOWN_BIAS_T       (1u << 4)
#define PC_GX_TEXGEN_SU_KNOWN_CYLINDER_S   (1u << 5)
#define PC_GX_TEXGEN_SU_KNOWN_CYLINDER_T   (1u << 6)

typedef enum {
    PC_GX_TEXGEN_MATRIX_PROVENANCE_NONE = 0,
    PC_GX_TEXGEN_MATRIX_PROVENANCE_IMMEDIATE = 1,
    PC_GX_TEXGEN_MATRIX_PROVENANCE_INDEXED_UNRESOLVED = 2,
    PC_GX_TEXGEN_MATRIX_PROVENANCE_INVALID = 3
} PCGXRawTexMatrixProvenance;

typedef struct {
    uint32_t function;
    uint32_t source;
    uint32_t ordinary_matrix_id;
    uint32_t normalize;
    uint32_t post_matrix_id;
    uint32_t component_known;
} PCGXRawTexgenRecord;

typedef struct {
    /* logical_id is the fixed canonical domain member for this record. */
    uint32_t logical_id;
    /* slot_known is setter/provenance knownness, not word knownness. */
    uint32_t slot_known;
    uint32_t provenance;
    uint32_t last_load_type;
    /* 0 means no written/attempted range and therefore no known-word bits. */
    uint32_t last_written_word_count;
    uint32_t known_word_mask;
    uint32_t words[PC_GX_TEXGEN_MATRIX_WORD_COUNT];
} PCGXRawTexMatrix;

typedef struct {
    uint32_t manual_enable;
    uint16_t scale_s_raw_u16;
    uint16_t scale_t_raw_u16;
    uint32_t bias_s;
    uint32_t bias_t;
    uint32_t cylinder_s;
    uint32_t cylinder_t;
    uint32_t component_known;
} PCGXRawTexcoordSU;

typedef struct {
    uint32_t active_texgen_count;
    uint32_t active_texgen_count_known;
    uint32_t invalid;
    PCGXRawTexgenRecord texgen[PC_GX_TEXGEN_COUNT];
    PCGXRawTexMatrix ordinary[PC_GX_TEXGEN_ORDINARY_MATRIX_COUNT];
    PCGXRawTexMatrix post[PC_GX_TEXGEN_POST_MATRIX_COUNT];
    PCGXRawTexcoordSU su[PC_GX_TEXGEN_COUNT];
} PCGXRawTexgen;

typedef struct {
    uint32_t vcd_type;
    uint32_t vat_count;
    uint32_t vat_type;
    uint32_t vat_fraction;
    uint8_t vcd_known;
    uint8_t vat_known;
    uint8_t reserved[2];
} PCGXRawGeometryFormat;

typedef struct {
    /* GXSetArray sources are borrowed only for synchronous indexed emission.
     * Each accepted element is copied into the live batch immediately; the
     * completed batch retains no source pointer. */
    uint64_t generation;
    uint32_t byte_size;
    uint32_t stride;
    uint8_t known;
    uint8_t data_known;
    uint8_t reserved[2];
} PCGXRawGeometryArray;

typedef struct {
    uint32_t raw_words[PC_GX_MAX_ATTR][PC_GX_GEOMETRY_MAX_VALUE_WORDS];
    uint32_t source_index[PC_GX_MAX_ATTR];
    uint32_t source_index_known_mask;
    uint32_t attribute_known_mask;
    uint32_t invalid;
} PCGXRawGeometryCurrentVertex;

typedef struct {
    uint32_t vcd_type;
    uint32_t vat_count;
    uint32_t vat_type;
    uint32_t vat_fraction;
    uint32_t descriptor_known;
    uint32_t array_known;
    uint64_t array_generation;
    uint32_t array_byte_size;
    uint32_t array_stride;
    uint32_t value_word_count;
    uint32_t value_count;
    uint32_t index_count;
    uint32_t index_stride;
    uint32_t value_source_index[PC_GX_GEOMETRY_MAX_VERTICES];
    uint32_t value_words[PC_GX_GEOMETRY_MAX_VERTICES]
        [PC_GX_GEOMETRY_MAX_VALUE_WORDS];
    uint32_t index_values[PC_GX_GEOMETRY_MAX_VERTICES];
    uint32_t source_indices[PC_GX_GEOMETRY_MAX_VERTICES];
    uint8_t value_known[PC_GX_GEOMETRY_MAX_VERTICES];
    uint8_t index_known[PC_GX_GEOMETRY_MAX_VERTICES];
    uint8_t reserved[2];
} PCGXRawGeometryAttribute;

typedef struct {
    uint32_t primitive;
    uint32_t vertex_count;
    uint32_t vtxfmt;
    uint32_t expected_vertex_count;
    uint32_t active;
    uint32_t known;
    uint32_t invalid;
    PCGXRawGeometryAttribute attr[PC_GX_MAX_ATTR];
} PCGXRawGeometryBatch;

typedef struct {
    PCGXRawGeometryFormat format[PC_GX_MAX_VTXFMT][PC_GX_MAX_ATTR];
    PCGXRawGeometryArray array[PC_GX_MAX_ATTR];
    uint64_t next_array_generation;
    uint32_t invalid;
    uint32_t reserved;
    PCGXRawGeometryCurrentVertex current;
    PCGXRawGeometryBatch live;
    /* completed is an owned, pointer-free snapshot valid until the next raw
     * capture or state reset.  A consumer must copy or consume it
     * synchronously; it may not retain a pointer across the next capture. */
    PCGXRawGeometryBatch completed;
} PCGXRawGeometry;

/* Uniform locations for one GL program */
typedef struct {
    GLint projection, modelview, normal_mtx;
    GLint tev_prev, tev_reg0, tev_reg1, tev_reg2;
    GLint num_tev_stages;
    GLint tev_color_in[PC_GX_MAX_TEV_STAGES], tev_alpha_in[PC_GX_MAX_TEV_STAGES];
    GLint tev_color_op[PC_GX_MAX_TEV_STAGES], tev_alpha_op[PC_GX_MAX_TEV_STAGES];
    GLint kcolor, tev_ksel;
    GLint alpha_ctrl, alpha_refs;
    GLint lighting_enabled, mat_color, amb_color;
    GLint chan_mat_src, chan_amb_src, num_chans;
    GLint alpha_lighting_enabled, alpha_mat_src;
    GLint light_mask, light_pos[8], light_color[8];
    GLint texmtx_enable[2], texmtx_row0[2], texmtx_row1[2], texgen_src[2];
    GLint use_texture0, use_texture1, use_texture2;
    GLint texture0, texture1, texture2;
    GLint tev_tc_src[PC_GX_MAX_TEV_STAGES];
    GLint num_ind_stages;
    GLint ind_tex[4], ind_scale[4];
    GLint ind_mtx_r0[PC_GX_MAX_TEV_STAGES], ind_mtx_r1[PC_GX_MAX_TEV_STAGES];
    GLint tev_ind_cfg[PC_GX_MAX_TEV_STAGES], tev_ind_wrap[PC_GX_MAX_TEV_STAGES];
    GLint fog_type, fog_enable, fog_start, fog_end, fog_color;
    GLint tev_bsc[PC_GX_MAX_TEV_STAGES], tev_out[PC_GX_MAX_TEV_STAGES];
    GLint swap_table;
    GLint tev_swap[PC_GX_MAX_TEV_STAGES];
} PCGXUloc;

typedef struct {
    /* Primitive assembly */
    int current_primitive;
    int current_vtxfmt;
    int vertex_count;
    int expected_vertex_count;
    int in_begin;
    PCGXVertex vertex_buffer[PC_GX_MAX_VERTS];
    int current_vertex_idx;
    PCGXVertex current_vertex;
    /* Deferred draw stuff*/
    int pending_verts;
    int pending_prim;

    /* Vertex descriptor */
    int vtx_desc[PC_GX_MAX_ATTR];
    PCGXVertexFormat vtx_fmt[PC_GX_MAX_VTXFMT];

    /* Transforms */
    float projection_mtx[4][4];
    int projection_type;
    float pos_mtx[10][3][4];
    float nrm_mtx[10][3][3];
    float tex_mtx[10][3][4];
    int current_mtx;
    PCGXRawTransform raw_transform;
    PCGXRawAlpha raw_alpha;
    PCGXRawRaster raw_raster;
    PCGXRawDepth raw_depth;
    PCGXRawChannels raw_channels;
    PCGXRawLighting raw_lighting;
    PCGXRawTexgen raw_texgen;
    PCGXRawGeometry raw_geometry;

    /* Viewport & scissor */
    float viewport[6];  /* x, y, w, h, near, far */
    int scissor[4];     /* left, top, w, h */

    /* TEV */
    int num_tev_stages;
    PCGXTevStage tev_stages[16];
    float tev_colors[4][4];    /* PREV, REG0, REG1, REG2 */
    float tev_k_colors[4][4];
    PCGXTevRawColor tev_raw_colors[4];    /* PREV, REG0, REG1, REG2 */
    PCGXTevRawColor tev_raw_k_colors[4]; /* K0, K1, K2, K3 */
    PCGXTevSwapTable tev_swap_table[4];
    PCGXRawTevIndirect raw_tev_indirect;

    /* Textures */
    int num_tex_gens;
    int tex_gen_type[8];
    int tex_gen_src[8];
    int tex_gen_mtx[8];
    GLuint gl_textures[8];
    int tex_obj_w[8];
    int tex_obj_h[8];
    int tex_obj_fmt[8];
    PCGXTextureSource texture_sources[8];

    /* Lighting */
    int num_chans;
    float chan_amb_color[2][4];
    float chan_mat_color[2][4];
    int chan_ctrl_enable[4];
    int chan_ctrl_amb_src[4];
    int chan_ctrl_mat_src[4];
    int chan_ctrl_light_mask[4];
    int chan_ctrl_diff_fn[4];
    int chan_ctrl_attn_fn[4];

    struct {
        float pos[3];
        float dir[3];
        float color[4];
        float a0, a1, a2;  /* angular attenuation */
        float k0, k1, k2;  /* distance attenuation */
    } lights[8];

    /* Blend & depth */
    int blend_mode;
    int blend_src;
    int blend_dst;
    int blend_logic_op;
    int z_compare_enable;
    int z_compare_func;
    int z_update_enable;
    int color_update_enable;
    int alpha_update_enable;

    /* Alpha compare */
    int alpha_comp0;
    int alpha_ref0;
    int alpha_op;
    int alpha_comp1;
    int alpha_ref1;

    int cull_mode;

    /* Fog */
    int fog_type;
    float fog_start, fog_end, fog_near, fog_far;
    float fog_color[4];

    /* TLUT palette storage for CI4/CI8 textures */
    struct {
        const void* data;
        int format;      /* GX_TL_IA8=0, GX_TL_RGB5A3=1 */
        int n_entries;
        int is_be;       /* 1=big-endian (ROM/JSystem), 0=native LE (emu64 tlutconv) */
    } tlut[16];

    /* Indirect textures */
    int num_ind_stages;
    struct {
        int tex_coord;
        int tex_map;
        int scale_s;
        int scale_t;
    } ind_order[4];
    float ind_mtx[3][2][3];
    int   ind_mtx_scale[3];

    /* Deferred vertex commit: position starts vertex, commit on next position or GXEnd */
    int vertex_pending;

    /* GL objects */
    GLuint vao;
    GLuint vbo;
    GLuint ebo;
    GLuint current_shader;

    /* Uniform locations (looked up once per program at link time) */
    PCGXUloc uloc;

    float clear_color[4];
    float clear_depth;

    /* Copy/framebuffer */
    int copy_src[4];       /* left, top, w, h */
    int copy_dst[2];       /* w, h */
    int tex_copy_src[4];
    int tex_copy_dst[2];
    unsigned int tex_copy_fmt;
    int tex_copy_mipmap;

    /* Indexed vertex data */
    const void* array_base[PC_GX_MAX_ATTR];
    uint32_t array_size[PC_GX_MAX_ATTR];
    unsigned char array_stride[PC_GX_MAX_ATTR];

    unsigned int dirty;

    /* Per-group dirty sequence: bumped by DIRTY(), used to detect uniforms
     * that went stale in a program while another program was bound */
    unsigned int group_seq[PC_GX_NUM_DIRTY_GROUPS];
    unsigned int seq_counter;

} PCGXState;

extern PCGXState g_gx;

/* Focused fixture seam: returns the setter-owned shadow without granting a
 * producer or consumer write access. */
const PCGXRawAlpha* pc_gx_raw_alpha_shadow_fixture(void);
const PCGXRawRaster* pc_gx_raw_raster_shadow_fixture(void);
const PCGXRawDepth* pc_gx_raw_depth_shadow_fixture(void);
const PCGXRawChannels* pc_gx_raw_channels_shadow_fixture(void);
const PCGXRawLighting* pc_gx_raw_lighting_shadow_fixture(void);
const PCGXRawTexgen* pc_gx_raw_texgen_shadow_fixture(void);
const PCGXRawGeometry* pc_gx_raw_geometry_shadow_fixture(void);
int pc_gx_raw_alpha_build_canonical(
    AcgcGxCanonicalAlphaState* destination
);
int pc_gx_raw_raster_build_canonical(
    AcgcGxCanonicalRasterState* destination
);
int pc_gx_raw_texgen_shadow_valid_fixture(void);
void pc_gx_raw_texgen_shadow_reset_fixture(void);

void pc_gx_tev_seq_reset(void);

static inline void pc_gx_dirty_set(unsigned int flags) {
    unsigned int f = flags & PC_GX_DIRTY_UNIFORM_GROUPS;
    g_gx.dirty |= flags;
    if (f) {
        unsigned int seq = ++g_gx.seq_counter;
        /* 0xFFFFFFFF is the never-uploaded sentinel: on wrap restart the epoch */
        if (seq == 0xFFFFFFFFu) {
            pc_gx_tev_seq_reset();
            seq = g_gx.seq_counter = 1;
        }
        do {
            g_gx.group_seq[__builtin_ctz(f)] = seq;
            f &= f - 1;
        } while (f);
    }
}
#define PC_GX_DIRTY_SET(flag) pc_gx_dirty_set(flag)
#define DIRTY(flag) pc_gx_dirty_set(flag)

/* Normalize a TEV stage's texcoord source for the shader's vec2 tc[2]:
 * NULL/invalid falls back to the stage number, then clamps to channel 1.
 * Shared by the uber uniform upload and specialized key construction so
 * the two paths cannot diverge (an OOB tc index is UB in the uber shader). */
static inline int pc_gx_tc_src_normalize(int tc, int stage) {
    if (tc < 0 || tc >= 8) tc = stage;
    if (tc > 1) tc = 1;
    return tc;
}

/* Shader specialization */

/* Config-shaped GX state folded into compile-time constants. */
typedef struct {
    u8 num_stages;
    u8 num_ind;
    u8 fog_enable;
    u8 alpha[3];      /* comp0, op, comp1 - refs stay uniforms (runtime values) */
    u8 light[7];      /* en0, mat_src0, amb_src0, num_chans, en1, mat_src1, mask */
    u8 swap_tbl[16];  /* 4 tables x rgba channel indices */
    struct {
        u8 cin[4], ain[4];
        u8 cop, aop;
        u8 bsc[4];    /* color bias/scale, alpha bias/scale */
        u8 outc[4];   /* color clamp, alpha clamp, color out, alpha out */
        u8 swap[2];   /* ras, tex */
        u8 ksel[2];
        u8 tc_src;
        u8 use_tex;
        u8 ind[7];    /* stage, mtx, bias, alpha, wrap_s, wrap_t, add_prev */
    } st[PC_GX_MAX_TEV_STAGES];
} PCGXShaderKey;

typedef struct {
    int used;
    PCGXShaderKey key;
    GLuint prog;
    PCGXUloc uloc;
    /* group_seq value at last upload of each group to this program;
     * 0xFFFFFFFF = never uploaded */
    unsigned int uploaded_seq[PC_GX_NUM_DIRTY_GROUPS];
} PCGXShaderVariant;

/* --- Internal functions --- */
typedef void (*PCGXSemanticPacketHandoffCallback)(
    void* context,
    const AcgcGxSemanticPacket* packet
);

void pc_gx_init(void);
void pc_gx_shutdown(void);
void pc_gx_flush_vertices(void);
void pc_gx_flush_if_begin_complete(void);
void pc_gx_draw_pending(void);

/* Raw Channels producer seam.  The setter helpers are called only after the
 * existing completed-batch flush boundary and before legacy mutation. */
void pc_gx_raw_channels_initialize(void);
void pc_gx_raw_channels_set_num(uint32_t count);
void pc_gx_raw_channels_set_control(
    uint32_t channel,
    uint32_t enable,
    uint32_t ambient_source,
    uint32_t material_source,
    uint32_t light_mask,
    uint32_t diffuse_function,
    uint32_t attenuation_function
);
void pc_gx_raw_channels_set_color(
    uint32_t channel,
    uint32_t color_packed,
    int material
);
int pc_gx_raw_channels_build_canonical(
    AcgcGxCanonicalChannelState* destination
);

/* Raw Lighting producer seam.  Constructor calls remain caller-object-only;
 * load calls own the cumulative slot state. */
void pc_gx_raw_lighting_initialize(void);
void pc_gx_raw_lighting_load_immediate(void* light_object, uint32_t light);
void pc_gx_raw_lighting_load_indexed(uint32_t object_index, uint32_t light);
int pc_gx_raw_lighting_build_canonical(
    AcgcGxCanonicalLightingState* destination
);

/* Install an optional value-only observer at the first GX flush boundary. */
void pc_gx_set_semantic_packet_handoff(
    PCGXSemanticPacketHandoffCallback callback,
    void* context
);
void pc_gx_clear_semantic_packet_handoff(void);

#ifdef PC_GX_ALPHA_RAW_SHADOW_FIXTURE
/* Test-target-only observation at the existing synchronous flush boundary.
 * The callback cannot intercept, cancel, or otherwise alter the normal flush. */
typedef void (*PCGXAlphaFlushFixtureObserver)(void* context);
void pc_gx_set_alpha_flush_fixture_observer(
    PCGXAlphaFlushFixtureObserver observer,
    void* context
);
void pc_gx_clear_alpha_flush_fixture_observer(void);
#endif

#ifdef PC_GX_RASTER_RAW_SHADOW_FIXTURE
/* Test-target-only observation at the existing synchronous flush boundary.
 * The callback cannot intercept, cancel, or otherwise alter the normal flush. */
typedef void (*PCGXRasterFlushFixtureObserver)(void* context);
void pc_gx_set_raster_flush_fixture_observer(
    PCGXRasterFlushFixtureObserver observer,
    void* context
);
void pc_gx_clear_raster_flush_fixture_observer(void);
#endif

#ifdef PC_GX_DEPTH_RAW_SHADOW_FIXTURE
/* Test-target-only observation at the existing synchronous flush boundary.
 * The callback cannot intercept, cancel, or otherwise alter the normal flush. */
typedef void (*PCGXDepthFlushFixtureObserver)(void* context);
void pc_gx_set_depth_flush_fixture_observer(
    PCGXDepthFlushFixtureObserver observer,
    void* context
);
void pc_gx_clear_depth_flush_fixture_observer(void);
#endif

#ifdef PC_GX_TEXGEN_RAW_SHADOW_FIXTURE
/* Test-target-only observation at the existing synchronous flush boundary.
 * The callback cannot intercept, cancel, or otherwise alter the normal flush. */
typedef void (*PCGXTexgenFlushFixtureObserver)(void* context);
void pc_gx_set_texgen_flush_fixture_observer(
    PCGXTexgenFlushFixtureObserver observer,
    void* context
);
void pc_gx_clear_texgen_flush_fixture_observer(void);
#endif

#ifdef PC_GX_GEOMETRY_RAW_BATCH_FIXTURE
/* Observation-only seam at the existing synchronous flush boundary.  The
 * callback is void and cannot cancel, replace, or reorder the normal flush. */
typedef void (*PCGXGeometryFlushFixtureObserver)(void* context);
void pc_gx_set_geometry_flush_fixture_observer(
    PCGXGeometryFlushFixtureObserver observer,
    void* context
);
void pc_gx_clear_geometry_flush_fixture_observer(void);
#endif

/* Copy the current borrowed CPU source metadata for one V2 texture map.
 * No source bytes are read.  The copied pointers remain borrowed and are
 * valid only while the returned generation remains current. */
int pc_gx_get_v2_texture_source(int map, PCGXTextureSource* destination);

#ifdef PC_DARWIN_COMPILE_AUDIT
/* Focused source-record fixture hook; it copies metadata only. */
int pc_gx_texture_source_fixture_store(
    int map,
    const PCGXTextureSource* candidate
);
#endif

/* Testable, renderer-neutral packet gate used by pc_gx_flush_vertices(). */
int pc_gx_try_handoff_semantic_vertices(
    int first_vertex,
    int vertex_count
);

void pc_gx_texture_bind_cache_invalidate(void);
void pc_gx_viewport_state_invalidate(void);

/* PC has no guest-memory owner for indexed GX matrix loads.  These entry
 * points retain that limitation explicitly instead of inventing matrix data. */
void GXLoadPosMtxIndx(u16 mtx_indx, u32 id);
void GXLoadNrmMtxIndx3x3(u16 mtx_indx, u32 id);
void GXLoadTexMtxIndx(u16 mtx_indx, u32 id, u32 type);
void GXLoadNrmMtxImm3x3(const void* mtx, u32 id);
void GXSetProjectionv(const float* values);

#ifdef PC_DARWIN_COMPILE_AUDIT
void pc_gx_transform_fixture_set_aspect(int active, float factor);
#endif

/* TEV shader */
PCGXShaderVariant* pc_gx_tev_get_variant(void);
void   pc_gx_tev_init(void);
void   pc_gx_tev_shutdown(void);
void   pc_gx_cache_uniform_locations(GLuint shader, PCGXUloc* out);

/* Texture cache */
GLuint pc_gx_texture_upload(void* data, int width, int height, int format, int ci_format,
                            void* tlut, int tlut_format, int tlut_count);
void   pc_gx_texture_init(void);
void   pc_gx_texture_shutdown(void);
void   pc_gx_texture_cache_invalidate(void);

#ifdef PC_ENHANCEMENTS
/* EFB capture: store full-res GL texture from GXCopyTex, retrieve on texture load */
void   pc_gx_efb_capture_store(u32 dest_ptr, GLuint gl_tex);
GLuint pc_gx_efb_capture_find(u32 data_ptr);
void   pc_gx_efb_capture_cleanup(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PC_GX_INTERNAL_H */
