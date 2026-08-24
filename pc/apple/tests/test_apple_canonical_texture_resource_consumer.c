#include "acgc/metal_packet_consumer.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static AcgcMetalPacketConsumerCanonicalResourceStage s_stage;

enum {
    FIXTURE_TEXTURE_WIDTH = 128,
    FIXTURE_TEXTURE_HEIGHT = 32,
    FIXTURE_TEXTURE_IMAGE_BYTES = 2048,
    FIXTURE_TEXTURE_TLUT_BYTES = 32,
    FIXTURE_TEXTURE_DECODED_BYTES = 16384
};

static void fill_texture(
    AcgcGxCanonicalTextureState* texture
) {
    AcgcGxCanonicalTextureRecord* record;

    memset(texture, 0, sizeof(*texture));
    texture->header.known_map_mask = 1;
    texture->header.known_map_count = 1;
    texture->header.indexed_map_mask = 1;
    texture->header.tlut_present_map_mask = 1;
    texture->header.required_map_mask = 1;
    texture->header.record_byte_offset =
        ACGC_GX_CANONICAL_TEXTURE_HEADER_BYTE_SIZE;
    texture->header.record_count = ACGC_GX_CANONICAL_TEXTURE_STATE_COUNT;
    texture->header.record_capacity =
        ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY;
    texture->header.record_word_count =
        ACGC_GX_CANONICAL_TEXTURE_RECORD_WORD_COUNT;
    texture->header.resource_id_scheme =
        ACGC_GX_CANONICAL_TEXTURE_RESOURCE_ID_SCHEME;

    record = &texture->records[0];
    record->flags = ACGC_GX_CANONICAL_TEXTURE_FLAG_INDEXED |
        ACGC_GX_CANONICAL_TEXTURE_FLAG_TLUT_PRESENT |
        ACGC_GX_CANONICAL_TEXTURE_FLAG_RESOURCE_REQUIRED;
    record->image_resource_id =
        ACGC_GX_CANONICAL_TEXTURE_IMAGE_RESOURCE_ID_BASE;
    record->image_owner_epoch = 7;
    record->image_generation_lo = 9;
    record->image_generation_hi = 10;
    record->width = FIXTURE_TEXTURE_WIDTH;
    record->height = FIXTURE_TEXTURE_HEIGHT;
    record->image_format = ACGC_GX_CANONICAL_TEXTURE_FORMAT_C4;
    record->wrap_s = ACGC_GX_CANONICAL_TEXTURE_WRAP_CLAMP;
    record->wrap_t = ACGC_GX_CANONICAL_TEXTURE_WRAP_CLAMP;
    record->min_filter = 0;
    record->mag_filter = 0;
    record->mip_level_count = 1;
    record->image_byte_size = FIXTURE_TEXTURE_IMAGE_BYTES;
    record->image_byte_order = ACGC_GX_CANONICAL_TEXTURE_BYTE_ORDER_GX_BE;
    record->image_source_kind =
        ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_RAW_GUEST;
    record->tlut_resource_id =
        ACGC_GX_CANONICAL_TEXTURE_TLUT_RESOURCE_ID_BASE;
    record->tlut_owner_epoch = 7;
    record->tlut_generation_lo = 11;
    record->tlut_generation_hi = 12;
    record->tlut_name = 0;
    record->tlut_format = ACGC_GX_CANONICAL_TEXTURE_TLUT_FORMAT_IA8;
    record->tlut_entry_count = 16;
    record->tlut_byte_size = 32;
    record->tlut_byte_order = ACGC_GX_CANONICAL_TEXTURE_BYTE_ORDER_GX_BE;
    record->tlut_source_kind =
        ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_RAW_GUEST;
}

