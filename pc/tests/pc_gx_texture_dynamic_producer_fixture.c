#include "pc_gx_internal.h"
#include "pc_gx_texture_raw_state.h"
#include "pc_settings.h"
#include "pc_texture_pack.h"

#include <dolphin/gx/GXEnum.h>
#include <dolphin/gx/GXVert.h>
#include <dolphin/gx/GXStruct.h>
#include <dolphin/gx/GXGeometry.h>
#include <dolphin/gx/GXTexture.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void pc_gx_tlut_set_native_le(unsigned int idx);
extern void GXBeginDisplayList(void* list, u32 size);
extern u32 GXEndDisplayList(void);
extern void GXCopyTex(void* dest, GXBool clear);
extern void GXDestroyTexObj(void* obj);
extern void GXDestroyTlutObj(void* obj);

int g_pc_window_w = PC_SCREEN_WIDTH;
int g_pc_window_h = PC_SCREEN_HEIGHT;
int g_pc_widescreen_stretch = 0;
PCSettings g_pc_settings = {
    .texture_filtering = 1
};

int pc_texture_pack_active(void) {
    return 0;
}

GLuint pc_texture_pack_lookup(
    const void* data,
    int data_size,
    int w,
    int h,
    unsigned int fmt,
    const void* tlut_data,
    int tlut_entries,
    int tlut_is_be,
    int* out_w,
    int* out_h
) {
    (void)data;
    (void)data_size;
    (void)w;
    (void)h;
    (void)fmt;
    (void)tlut_data;
    (void)tlut_entries;
    (void)tlut_is_be;
    (void)out_w;
    (void)out_h;
    return 0;
}

static PCGXShaderVariant g_fixture_shader_variant;

void pc_gx_tev_seq_reset(void) {
}

PCGXShaderVariant* pc_gx_tev_get_variant(void) {
    return &g_fixture_shader_variant;
}

static void fixture_gl_bind_vertex_array(GLuint array) {
    (void)array;
}

