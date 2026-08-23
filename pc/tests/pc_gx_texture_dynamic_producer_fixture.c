#include "pc_gx_internal.h"
#include "pc_gx_texture_raw_state.h"

#include <dolphin/gx/GXEnum.h>
#include <dolphin/gx/GXVert.h>
#include <dolphin/gx/GXStruct.h>
#include <dolphin/gx/GXGeometry.h>
#include <dolphin/gx/GXTexture.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void pc_gx_tlut_set_native_le(unsigned int idx);

int g_pc_window_w = PC_SCREEN_WIDTH;
int g_pc_window_h = PC_SCREEN_HEIGHT;
int g_pc_widescreen_stretch = 0;

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
    int borrow_active;
    int nested_result;
    int mutation_state_unchanged;
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

static void observe_active_borrow(
    void* context,
    const AcgcGxCanonicalTextureState* texture,
    const AcgcGxCanonicalDynamicState* dynamic,
    const PCGXTextureDynamicLease* lease
) {
    BorrowObservation* observation = (BorrowObservation*)context;
    PCGXTextureRawState before;
    PCGXTextureRawState after;

    (void)texture;
    (void)dynamic;
    (void)lease;
    observation->calls++;
    observation->borrow_active = pc_gx_texture_raw_borrow_is_active();
    pc_gx_texture_raw_snapshot(&before);

    /* Every raw writer, including a nested publication attempt, must fail
     * closed while the callback owns the synchronous borrow. */
    pc_gx_texture_raw_mark_map_invalid(0);
    pc_gx_texture_raw_drop_image_lease(0);
    pc_gx_texture_raw_mark_global_invalid();
    observation->nested_result = pc_gx_try_texture_dynamic_snapshot();

    pc_gx_texture_raw_snapshot(&after);
    observation->mutation_state_unchanged =
        memcmp(&before, &after, sizeof(before)) == 0;
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

static int build_valid_snapshot(
    AcgcGxCanonicalTextureState* texture,
    AcgcGxCanonicalDynamicState* dynamic,
    PCGXTextureDynamicLease* lease
) {
    CHECK(pc_gx_build_texture_dynamic_snapshot(texture, dynamic, lease) == 1);
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
    CHECK(pc_gx_build_texture_dynamic_snapshot(&texture, &dynamic, &lease) == 0);
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
    CHECK(pc_gx_build_texture_dynamic_snapshot(&texture, &dynamic, &lease) == 0);
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
    CHECK(pc_gx_build_texture_dynamic_snapshot(&texture, &dynamic, &lease) == 0);
    /* Only indexed map 1 depends on the changed TLUT. */
    pc_gx_texture_raw_publish_image_lease(1, image);
    CHECK(build_valid_snapshot(&texture, &dynamic, &lease) == 0);

    pc_gx_texture_raw_drop_image_lease(0);
    CHECK(pc_gx_build_texture_dynamic_snapshot(&texture, &dynamic, &lease) == 0);
    /* Map 0 is not indexed, so its required image lease is the only missing
     * resource in the preceding call; republishing restores the snapshot. */
    pc_gx_texture_raw_publish_image_lease(0, image);
    CHECK(build_valid_snapshot(&texture, &dynamic, &lease) == 0);

    GXInvalidateTexAll();
    CHECK(pc_gx_build_texture_dynamic_snapshot(&texture, &dynamic, &lease) == 0);
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
    CHECK(pc_gx_build_texture_dynamic_snapshot(&texture, &dynamic, &lease) == 0);

    reset_state();
    pc_gx_texture_raw_load_tlut(
        13, GX_TL_RGB565, 0x4001,
        PC_GX_TEXTURE_RAW_BYTE_ORDER_GX_BE,
        PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST
    );
    CHECK(pc_gx_build_texture_dynamic_snapshot(&texture, &dynamic, &lease) == 0);

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

static int test_synchronous_borrow_transaction(void) {
    _Alignas(32) static uint8_t image[32];
    GXTexObj object;
    PCGXTextureRawState raw_before;
    PCGXTextureRawState raw_after;
    BorrowObservation observation;
    AcgcGxCanonicalTextureState texture;
    AcgcGxCanonicalDynamicState dynamic;
    PCGXTextureDynamicLease lease;
    AcgcGxCanonicalTextureState old_texture;
    AcgcGxCanonicalDynamicState old_dynamic;
    PCGXTextureDynamicLease old_lease;

    memset(image, 0x91, sizeof(image));
    reset_state();
    make_image_object(&object, image, 8, 8, GX_TF_I4, GX_FALSE);
    pc_gx_texture_raw_load_map(
        0, object.dummy, PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST, 1
    );
    pc_gx_texture_raw_publish_image_lease(0, image);
    pc_gx_texture_raw_snapshot(&raw_before);
    memset(&observation, 0, sizeof(observation));
    pc_gx_set_texture_dynamic_snapshot_callback(
        observe_active_borrow, &observation
    );
    CHECK(pc_gx_try_texture_dynamic_snapshot() == 1);
    CHECK(observation.calls == 1);
    CHECK(observation.borrow_active == 1);
    CHECK(observation.nested_result == 0);
    CHECK(observation.mutation_state_unchanged == 1);
    pc_gx_texture_raw_snapshot(&raw_after);
    CHECK(memcmp(&raw_before, &raw_after, sizeof(raw_before)) == 0);
    CHECK(build_valid_snapshot(&texture, &dynamic, &lease) == 0);
    pc_gx_clear_texture_dynamic_snapshot_callback();

    /* A reentrant builder cannot partially write caller outputs. */
    reset_state();
    memset(&old_texture, 0x5C, sizeof(old_texture));
    memset(&old_dynamic, 0x5C, sizeof(old_dynamic));
    memset(&old_lease, 0x5C, sizeof(old_lease));
    texture = old_texture;
    dynamic = old_dynamic;
    lease = old_lease;
    CHECK(pc_gx_texture_raw_begin_borrow() == 1);
    CHECK(pc_gx_build_texture_dynamic_snapshot(&texture, &dynamic, &lease) == 0);
    CHECK(memcmp(&texture, &old_texture, sizeof(texture)) == 0);
    CHECK(memcmp(&dynamic, &old_dynamic, sizeof(dynamic)) == 0);
    CHECK(memcmp(&lease, &old_lease, sizeof(lease)) == 0);
    pc_gx_texture_raw_end_borrow();
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
    CHECK(test_synchronous_borrow_transaction() == 0);
    CHECK(test_complete_batch_flush_before_tlut_mutation() == 0);
    CHECK(test_incomplete_batch_fails_closed() == 0);
    puts("pc_gx_texture_dynamic_producer_fixture: PASS");
    puts("invariant: canonical Texture/Dynamic publication is pointer-free, lease-scoped, and fail-closed");
    return 0;
}