static void fill_dynamic(
    AcgcGxCanonicalDynamicState* dynamic
) {
    AcgcGxCanonicalDynamicRecord* image;
    AcgcGxCanonicalDynamicRecord* tlut;

    memset(dynamic, 0, sizeof(*dynamic));
    dynamic->header.owner_epoch = 7;
    dynamic->header.present_image_mask = 1;
    dynamic->header.present_tlut_mask = 1;
    dynamic->header.required_image_mask = 1;
    dynamic->header.required_tlut_mask = 1;
    dynamic->header.present_resource_count = 2;
    dynamic->header.record_byte_offset =
        ACGC_GX_CANONICAL_DYNAMIC_HEADER_BYTE_SIZE;
    dynamic->header.record_count = ACGC_GX_CANONICAL_DYNAMIC_STATE_COUNT;
    dynamic->header.record_capacity =
        ACGC_GX_CANONICAL_DYNAMIC_STATE_CAPACITY;
    dynamic->header.record_word_count =
        ACGC_GX_CANONICAL_DYNAMIC_RECORD_WORD_COUNT;
    dynamic->header.resource_id_scheme =
        ACGC_GX_CANONICAL_DYNAMIC_RESOURCE_ID_SCHEME;

    image = &dynamic->records[0];
    image->resource_id = ACGC_GX_CANONICAL_DYNAMIC_IMAGE_RESOURCE_ID_BASE;
    image->kind = ACGC_GX_CANONICAL_DYNAMIC_KIND_IMAGE;
    image->owner_epoch = 7;
    image->generation_lo = 9;
    image->generation_hi = 10;
    image->owner_slot = 0;
    image->byte_flags = ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE |
        ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED;
    image->byte_size = FIXTURE_TEXTURE_IMAGE_BYTES;
    image->byte_order = ACGC_GX_CANONICAL_DYNAMIC_BYTE_ORDER_GX_BE;
    image->alignment = ACGC_GX_CANONICAL_DYNAMIC_ALIGNMENT_BYTES;
    image->source_kind = ACGC_GX_CANONICAL_DYNAMIC_SOURCE_KIND_RAW_GUEST;
    image->format = ACGC_GX_CANONICAL_DYNAMIC_FORMAT_C4;

    tlut = &dynamic->records[ACGC_GX_CANONICAL_DYNAMIC_IMAGE_MAP_COUNT];
    tlut->resource_id = ACGC_GX_CANONICAL_DYNAMIC_TLUT_RESOURCE_ID_BASE;
    tlut->kind = ACGC_GX_CANONICAL_DYNAMIC_KIND_TLUT;
    tlut->owner_epoch = 7;
    tlut->generation_lo = 11;
    tlut->generation_hi = 12;
    tlut->owner_slot = 0;
    tlut->byte_flags = ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE |
        ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED;
    tlut->byte_size = 32;
    tlut->byte_order = ACGC_GX_CANONICAL_DYNAMIC_BYTE_ORDER_GX_BE;
    tlut->alignment = ACGC_GX_CANONICAL_DYNAMIC_ALIGNMENT_BYTES;
    tlut->source_kind = ACGC_GX_CANONICAL_DYNAMIC_SOURCE_KIND_RAW_GUEST;
    tlut->format = ACGC_GX_CANONICAL_DYNAMIC_TLUT_FORMAT_IA8;
    tlut->element_count = 16;
}

static void fill_lease(
    PCGXTextureDynamicLease* lease,
    uint8_t* image_bytes,
    uint8_t* tlut_bytes
) {
    memset(lease, 0, sizeof(*lease));
    lease->owner_epoch = 7;
    lease->image_mask = 1;
    lease->tlut_mask = 1;
    lease->images[0].bytes = image_bytes;
    lease->images[0].byte_size = FIXTURE_TEXTURE_IMAGE_BYTES;
    lease->images[0].owner_epoch = 7;
    lease->images[0].generation =
        ((uint64_t)10 << 32) | UINT64_C(9);
    lease->images[0].format = ACGC_GX_CANONICAL_TEXTURE_FORMAT_C4;
    lease->images[0].byte_order = ACGC_GX_CANONICAL_TEXTURE_BYTE_ORDER_GX_BE;
    lease->images[0].source_kind =
        ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_RAW_GUEST;
    lease->tluts[0].bytes = tlut_bytes;
    lease->tluts[0].byte_size = FIXTURE_TEXTURE_TLUT_BYTES;
    lease->tluts[0].owner_epoch = 7;
    lease->tluts[0].generation =
        ((uint64_t)12 << 32) | UINT64_C(11);
    lease->tluts[0].format = ACGC_GX_CANONICAL_TEXTURE_TLUT_FORMAT_IA8;
    lease->tluts[0].byte_order = ACGC_GX_CANONICAL_TEXTURE_BYTE_ORDER_GX_BE;
    lease->tluts[0].source_kind =
        ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_RAW_GUEST;
    lease->tluts[0].element_count = 16;
}