static void fixture_gl_bind_buffer(GLenum target, GLuint buffer) {
    (void)target;
    (void)buffer;
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct {
    int calls;
    int in_begin;
} FlushObservation;

typedef struct {
    int calls;
    uint32_t image_mask;
    uint32_t tlut_mask;
    const void* image_pointer;
    const void* tlut_pointer;
    uint32_t image_size;
    uint32_t tlut_size;
} SnapshotObservation;

typedef struct {
    int calls;
} ReplacementObservation;

typedef struct {
    int calls;
    int borrow_active;
    int nested_result;
    int mutation_state_unchanged;
    int forged_end_result;
    int forged_begin_result;
    int active_after_forged;
    int raw_state_unchanged;
    int lease_state_unchanged;
    int borrowed_bytes_unchanged;
    int g_gx_state_unchanged;
    int texture_source_unchanged;
    int object_state_unchanged;
    int display_list_unchanged;
    int callback_replacement_calls;
    void* image;
    void* tlut_bytes;
    GXTexObj* texture_object;
    GXTlutObj* tlut_object;
    ReplacementObservation replacement;
} BorrowObservation;

static void observe_geometry_flush(void* context) {
    FlushObservation* observation = (FlushObservation*)context;

    observation->calls++;
    observation->in_begin = g_gx.in_begin;
}

static void observe_snapshot(
    void* context,
    const AcgcGxCanonicalTextureState* texture,
    const AcgcGxCanonicalDynamicState* dynamic,
    const PCGXTextureDynamicLease* lease
) {
    SnapshotObservation* observation = (SnapshotObservation*)context;

    observation->calls++;
    observation->image_mask = dynamic->header.present_image_mask;
    observation->tlut_mask = dynamic->header.present_tlut_mask;
    observation->image_pointer = lease->images[0].bytes;
    observation->tlut_pointer = lease->tluts[15].bytes;
    observation->image_size = texture->records[0].image_byte_size;
    observation->tlut_size = dynamic->records[8 + 15].byte_size;
}

static void observe_replacement(
    void* context,
    const AcgcGxCanonicalTextureState* texture,
    const AcgcGxCanonicalDynamicState* dynamic,
    const PCGXTextureDynamicLease* lease
) {
    ReplacementObservation* observation = (ReplacementObservation*)context;

    (void)texture;
    (void)dynamic;
    (void)lease;
    observation->calls++;
}

static void observe_active_borrow(
    void* context,
    const AcgcGxCanonicalTextureState* texture,
    const AcgcGxCanonicalDynamicState* dynamic,
    const PCGXTextureDynamicLease* lease
) {
    BorrowObservation* observation = (BorrowObservation*)context;
    PCGXTextureRawState before;
    PCGXTextureRawState after;
    PCGXTextureBorrowedResource image_lease_before;
    PCGXTextureBorrowedResource tlut_lease_before;
    PCGXTextureBorrowedResource image_lease_after;
    PCGXTextureBorrowedResource tlut_lease_after;
    PCGXTextureSource source_before;
    GXTexObj texture_object_before;
    GXTlutObj tlut_object_before;
    unsigned char image_bytes_before[32];
    unsigned char tlut_bytes_before[32];
    unsigned char tlut_state_before[sizeof(g_gx.tlut[15])];
    GLuint gl_texture_before;
    int texture_width_before;
    int texture_height_before;
    int texture_format_before;
    unsigned int dirty_before;

    observation->calls++;
    if (observation->calls != 1) {
        return;
    }
    if (texture == NULL || dynamic == NULL || lease == NULL ||
        observation->texture_object == NULL ||
        observation->tlut_object == NULL || observation->image == NULL ||
        observation->tlut_bytes == NULL) {
        observation->mutation_state_unchanged = 0;
        return;
    }
    observation->borrow_active = pc_gx_texture_raw_borrow_is_active();
    pc_gx_texture_raw_snapshot(&before);
    if (pc_gx_texture_raw_get_image_lease(0, &image_lease_before) != 1 ||
        pc_gx_texture_raw_get_tlut_lease(15, &tlut_lease_before) != 1) {
        observation->mutation_state_unchanged = 0;
        return;
    }
    source_before = g_gx.texture_sources[0];
    texture_object_before = *observation->texture_object;
    tlut_object_before = *observation->tlut_object;
    memcpy(image_bytes_before, observation->image, sizeof(image_bytes_before));
    memcpy(tlut_bytes_before, observation->tlut_bytes, sizeof(tlut_bytes_before));
    memcpy(tlut_state_before, &g_gx.tlut[15], sizeof(tlut_state_before));
    gl_texture_before = g_gx.gl_textures[0];
    texture_width_before = g_gx.tex_obj_w[0];
    texture_height_before = g_gx.tex_obj_h[0];
    texture_format_before = g_gx.tex_obj_fmt[0];
    dirty_before = g_gx.dirty;

    {
        PCGXTextureRawBorrow forged = {0};

        observation->forged_end_result =
            pc_gx_texture_raw_end_borrow(&forged);
        observation->forged_begin_result =
            pc_gx_texture_raw_begin_borrow(&forged);
        observation->active_after_forged =
            pc_gx_texture_raw_borrow_is_active();
    }

    /* Every raw writer, including a nested publication attempt, must fail
     * closed while the callback owns the synchronous borrow. */
    pc_gx_texture_raw_mark_map_invalid(0);
    pc_gx_texture_raw_drop_image_lease(0);
    pc_gx_texture_raw_drop_all_image_leases();
    pc_gx_texture_raw_drop_all_tlut_leases();
    pc_gx_texture_raw_mark_global_invalid();
    observation->nested_result = pc_gx_try_texture_dynamic_snapshot();

    /* These are top-level resource mutations, so each must reject before it
     * can touch g_gx, the source sideband, the cache, or leased bytes. */
    GXLoadTexObj(observation->texture_object, 0);
    GXLoadTlut(observation->tlut_object, 15);
    GXInvalidateTexAll();
    GXInvalidateTexRegion(NULL);
    GXDestroyTexObj(observation->texture_object);
    GXDestroyTlutObj(observation->tlut_object);
    pc_gx_texture_cache_invalidate();
    pc_gx_tlut_set_native_le(15);
    pc_gx_texture_init();
    pc_gx_texture_shutdown();

    /* The display-list path is the first side effect in GXCopyTex.  Keep it
     * active so this also proves the guard precedes command recording. */
    GXCopyTex(observation->image, GX_FALSE);

    pc_gx_texture_raw_snapshot(&after);
    observation->mutation_state_unchanged =
        memcmp(&before, &after, sizeof(before)) == 0;
    observation->raw_state_unchanged = observation->mutation_state_unchanged;
    observation->lease_state_unchanged =
        pc_gx_texture_raw_get_image_lease(0, &image_lease_after) == 1 &&
        pc_gx_texture_raw_get_tlut_lease(15, &tlut_lease_after) == 1 &&
        memcmp(&image_lease_before, &image_lease_after,
               sizeof(image_lease_before)) == 0 &&
        memcmp(&tlut_lease_before, &tlut_lease_after,
               sizeof(tlut_lease_before)) == 0 &&
        lease->images[0].bytes == image_lease_after.bytes &&
        lease->tluts[15].bytes == tlut_lease_after.bytes;
    observation->borrowed_bytes_unchanged =
        memcmp(image_bytes_before, observation->image,
               sizeof(image_bytes_before)) == 0 &&
        memcmp(tlut_bytes_before, observation->tlut_bytes,
               sizeof(tlut_bytes_before)) == 0;
    observation->texture_source_unchanged =
        memcmp(&source_before, &g_gx.texture_sources[0],
               sizeof(source_before)) == 0;
    observation->g_gx_state_unchanged =
        gl_texture_before == g_gx.gl_textures[0] &&
        texture_width_before == g_gx.tex_obj_w[0] &&
        texture_height_before == g_gx.tex_obj_h[0] &&
        texture_format_before == g_gx.tex_obj_fmt[0] &&
        dirty_before == g_gx.dirty &&
        memcmp(tlut_state_before, &g_gx.tlut[15],
               sizeof(tlut_state_before)) == 0;
    observation->object_state_unchanged =
        memcmp(&texture_object_before, observation->texture_object,
               sizeof(texture_object_before)) == 0 &&
        memcmp(&tlut_object_before, observation->tlut_object,
               sizeof(tlut_object_before)) == 0;
    observation->callback_replacement_calls = observation->replacement.calls;

    pc_gx_clear_texture_dynamic_snapshot_callback();
    pc_gx_set_texture_dynamic_snapshot_callback(
        observe_replacement, &observation->replacement
    );
}

static void reset_state(void) {
    pc_gx_clear_geometry_flush_fixture_observer();
    pc_gx_clear_texture_dynamic_snapshot_callback();
    memset(&g_gx, 0, sizeof(g_gx));
    memset(&g_fixture_shader_variant, 0, sizeof(g_fixture_shader_variant));
    glad_glBindVertexArray = fixture_gl_bind_vertex_array;
    glad_glBindBuffer = fixture_gl_bind_buffer;
    pc_gx_texture_init();
}

static void make_legacy_source(
    PCGXTextureSource* source,
    const void* image,
    uint32_t format,
    uint32_t byte_size
) {
    memset(source, 0, sizeof(*source));
    source->image_ptr = image;
    source->image_byte_size = byte_size;
    source->width = 8;
    source->height = 8;
    source->format = format;
    source->wrap_s = GX_CLAMP;
    source->wrap_t = GX_CLAMP;
    source->min_filter = GX_NEAR;
    source->mag_filter = GX_NEAR;
    source->effective_filter = GX_NEAR;
    source->tlut_name = UINT32_MAX;
    source->source_kind = PCGX_TEXTURE_SOURCE_RAW_GUEST;
    source->tlut_source_kind = PCGX_TEXTURE_SOURCE_NONE;
}

static void make_image_object(
    GXTexObj* object,
    void* image,
    uint16_t width,
    uint16_t height,
    uint32_t format,
    uint8_t mipmap
) {
    GXInitTexObj(
        object, image, width, height, format, GX_CLAMP, GX_CLAMP, mipmap
    );
    if (mipmap != 0) {
        GXInitTexObjLOD(
            object, GX_NEAR, GX_NEAR, 0.0f, 1.0f, 0.0f,
            GX_FALSE, GX_FALSE, GX_ANISO_1
        );
    }
}

static int build_snapshot_with_token(
    AcgcGxCanonicalTextureState* texture,
    AcgcGxCanonicalDynamicState* dynamic,
    PCGXTextureDynamicLease* lease
) {
    PCGXTextureRawBorrow borrow = {0};
    PCGXTextureRawState raw_capture;
    int success;

    if (!pc_gx_texture_raw_begin_borrow(&borrow)) {
        return 0;
    }
    success = pc_gx_build_texture_dynamic_snapshot_borrowed(
        &borrow, texture, dynamic, &raw_capture, lease
    );
    if (success) {
        success = pc_gx_texture_raw_revalidate_borrow(
            &borrow, &raw_capture, lease
        );
    }
    if (!pc_gx_texture_raw_end_borrow(&borrow)) {
        success = 0;
    }
    return success;
}

static int build_valid_snapshot(
    AcgcGxCanonicalTextureState* texture,
    AcgcGxCanonicalDynamicState* dynamic,
    PCGXTextureDynamicLease* lease
) {
    CHECK(build_snapshot_with_token(texture, dynamic, lease) == 1);
    CHECK(acgc_gx_canonical_texture_state_validate(texture) == 1);
    CHECK(acgc_gx_canonical_dynamic_state_validate(dynamic) == 1);
    CHECK(acgc_gx_canonical_texture_dynamic_validate(texture, dynamic) == 1);
    return 0;
}

static int test_empty_and_all_or_nothing(void) {
    AcgcGxCanonicalTextureState texture;
    AcgcGxCanonicalDynamicState dynamic;
    PCGXTextureDynamicLease lease;
    AcgcGxCanonicalTextureState old_texture;
    AcgcGxCanonicalDynamicState old_dynamic;
    PCGXTextureDynamicLease old_lease;

    reset_state();
    memset(&texture, 0xA5, sizeof(texture));
    memset(&dynamic, 0xA5, sizeof(dynamic));
    memset(&lease, 0xA5, sizeof(lease));
    CHECK(build_valid_snapshot(&texture, &dynamic, &lease) == 0);
    CHECK(texture.header.known_map_mask == 0);
    CHECK(dynamic.header.owner_epoch != 0);
    CHECK(dynamic.header.present_image_mask == 0);
    CHECK(dynamic.header.present_tlut_mask == 0);

    reset_state();
    memset(&old_texture, 0x5A, sizeof(old_texture));
    memset(&old_dynamic, 0x5A, sizeof(old_dynamic));
    memset(&old_lease, 0x5A, sizeof(old_lease));
    texture = old_texture;
    dynamic = old_dynamic;
    lease = old_lease;
    {
        GXTexObj bad_object;
        make_image_object(&bad_object, NULL, 8, 8, GX_TF_I4, GX_FALSE);
        bad_object.dummy[4] = 99;
        pc_gx_texture_raw_load_map(0, bad_object.dummy,
                                    PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST, 1);
    }
    CHECK(build_snapshot_with_token(&texture, &dynamic, &lease) == 0);
    CHECK(memcmp(&texture, &old_texture, sizeof(texture)) == 0);
    CHECK(memcmp(&dynamic, &old_dynamic, sizeof(dynamic)) == 0);
    CHECK(memcmp(&lease, &old_lease, sizeof(lease)) == 0);
    return 0;
}

static int test_tiled_mip_converted_and_generation(void) {
    _Alignas(32) static uint8_t image0[32];
    _Alignas(32) static uint8_t image1[320];
    _Alignas(32) static uint8_t image2[32];
    _Alignas(32) static uint8_t tlut15_bytes[32];
    _Alignas(32) static uint8_t tlut14_bytes[32];
    GXTexObj object0;
    GXTexObj object1;
    GXTexObj object2;
    GXTexObj object3;
    GXTexObj object5;
    GXTlutObj tlut_object;
    PCGXTextureSource legacy_source;
    PCGXTextureRawState raw;
    AcgcGxCanonicalTextureState texture;
    AcgcGxCanonicalDynamicState dynamic;
    PCGXTextureDynamicLease lease;
    uint64_t first_generation;
    uint64_t first_tlut_generation;
    uint64_t second_tlut_generation;
    uint64_t map0_generation_before_tlut;
    uint64_t map1_generation_before_tlut;
    uint64_t map2_generation_before_tlut;
    uint64_t map3_generation_before_tlut;
    uint64_t map5_generation_before_tlut;
    PCGXTextureBorrowedResource map0_lease_before_tlut;
    PCGXTextureBorrowedResource map1_lease_before_tlut;
    PCGXTextureBorrowedResource map2_lease_before_tlut;
    PCGXTextureBorrowedResource map5_lease_before_tlut;

    memset(image0, 0x11, sizeof(image0));
    memset(image1, 0x22, sizeof(image1));
    memset(image2, 0x33, sizeof(image2));
    memset(tlut15_bytes, 0x44, sizeof(tlut15_bytes));
    memset(tlut14_bytes, 0x55, sizeof(tlut14_bytes));
    reset_state();

    make_image_object(&object0, image0, 8, 8, GX_TF_I4, GX_FALSE);
    pc_gx_texture_raw_load_map(
        0, object0.dummy, PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST, 1
    );
    make_legacy_source(&legacy_source, image0, GX_TF_I4, sizeof(image0));
    CHECK(pc_gx_texture_source_fixture_store(0, &legacy_source) == 1);
    pc_gx_texture_raw_publish_image_lease(0, image0);

    make_image_object(&object1, image1, 8, 8, GX_TF_RGBA8, GX_TRUE);
    pc_gx_texture_raw_load_map(
        1, object1.dummy, PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST, 1
    );
    pc_gx_texture_raw_publish_image_lease(1, image1);

    pc_gx_texture_mark_image_converted(2);
    CHECK(pc_gx_texture_consume_image_source_kind(2) ==
          PC_GX_TEXTURE_RAW_SOURCE_EMU64_CONVERTED);
    CHECK(pc_gx_texture_consume_image_source_kind(2) ==
          PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST);
    make_image_object(&object2, image2, 8, 8, GX_TF_I8, GX_FALSE);
    pc_gx_texture_raw_load_map(
        2, object2.dummy, PC_GX_TEXTURE_RAW_SOURCE_EMU64_CONVERTED, 1
    );
    pc_gx_texture_raw_publish_image_lease(2, image2);

    GXInitTlutObj(&tlut_object, tlut15_bytes, GX_TL_RGB5A3, 16);
    GXLoadTlut(&tlut_object, 15);

    GXInitTexObjCI(
        &object3, image0, 8, 8, GX_TF_C4, GX_CLAMP, GX_CLAMP, GX_FALSE, 15
    );
    pc_gx_texture_raw_load_map(
        3, object3.dummy, PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST, 1
    );
    pc_gx_texture_raw_publish_image_lease(3, image0);

    pc_gx_texture_raw_load_tlut(
        14, GX_TL_RGB565, 16,
        PC_GX_TEXTURE_RAW_BYTE_ORDER_GX_BE,
        PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST
    );
    pc_gx_texture_raw_publish_tlut_lease(14, tlut14_bytes);
    GXInitTexObjCI(
        &object5, image0, 4, 4, GX_TF_C14X2, GX_CLAMP, GX_CLAMP, GX_FALSE, 14
    );
    pc_gx_texture_raw_load_map(
        5, object5.dummy, PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST, 1
    );
    pc_gx_texture_raw_publish_image_lease(5, image0);

    CHECK(build_valid_snapshot(&texture, &dynamic, &lease) == 0);
    CHECK(texture.records[1].image_byte_size == 320);
    CHECK(texture.records[1].mip_level_count == 2);
    CHECK(texture.records[2].image_source_kind ==
          PC_GX_TEXTURE_RAW_SOURCE_EMU64_CONVERTED);
    CHECK(texture.records[2].image_byte_order ==
          PC_GX_TEXTURE_RAW_BYTE_ORDER_LE);
    CHECK(texture.records[3].tlut_name == 15);
    CHECK(texture.records[5].tlut_name == 14);
    CHECK(dynamic.records[8 + 15].resource_id == 0x10Fu);
    CHECK(dynamic.records[8 + 14].resource_id == 0x10Eu);
    CHECK(dynamic.header.required_tlut_mask ==
          ((UINT32_C(1) << 15) | (UINT32_C(1) << 14)));

    pc_gx_texture_raw_snapshot(&raw);
    first_generation = raw.images[0].generation;
    first_tlut_generation = raw.tluts[15].generation;
    pc_gx_texture_raw_load_map(
        0, object0.dummy, PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST, 1
    );
    pc_gx_texture_raw_publish_image_lease(0, image0);
    pc_gx_texture_raw_snapshot(&raw);
    CHECK(raw.images[0].generation > first_generation);

    map0_generation_before_tlut = raw.images[0].generation;
    map1_generation_before_tlut = raw.images[1].generation;
    map2_generation_before_tlut = raw.images[2].generation;
    map3_generation_before_tlut = raw.images[3].generation;
    map5_generation_before_tlut = raw.images[5].generation;
    CHECK(pc_gx_texture_raw_get_image_lease(0, &map0_lease_before_tlut) == 1);
    CHECK(pc_gx_texture_raw_get_image_lease(1, &map1_lease_before_tlut) == 1);
    CHECK(pc_gx_texture_raw_get_image_lease(2, &map2_lease_before_tlut) == 1);
    CHECK(pc_gx_texture_raw_get_image_lease(5, &map5_lease_before_tlut) == 1);

    GXLoadTlut(&tlut_object, 15);
    pc_gx_texture_raw_snapshot(&raw);
    second_tlut_generation = raw.tluts[15].generation;
    CHECK(second_tlut_generation > first_tlut_generation);
    CHECK(raw.images[0].generation == map0_generation_before_tlut);
    CHECK(raw.images[1].generation == map1_generation_before_tlut);
    CHECK(raw.images[2].generation == map2_generation_before_tlut);
    CHECK(raw.images[5].generation == map5_generation_before_tlut);
    CHECK(raw.images[3].generation > map3_generation_before_tlut);
    CHECK((raw.available_map_mask & (UINT8_C(1) << 0)) != 0);
    CHECK((raw.available_map_mask & (UINT8_C(1) << 1)) != 0);
    CHECK((raw.available_map_mask & (UINT8_C(1) << 2)) != 0);
    CHECK((raw.available_map_mask & (UINT8_C(1) << 3)) == 0);
    CHECK((raw.available_map_mask & (UINT8_C(1) << 5)) != 0);
    {
        PCGXTextureBorrowedResource lease_after_tlut;
        CHECK(pc_gx_texture_raw_get_image_lease(0, &lease_after_tlut) == 1);
        CHECK(lease_after_tlut.bytes == map0_lease_before_tlut.bytes);
        CHECK(lease_after_tlut.generation == map0_lease_before_tlut.generation);
        CHECK(pc_gx_texture_raw_get_image_lease(1, &lease_after_tlut) == 1);
        CHECK(lease_after_tlut.bytes == map1_lease_before_tlut.bytes);
        CHECK(lease_after_tlut.generation == map1_lease_before_tlut.generation);
        CHECK(pc_gx_texture_raw_get_image_lease(2, &lease_after_tlut) == 1);
        CHECK(lease_after_tlut.bytes == map2_lease_before_tlut.bytes);
        CHECK(lease_after_tlut.generation == map2_lease_before_tlut.generation);
        CHECK(pc_gx_texture_raw_get_image_lease(5, &lease_after_tlut) == 1);
        CHECK(lease_after_tlut.bytes == map5_lease_before_tlut.bytes);
        CHECK(lease_after_tlut.generation == map5_lease_before_tlut.generation);
    }
    CHECK(build_snapshot_with_token(&texture, &dynamic, &lease) == 0);
    /* Only the map dependent on TLUT 15 needs a new image lease. */
    pc_gx_texture_raw_publish_image_lease(3, image0);
    CHECK(build_valid_snapshot(&texture, &dynamic, &lease) == 0);
    return 0;
}

static int test_tlut_native_le_and_lease_drop(void) {
    _Alignas(32) static uint8_t image[32];
    _Alignas(32) static uint8_t tlut_bytes[32];
    GXTexObj object;
    GXTexObj ci_object;
    GXTlutObj tlut_object;
    AcgcGxCanonicalTextureState texture;
    AcgcGxCanonicalDynamicState dynamic;
    PCGXTextureDynamicLease lease;
    PCGXTextureRawState raw;
    uint64_t first_generation;
    uint64_t map0_generation_before_tlut;
    uint64_t map0_generation_before_native_le;
    uint64_t map1_generation_before_native_le;
    PCGXTextureBorrowedResource map0_lease_before_tlut;

    memset(image, 0x66, sizeof(image));
    memset(tlut_bytes, 0x77, sizeof(tlut_bytes));
    reset_state();
    make_image_object(&object, image, 8, 8, GX_TF_I4, GX_FALSE);
    pc_gx_texture_raw_load_map(
        0, object.dummy, PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST, 1
    );
    pc_gx_texture_raw_publish_image_lease(0, image);
    GXInitTlutObj(&tlut_object, tlut_bytes, GX_TL_RGB5A3, 16);
    GXLoadTlut(&tlut_object, 15);
    pc_gx_texture_raw_snapshot(&raw);
    map0_generation_before_tlut = raw.images[0].generation;
    CHECK(pc_gx_texture_raw_get_image_lease(0, &map0_lease_before_tlut) == 1);
    GXInitTexObjCI(
        &ci_object, image, 8, 8, GX_TF_C4, GX_CLAMP, GX_CLAMP, GX_FALSE, 15
    );
    pc_gx_texture_raw_load_map(
        1, ci_object.dummy, PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST, 1
    );
    pc_gx_texture_raw_publish_image_lease(1, image);
    CHECK(build_valid_snapshot(&texture, &dynamic, &lease) == 0);

    pc_gx_texture_raw_snapshot(&raw);
    first_generation = raw.tluts[15].generation;
    map0_generation_before_native_le = raw.images[0].generation;
    map1_generation_before_native_le = raw.images[1].generation;
    pc_gx_tlut_set_native_le(15);
    pc_gx_texture_raw_snapshot(&raw);
    CHECK(raw.tluts[15].generation > first_generation);
    CHECK(raw.images[0].generation == map0_generation_before_native_le);
    CHECK(raw.images[0].generation == map0_generation_before_tlut);
    CHECK(raw.images[1].generation > map1_generation_before_native_le);
    CHECK((raw.available_map_mask & (UINT8_C(1) << 0)) != 0);
    CHECK((raw.available_map_mask & (UINT8_C(1) << 1)) == 0);
    {
        PCGXTextureBorrowedResource map0_lease_after_native_le;
        CHECK(pc_gx_texture_raw_get_image_lease(
            0, &map0_lease_after_native_le
        ) == 1);
        CHECK(map0_lease_after_native_le.bytes == map0_lease_before_tlut.bytes);
        CHECK(map0_lease_after_native_le.generation ==
              map0_lease_before_tlut.generation);
    }
    CHECK(raw.tluts[15].byte_order == PC_GX_TEXTURE_RAW_BYTE_ORDER_LE);
    CHECK(raw.tluts[15].source_kind ==
          PC_GX_TEXTURE_RAW_SOURCE_EMU64_CONVERTED);
    CHECK(build_snapshot_with_token(&texture, &dynamic, &lease) == 0);
    /* Only indexed map 1 depends on the changed TLUT. */
    pc_gx_texture_raw_publish_image_lease(1, image);
    CHECK(build_valid_snapshot(&texture, &dynamic, &lease) == 0);

    pc_gx_texture_raw_drop_image_lease(0);
    CHECK(build_snapshot_with_token(&texture, &dynamic, &lease) == 0);
    /* Map 0 is not indexed, so its required image lease is the only missing
     * resource in the preceding call; republishing restores the snapshot. */
    pc_gx_texture_raw_publish_image_lease(0, image);
    CHECK(build_valid_snapshot(&texture, &dynamic, &lease) == 0);

    GXInvalidateTexAll();
    CHECK(build_snapshot_with_token(&texture, &dynamic, &lease) == 0);
    pc_gx_texture_raw_publish_image_lease(0, image);
    pc_gx_texture_raw_publish_image_lease(1, image);
    CHECK(build_valid_snapshot(&texture, &dynamic, &lease) == 0);
    return 0;
}

static int test_format_sizes_and_mip_boundaries(void) {
    static const struct {
        uint32_t format;
        uint32_t byte_size;
        uint32_t indexed;
    } cases[] = {
        {GX_TF_I4, 32, 0},
        {GX_TF_I8, 64, 0},
        {GX_TF_IA4, 64, 0},
        {GX_TF_IA8, 128, 0},
        {GX_TF_RGB565, 128, 0},
        {GX_TF_RGB5A3, 128, 0},
        {GX_TF_RGBA8, 256, 0},
        {GX_TF_CMPR, 32, 0},
        {GX_TF_C4, 32, 1},
        {GX_TF_C8, 64, 1},
        {GX_TF_C14X2, 128, 1}
    };
    _Alignas(32) static uint8_t bytes[1024];
    GXTexObj object;
    GXTlutObj tlut_object;
    PCGXTextureRawState raw;
    AcgcGxCanonicalTextureState texture;
    AcgcGxCanonicalDynamicState dynamic;
    PCGXTextureDynamicLease lease;
    size_t index;

    memset(bytes, 0xBC, sizeof(bytes));
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        reset_state();
        if (cases[index].indexed != 0) {
            GXInitTlutObj(&tlut_object, bytes, GX_TL_RGB5A3, 16);
            pc_gx_texture_raw_load_tlut(
                0, GX_TL_RGB5A3, 16,
                PC_GX_TEXTURE_RAW_BYTE_ORDER_GX_BE,
                PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST
            );
            pc_gx_texture_raw_publish_tlut_lease(0, bytes);
            GXInitTexObjCI(
                &object, bytes, 8, 8, cases[index].format,
                GX_CLAMP, GX_CLAMP, GX_FALSE, 0
            );
        } else {
            GXInitTexObj(
                &object, bytes, 8, 8, cases[index].format,
                GX_CLAMP, GX_CLAMP, GX_FALSE
            );
        }
        pc_gx_texture_raw_load_map(
            0, object.dummy, PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST, 1
        );
        pc_gx_texture_raw_publish_image_lease(0, bytes);
        pc_gx_texture_raw_snapshot(&raw);
        CHECK(raw.images[0].byte_size == cases[index].byte_size);
        CHECK(raw.images[0].mip_level_count == 1);
        CHECK(build_valid_snapshot(&texture, &dynamic, &lease) == 0);
    }

    reset_state();
    GXInitTexObj(
        &object, bytes, 1, 1, GX_TF_RGBA8,
        GX_CLAMP, GX_CLAMP, GX_TRUE
    );
    GXInitTexObjLOD(
        &object, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f,
        GX_FALSE, GX_FALSE, GX_ANISO_1
    );
    pc_gx_texture_raw_load_map(
        0, object.dummy, PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST, 1
    );
    pc_gx_texture_raw_publish_image_lease(0, bytes);
    pc_gx_texture_raw_snapshot(&raw);
    CHECK(raw.images[0].byte_size == 64);
    CHECK(raw.images[0].mip_level_count == 1);

    reset_state();
    GXInitTexObj(
        &object, bytes, 1024, 1024, GX_TF_I4,
        GX_CLAMP, GX_CLAMP, GX_TRUE
    );
    GXInitTexObjLOD(
        &object, GX_NEAR, GX_NEAR, 0.0f, 10.0f, 0.0f,
        GX_FALSE, GX_FALSE, GX_ANISO_1
    );
    pc_gx_texture_raw_load_map(
        0, object.dummy, PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST, 1
    );
    pc_gx_texture_raw_publish_image_lease(0, bytes);
    pc_gx_texture_raw_snapshot(&raw);
    CHECK(raw.images[0].mip_level_count == 11);
    CHECK(raw.images[0].byte_size > 32);
    return 0;
}