static void extend_texture_to_two_maps(
    AcgcGxCanonicalTextureState* texture
) {
    texture->header.known_map_mask = 3;
    texture->header.known_map_count = 2;
    texture->header.indexed_map_mask = 3;
    texture->header.tlut_present_map_mask = 3;
    texture->header.required_map_mask = 3;
    texture->records[1] = texture->records[0];
    texture->records[1].image_resource_id =
        ACGC_GX_CANONICAL_TEXTURE_IMAGE_RESOURCE_ID_BASE + 1;
    texture->records[1].image_generation_lo = 13;
    texture->records[1].image_generation_hi = 14;
}

static void extend_dynamic_to_two_maps(
    AcgcGxCanonicalDynamicState* dynamic
) {
    dynamic->header.present_image_mask = 3;
    dynamic->header.required_image_mask = 3;
    dynamic->header.present_resource_count = 3;
    dynamic->records[1] = dynamic->records[0];
    dynamic->records[1].resource_id =
        ACGC_GX_CANONICAL_DYNAMIC_IMAGE_RESOURCE_ID_BASE + 1;
    dynamic->records[1].generation_lo = 13;
    dynamic->records[1].generation_hi = 14;
    dynamic->records[1].owner_slot = 1;
}

static void extend_lease_to_two_maps(
    PCGXTextureDynamicLease* lease,
    uint8_t* image_bytes
) {
    lease->image_mask = 3;
    lease->images[1] = lease->images[0];
    lease->images[1].bytes = image_bytes;
    lease->images[1].generation =
        ((uint64_t)14 << 32) | UINT64_C(13);
}