static int test_invalidity_and_callback(void) {
    _Alignas(32) static uint8_t image[32];
    GXTexObj object;
    AcgcGxCanonicalTextureState texture;
    AcgcGxCanonicalDynamicState dynamic;
    PCGXTextureDynamicLease lease;
    SnapshotObservation observation;

    memset(image, 0x88, sizeof(image));
    reset_state();
    pc_gx_texture_mark_image_converted(2);
    CHECK(pc_gx_texture_consume_image_source_kind(1) ==
          PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST);
    CHECK(pc_gx_texture_consume_image_source_kind(2) ==
          PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST);
    pc_gx_texture_mark_image_converted(2);
    CHECK(pc_gx_texture_consume_image_source_kind(2) ==
          PC_GX_TEXTURE_RAW_SOURCE_EMU64_CONVERTED);
    CHECK(pc_gx_texture_consume_image_source_kind(2) ==
          PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST);
    make_image_object(&object, image, 8, 8, GX_TF_I4, GX_FALSE);
    object.dummy[10] = UINT32_C(0x7FC00000);
    pc_gx_texture_raw_load_map(
        0, object.dummy, PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST, 1
    );
    CHECK(build_snapshot_with_token(&texture, &dynamic, &lease) == 0);

    reset_state();
    pc_gx_texture_raw_load_tlut(
        13, GX_TL_RGB565, 0x4001,
        PC_GX_TEXTURE_RAW_BYTE_ORDER_GX_BE,
        PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST
    );
    CHECK(build_snapshot_with_token(&texture, &dynamic, &lease) == 0);

    reset_state();
    make_image_object(&object, image, 8, 8, GX_TF_I4, GX_FALSE);
    pc_gx_texture_raw_load_map(
        0, object.dummy, PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST, 1
    );
    pc_gx_texture_raw_publish_image_lease(0, image);
    memset(&observation, 0, sizeof(observation));
    pc_gx_set_texture_dynamic_snapshot_callback(
        observe_snapshot, &observation
    );
    CHECK(pc_gx_try_texture_dynamic_snapshot() == 1);
    CHECK(observation.calls == 1);
    CHECK(observation.image_mask == 1);
    CHECK(observation.tlut_mask == 0);
    CHECK(observation.image_pointer == image);
    CHECK(observation.image_size == 32);
    CHECK(observation.tlut_pointer == NULL);
    pc_gx_clear_texture_dynamic_snapshot_callback();
    return 0;
}

static int test_borrowed_builder_lifetime(void) {
    _Alignas(32) static uint8_t image[32];
    GXTexObj object;
    AcgcGxCanonicalTextureState texture;
    AcgcGxCanonicalDynamicState dynamic;
    PCGXTextureRawState raw_capture;
    PCGXTextureDynamicLease lease;
    AcgcGxCanonicalTextureState old_texture;
    AcgcGxCanonicalDynamicState old_dynamic;
    PCGXTextureRawState old_raw_capture;
    PCGXTextureDynamicLease old_lease;
    PCGXTextureRawBorrow inactive = {0};
    PCGXTextureRawBorrow active = {0};
    PCGXTextureRawBorrow forged = {0};
    PCGXTextureRawBorrow mismatched = {0};
    PCGXTextureRawBorrow ended = {0};
    uint8_t consumed;

    memset(image, 0xC3, sizeof(image));
    reset_state();
    make_image_object(&object, image, 8, 8, GX_TF_I4, GX_FALSE);
    pc_gx_texture_raw_load_map(
        0, object.dummy, PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST, 1
    );
    pc_gx_texture_raw_publish_image_lease(0, image);

    CHECK(pc_gx_texture_raw_begin_borrow(&ended) == 1);
    CHECK(pc_gx_texture_raw_end_borrow(&ended) == 1);
    CHECK(pc_gx_texture_raw_begin_borrow(&ended) == 0);

    memset(&texture, 0x5D, sizeof(texture));
    memset(&dynamic, 0x5D, sizeof(dynamic));
    memset(&raw_capture, 0x5D, sizeof(raw_capture));
    memset(&lease, 0x5D, sizeof(lease));
    old_texture = texture;
    old_dynamic = dynamic;
    old_raw_capture = raw_capture;
    old_lease = lease;

    /* The public builder never acquires an implicit borrow. */
    CHECK(pc_gx_build_texture_dynamic_snapshot_borrowed(
        &inactive, &texture, &dynamic, &raw_capture, &lease
    ) == 0);
    CHECK(memcmp(&texture, &old_texture, sizeof(texture)) == 0);
    CHECK(memcmp(&dynamic, &old_dynamic, sizeof(dynamic)) == 0);
    CHECK(memcmp(&raw_capture, &old_raw_capture, sizeof(raw_capture)) == 0);
    CHECK(memcmp(&lease, &old_lease, sizeof(lease)) == 0);

    CHECK(pc_gx_texture_raw_begin_borrow(&active) == 1);
    forged = active;
    mismatched = active;
    mismatched.owner = &mismatched;
    mismatched.serial++;
    CHECK(pc_gx_build_texture_dynamic_snapshot_borrowed(
        &forged, &texture, &dynamic, &raw_capture, &lease
    ) == 0);
    CHECK(memcmp(&texture, &old_texture, sizeof(texture)) == 0);
    CHECK(memcmp(&dynamic, &old_dynamic, sizeof(dynamic)) == 0);
    CHECK(memcmp(&raw_capture, &old_raw_capture, sizeof(raw_capture)) == 0);
    CHECK(memcmp(&lease, &old_lease, sizeof(lease)) == 0);
    CHECK(pc_gx_build_texture_dynamic_snapshot_borrowed(
        &mismatched, &texture, &dynamic, &raw_capture, &lease
    ) == 0);
    CHECK(memcmp(&texture, &old_texture, sizeof(texture)) == 0);
    CHECK(memcmp(&dynamic, &old_dynamic, sizeof(dynamic)) == 0);
    CHECK(memcmp(&raw_capture, &old_raw_capture, sizeof(raw_capture)) == 0);
    CHECK(memcmp(&lease, &old_lease, sizeof(lease)) == 0);
    CHECK(pc_gx_build_texture_dynamic_snapshot_borrowed(
        &ended, &texture, &dynamic, &raw_capture, &lease
    ) == 0);
    CHECK(memcmp(&texture, &old_texture, sizeof(texture)) == 0);
    CHECK(memcmp(&dynamic, &old_dynamic, sizeof(dynamic)) == 0);
    CHECK(memcmp(&raw_capture, &old_raw_capture, sizeof(raw_capture)) == 0);
    CHECK(memcmp(&lease, &old_lease, sizeof(lease)) == 0);
    CHECK(pc_gx_texture_raw_borrow_is_active() == 1);

    /* The caller owns the active token and can consume leased bytes before
     * revalidation and end; the builder itself leaves the borrow active. */
    CHECK(pc_gx_build_texture_dynamic_snapshot_borrowed(
        &active, &texture, &dynamic, &raw_capture, &lease
    ) == 1);
    CHECK(pc_gx_texture_raw_borrow_is_active() == 1);
    CHECK(raw_capture.owner_epoch != 0);
    CHECK(lease.image_mask == 1);
    CHECK(lease.images[0].bytes == image);
    CHECK(lease.images[0].byte_size == sizeof(image));
    consumed = *((const uint8_t*)lease.images[0].bytes);
    CHECK(consumed == 0xC3);
    CHECK(pc_gx_texture_raw_revalidate_borrow(
        &active, &raw_capture, &lease
    ) == 1);
    CHECK(pc_gx_texture_raw_end_borrow(&active) == 1);
    CHECK(pc_gx_texture_raw_borrow_is_active() == 0);

    /* The ended token cannot revalidate, end, begin, or publish another
     * borrowed result.  The lease is not dereferenced after end. */
    CHECK(pc_gx_texture_raw_revalidate_borrow(
        &active, &raw_capture, &lease
    ) == 0);
    CHECK(pc_gx_texture_raw_end_borrow(&active) == 0);
    CHECK(pc_gx_texture_raw_begin_borrow(&active) == 0);
    memset(&texture, 0x6E, sizeof(texture));
    memset(&dynamic, 0x6E, sizeof(dynamic));
    memset(&raw_capture, 0x6E, sizeof(raw_capture));
    memset(&lease, 0x6E, sizeof(lease));
    old_texture = texture;
    old_dynamic = dynamic;
    old_raw_capture = raw_capture;
    old_lease = lease;
    CHECK(pc_gx_build_texture_dynamic_snapshot_borrowed(
        &active, &texture, &dynamic, &raw_capture, &lease
    ) == 0);
    CHECK(memcmp(&texture, &old_texture, sizeof(texture)) == 0);
    CHECK(memcmp(&dynamic, &old_dynamic, sizeof(dynamic)) == 0);
    CHECK(memcmp(&raw_capture, &old_raw_capture, sizeof(raw_capture)) == 0);
    CHECK(memcmp(&lease, &old_lease, sizeof(lease)) == 0);
    CHECK(pc_gx_build_texture_dynamic_snapshot_borrowed(
        &forged, &texture, &dynamic, &raw_capture, &lease
    ) == 0);
    CHECK(memcmp(&texture, &old_texture, sizeof(texture)) == 0);
    CHECK(memcmp(&dynamic, &old_dynamic, sizeof(dynamic)) == 0);
    CHECK(memcmp(&raw_capture, &old_raw_capture, sizeof(raw_capture)) == 0);
    CHECK(memcmp(&lease, &old_lease, sizeof(lease)) == 0);
    return 0;
}