static int stage_is_zero(
    const AcgcMetalPacketConsumerCanonicalResourceStage* stage
) {
    const uint8_t* bytes = (const uint8_t*)stage;
    size_t index;

    for (index = 0; index < sizeof(*stage); index++) {
        if (bytes[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int stage_copies_and_decodes_without_retaining_lease(void) {
    AcgcGxCanonicalTextureState texture;
    AcgcGxCanonicalDynamicState dynamic;
    PCGXTextureDynamicLease lease;
    PCGXTextureDynamicLease mismatched;
    uint8_t image_bytes[FIXTURE_TEXTURE_IMAGE_BYTES];
    uint8_t tlut_bytes[FIXTURE_TEXTURE_TLUT_BYTES];
    uint8_t image_before;
    uint8_t tlut_before;

    memset(image_bytes, 0, sizeof(image_bytes));
    memset(tlut_bytes, 0, sizeof(tlut_bytes));
    /* C4 tile index 1 followed by index 0. IA8 palette entries are BE. */
    image_bytes[0] = UINT8_C(0x10);
    tlut_bytes[2] = UINT8_C(0xFF);
    tlut_bytes[3] = UINT8_C(0xFF);
    fill_texture(&texture);
    fill_dynamic(&dynamic);
    fill_lease(&lease, image_bytes, tlut_bytes);
    image_before = image_bytes[0];
    tlut_before = tlut_bytes[2];

    CHECK(acgc_renderer_fixture_texture_bytes(
        FIXTURE_TEXTURE_WIDTH,
        FIXTURE_TEXTURE_HEIGHT,
        ACGC_RENDERER_FIXTURE_TF_C4
    ) == sizeof(image_bytes));
    CHECK(sizeof(image_bytes) == FIXTURE_TEXTURE_IMAGE_BYTES);
    CHECK(sizeof(tlut_bytes) == FIXTURE_TEXTURE_TLUT_BYTES);
    CHECK(FIXTURE_TEXTURE_DECODED_BYTES ==
        FIXTURE_TEXTURE_WIDTH * FIXTURE_TEXTURE_HEIGHT * 4);
    CHECK(acgc_gx_canonical_texture_state_validate(&texture));
    CHECK(acgc_gx_canonical_dynamic_state_validate(&dynamic));
    CHECK(acgc_gx_canonical_texture_dynamic_validate(&texture, &dynamic));
    CHECK(acgc_metal_packet_consumer_stage_canonical_resources(
        &texture, &dynamic, &lease, &s_stage));
    CHECK(s_stage.valid == 1);
    CHECK(s_stage.image_mask == 1);
    CHECK(s_stage.tlut_mask == 1);
    CHECK(s_stage.decoded_image_mask == 1);
    CHECK(s_stage.image_byte_sizes[0] == sizeof(image_bytes));
    CHECK(s_stage.tlut_byte_sizes[0] == sizeof(tlut_bytes));
    CHECK(memcmp(s_stage.image_bytes[0], image_bytes, sizeof(image_bytes)) == 0);
    CHECK(memcmp(s_stage.tlut_bytes[0], tlut_bytes, sizeof(tlut_bytes)) == 0);
    CHECK(s_stage.decoded_rgba_byte_sizes[0] ==
        FIXTURE_TEXTURE_DECODED_BYTES);

    /* The source buffers are borrowed and may change after the synchronous
     * callback; the stage remains caller-owned and unchanged. */
    image_bytes[0] = 0;
    tlut_bytes[2] = 0;
    CHECK(image_before != image_bytes[0]);
    CHECK(tlut_before != tlut_bytes[2]);
    CHECK(s_stage.image_bytes[0][0] == image_before);
    CHECK(s_stage.tlut_bytes[0][2] == tlut_before);

    mismatched = lease;
    mismatched.images[0].generation++;
    CHECK(!acgc_metal_packet_consumer_stage_canonical_resources(
        &texture, &dynamic, &mismatched, &s_stage));
    CHECK(stage_is_zero(&s_stage));

    mismatched = lease;
    mismatched.images[0].byte_size = sizeof(image_bytes) + 1;
    CHECK(!acgc_metal_packet_consumer_stage_canonical_resources(
        &texture, &dynamic, &mismatched, &s_stage));
    CHECK(stage_is_zero(&s_stage));
    return 0;
}

static int late_map_failure_zeroes_all_staged_bytes(void) {
    AcgcGxCanonicalTextureState texture;
    AcgcGxCanonicalDynamicState dynamic;
    PCGXTextureDynamicLease lease;
    uint8_t image_bytes[FIXTURE_TEXTURE_IMAGE_BYTES];
    uint8_t second_image_bytes[FIXTURE_TEXTURE_IMAGE_BYTES];
    uint8_t tlut_bytes[FIXTURE_TEXTURE_TLUT_BYTES];

    memset(image_bytes, 0, sizeof(image_bytes));
    memset(second_image_bytes, 0, sizeof(second_image_bytes));
    memset(tlut_bytes, 0, sizeof(tlut_bytes));
    fill_texture(&texture);
    fill_dynamic(&dynamic);
    fill_lease(&lease, image_bytes, tlut_bytes);
    extend_texture_to_two_maps(&texture);
    extend_dynamic_to_two_maps(&dynamic);
    extend_lease_to_two_maps(&lease, second_image_bytes);

    /* The first TLUT and image are valid and are copied before map 1 reaches
     * this source-faithful base-level sampler rejection.  The complete
     * candidate must still be hidden when the later map fails. */
    texture.records[1].min_filter =
        ACGC_GX_CANONICAL_TEXTURE_MIN_FILTER_MAX;
    CHECK(acgc_gx_canonical_texture_state_validate(&texture));
    CHECK(acgc_gx_canonical_dynamic_state_validate(&dynamic));
    CHECK(acgc_gx_canonical_texture_dynamic_validate(&texture, &dynamic));

    memset(&s_stage, 0xA5, sizeof(s_stage));
    CHECK(!acgc_metal_packet_consumer_stage_canonical_resources(
        &texture, &dynamic, &lease, &s_stage));
    CHECK(stage_is_zero(&s_stage));
    CHECK(s_stage.valid == 0);
    CHECK(s_stage.image_mask == 0);
    CHECK(s_stage.tlut_mask == 0);
    CHECK(s_stage.decoded_image_mask == 0);
    return 0;
}

int main(void) {
    CHECK(stage_copies_and_decodes_without_retaining_lease() == 0);
    CHECK(late_map_failure_zeroes_all_staged_bytes() == 0);
    puts("Apple canonical Texture resource consumer fixture: PASS");
    puts("proof boundary: bounded CPU-side lease metadata validation, raw-byte copy, base-level decode, and post-copy lease isolation; no cumulative borrow, Metal texture/sampler, pixels, device, assets, or playability claim");
    return 0;
}