static int test_synchronous_borrow_transaction(void) {
    _Alignas(32) static uint8_t image[32];
    _Alignas(32) static uint8_t tlut_bytes[32];
    _Alignas(32) static uint8_t display_list[32];
    GXTexObj object;
    GXTlutObj tlut_object;
    PCGXTextureRawState raw_before;
    PCGXTextureRawState raw_after;
    BorrowObservation observation;
    AcgcGxCanonicalTextureState texture;
    AcgcGxCanonicalDynamicState dynamic;
    PCGXTextureDynamicLease lease;
    AcgcGxCanonicalTextureState old_texture;
    AcgcGxCanonicalDynamicState old_dynamic;
    PCGXTextureDynamicLease old_lease;
    AcgcGxCanonicalTextureState token_texture;
    AcgcGxCanonicalDynamicState token_dynamic;
    PCGXTextureDynamicLease token_lease;
    PCGXTextureRawState token_raw;
    PCGXTextureRawBorrow token = {0};
    PCGXTextureRawBorrow forged = {0};
    PCGXTextureRawBorrow nested = {0};

    memset(image, 0x91, sizeof(image));
    memset(tlut_bytes, 0xA1, sizeof(tlut_bytes));
    memset(display_list, 0xA7, sizeof(display_list));
    reset_state();
    GXInitTlutObj(&tlut_object, tlut_bytes, GX_TL_RGB5A3, 16);
    GXLoadTlut(&tlut_object, 15);
    make_image_object(&object, image, 8, 8, GX_TF_I4, GX_FALSE);
    pc_gx_texture_raw_load_map(
        0, object.dummy, PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST, 1
    );
    pc_gx_texture_raw_publish_image_lease(0, image);
    pc_gx_texture_raw_snapshot(&raw_before);
    memset(&observation, 0, sizeof(observation));
    observation.image = image;
    observation.tlut_bytes = tlut_bytes;
    observation.texture_object = &object;
    observation.tlut_object = &tlut_object;
    pc_gx_set_texture_dynamic_snapshot_callback(
        observe_active_borrow, &observation
    );
    GXBeginDisplayList(display_list, sizeof(display_list));
    CHECK(pc_gx_try_texture_dynamic_snapshot() == 1);
    CHECK(GXEndDisplayList() == 0);
    CHECK(observation.calls == 1);
    CHECK(observation.borrow_active == 1);
    CHECK(observation.forged_end_result == 0);
    CHECK(observation.forged_begin_result == 0);
    CHECK(observation.active_after_forged == 1);
    CHECK(observation.nested_result == 0);
    CHECK(observation.mutation_state_unchanged == 1);
    CHECK(observation.raw_state_unchanged == 1);
    CHECK(observation.lease_state_unchanged == 1);
    CHECK(observation.borrowed_bytes_unchanged == 1);
    CHECK(observation.g_gx_state_unchanged == 1);
    CHECK(observation.texture_source_unchanged == 1);
    CHECK(observation.object_state_unchanged == 1);
    CHECK(observation.callback_replacement_calls == 0);
    {
        uint8_t display_list_before[sizeof(display_list)];

        memset(display_list_before, 0xA7, sizeof(display_list_before));
        observation.display_list_unchanged =
            memcmp(display_list, display_list_before,
                   sizeof(display_list)) == 0;
    }
    CHECK(observation.display_list_unchanged == 1);
    pc_gx_texture_raw_snapshot(&raw_after);
    CHECK(memcmp(&raw_before, &raw_after, sizeof(raw_before)) == 0);
    CHECK(pc_gx_try_texture_dynamic_snapshot() == 1);
    CHECK(observation.calls == 2);
    CHECK(observation.replacement.calls == 0);
    CHECK(build_valid_snapshot(&texture, &dynamic, &lease) == 0);

    /* A callback may change registration only after the outer borrow ends. */
    pc_gx_clear_texture_dynamic_snapshot_callback();
    pc_gx_set_texture_dynamic_snapshot_callback(
        observe_replacement, &observation.replacement
    );
    CHECK(pc_gx_try_texture_dynamic_snapshot() == 1);
    CHECK(observation.calls == 2);
    CHECK(observation.replacement.calls == 1);
    pc_gx_clear_texture_dynamic_snapshot_callback();

    /* Exact-address token ownership rejects forged, nested, and ended
     * lifecycle calls without ever exposing the real token to the callback.
     */
    CHECK(build_valid_snapshot(&token_texture, &token_dynamic, &token_lease) == 0);
    pc_gx_texture_raw_snapshot(&token_raw);
    CHECK(pc_gx_texture_raw_begin_borrow(NULL) == 0);
    CHECK(pc_gx_texture_raw_end_borrow(NULL) == 0);
    CHECK(pc_gx_texture_raw_revalidate_borrow(
        NULL, &token_raw, &token_lease
    ) == 0);
    CHECK(pc_gx_texture_raw_begin_borrow(&token) == 1);
    CHECK(pc_gx_texture_raw_revalidate_borrow(
        &token, &token_raw, &token_lease
    ) == 1);
    forged = token;
    CHECK(pc_gx_texture_raw_revalidate_borrow(
        &forged, &token_raw, &token_lease
    ) == 0);
    CHECK(pc_gx_texture_raw_end_borrow(&forged) == 0);
    CHECK(pc_gx_texture_raw_borrow_is_active() == 1);
    CHECK(pc_gx_texture_raw_begin_borrow(&nested) == 0);
    CHECK(pc_gx_texture_raw_end_borrow(&nested) == 0);
    CHECK(pc_gx_texture_raw_end_borrow(&token) == 1);
    CHECK(pc_gx_texture_raw_end_borrow(&token) == 0);
    CHECK(pc_gx_texture_raw_borrow_is_active() == 0);
    CHECK(pc_gx_texture_raw_begin_borrow(&token) == 0);

    /* A reentrant builder cannot partially write caller outputs. */
    reset_state();
    memset(&old_texture, 0x5C, sizeof(old_texture));
    memset(&old_dynamic, 0x5C, sizeof(old_dynamic));
    memset(&old_lease, 0x5C, sizeof(old_lease));
    texture = old_texture;
    dynamic = old_dynamic;
    lease = old_lease;
    CHECK(pc_gx_texture_raw_begin_borrow(&token) == 0);
    {
        PCGXTextureRawBorrow outer_borrow = {0};

        CHECK(pc_gx_texture_raw_begin_borrow(&outer_borrow) == 1);
        CHECK(build_snapshot_with_token(&texture, &dynamic, &lease) == 0);
        CHECK(memcmp(&texture, &old_texture, sizeof(texture)) == 0);
        CHECK(memcmp(&dynamic, &old_dynamic, sizeof(dynamic)) == 0);
        CHECK(memcmp(&lease, &old_lease, sizeof(lease)) == 0);
        CHECK(pc_gx_texture_raw_end_borrow(&outer_borrow) == 1);
    }
    return 0;
}

static int test_complete_batch_flush_before_tlut_mutation(void) {
    _Alignas(32) static uint8_t tlut_bytes[32];
    GXTlutObj tlut_object;
    FlushObservation observation;

    memset(tlut_bytes, 0x99, sizeof(tlut_bytes));
    reset_state();
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    memset(&observation, 0, sizeof(observation));
    pc_gx_set_geometry_flush_fixture_observer(
        observe_geometry_flush, &observation
    );
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(1.0f, 2.0f, 3.0f);
    GXPosition3f32(4.0f, 5.0f, 6.0f);
    GXPosition3f32(7.0f, 8.0f, 9.0f);
    GXInitTlutObj(&tlut_object, tlut_bytes, GX_TL_RGB5A3, 16);
    GXLoadTlut(&tlut_object, 15);
    CHECK(observation.calls == 1);
    CHECK(observation.in_begin == 0);
    CHECK(g_gx.raw_geometry.completed.vertex_count == 3);
    pc_gx_clear_geometry_flush_fixture_observer();
    return 0;
}

static int test_incomplete_batch_fails_closed(void) {
    _Alignas(32) static uint8_t tlut_bytes[32];
    GXTlutObj tlut_object;
    FlushObservation observation;
    PCGXTextureRawState raw;

    memset(tlut_bytes, 0xAA, sizeof(tlut_bytes));
    reset_state();
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    memset(&observation, 0, sizeof(observation));
    pc_gx_set_geometry_flush_fixture_observer(
        observe_geometry_flush, &observation
    );
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(1.0f, 2.0f, 3.0f);
    GXInitTlutObj(&tlut_object, tlut_bytes, GX_TL_RGB5A3, 16);
    GXLoadTlut(&tlut_object, 15);
    pc_gx_texture_raw_snapshot(&raw);
    CHECK(raw.invalid == 1);
    CHECK(observation.calls == 0);
    CHECK(g_gx.in_begin == 1);
    pc_gx_clear_geometry_flush_fixture_observer();
    return 0;
}

int main(void) {
    CHECK(test_empty_and_all_or_nothing() == 0);
    CHECK(test_tiled_mip_converted_and_generation() == 0);
    CHECK(test_tlut_native_le_and_lease_drop() == 0);
    CHECK(test_format_sizes_and_mip_boundaries() == 0);
    CHECK(test_invalidity_and_callback() == 0);
    CHECK(test_borrowed_builder_lifetime() == 0);
    CHECK(test_synchronous_borrow_transaction() == 0);
    CHECK(test_complete_batch_flush_before_tlut_mutation() == 0);
    CHECK(test_incomplete_batch_fails_closed() == 0);
    puts("pc_gx_texture_dynamic_producer_fixture: PASS");
    puts("invariant: canonical Texture/Dynamic publication is pointer-free, lease-scoped, and fail-closed");
    return 0;
}
