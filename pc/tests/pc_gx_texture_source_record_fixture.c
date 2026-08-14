#include "pc_gx_internal.h"

#include <dolphin/gx/GXEnum.h>
#include <dolphin/gx/GXTexture.h>

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

/* The fixture never enters the renderer/TEV path. */
PCGXShaderVariant* pc_gx_tev_get_variant(void) {
    return NULL;
}

static void make_raw_source(PCGXTextureSource* source, const void* image) {
    memset(source, 0, sizeof(*source));
    source->image_ptr = image;
    source->image_byte_size = 32;
    source->width = 8;
    source->height = 8;
    source->format = GX_TF_I4;
    source->wrap_s = GX_CLAMP;
    source->wrap_t = GX_CLAMP;
    source->min_filter = GX_NEAR;
    source->mag_filter = GX_NEAR;
    source->effective_filter = GX_NEAR;
    source->tlut_name = UINT32_MAX;
    source->source_kind = PCGX_TEXTURE_SOURCE_RAW_GUEST;
    source->tlut_source_kind = PCGX_TEXTURE_SOURCE_NONE;
}

int main(void) {
    _Alignas(32) static uint8_t image_bytes[32];
    _Alignas(32) static uint8_t tlut_bytes[32];
    GXTlutObj tlut_object;
    PCGXTextureSource candidate;
    PCGXTextureSource output;
    uint64_t first_generation;

    memset(image_bytes, 0xA5, sizeof(image_bytes));
    memset(tlut_bytes, 0x5A, sizeof(tlut_bytes));
    pc_gx_texture_init();

    CHECK(pc_gx_get_v2_texture_source(0, &output) == 0);
    CHECK(pc_gx_get_v2_texture_source(-1, &output) == 0);
    CHECK(pc_gx_get_v2_texture_source(8, &output) == 0);

    make_raw_source(&candidate, image_bytes);
    CHECK(pc_gx_texture_source_fixture_store(3, &candidate) == 1);
    CHECK(pc_gx_get_v2_texture_source(3, &output) == 1);
    CHECK(output.image_ptr == image_bytes);
    CHECK(output.image_byte_size == sizeof(image_bytes));
    CHECK(output.width == 8 && output.height == 8);
    CHECK(output.format == GX_TF_I4);
    CHECK(output.source_kind == PCGX_TEXTURE_SOURCE_RAW_GUEST);
    CHECK(output.tlut_source_kind == PCGX_TEXTURE_SOURCE_NONE);
    CHECK(output.generation != 0);
    first_generation = output.generation;
    CHECK(image_bytes[0] == 0xA5 && tlut_bytes[0] == 0x5A);

    /* A cache/GL key, malformed size, or unaligned pointer cannot populate it. */
    candidate.image_byte_size = 31;
    CHECK(pc_gx_texture_source_fixture_store(3, &candidate) == 0);
    CHECK(pc_gx_get_v2_texture_source(3, &output) == 0);
    make_raw_source(&candidate, image_bytes + 1);
    CHECK(pc_gx_texture_source_fixture_store(3, &candidate) == 0);
    CHECK(pc_gx_get_v2_texture_source(3, &output) == 0);

    /* Indexed sources require a validated, borrowed TLUT record. */
    make_raw_source(&candidate, image_bytes);
    candidate.format = GX_TF_C4;
    candidate.tlut_ptr = tlut_bytes;
    candidate.tlut_byte_size = sizeof(tlut_bytes);
    candidate.tlut_format = GX_TL_RGB5A3;
    candidate.tlut_entries = 16;
    candidate.tlut_name = 2;
    candidate.tlut_is_be = 1;
    candidate.tlut_source_kind = PCGX_TEXTURE_SOURCE_RAW_GUEST;
    CHECK(pc_gx_texture_source_fixture_store(2, &candidate) == 1);
    CHECK(pc_gx_get_v2_texture_source(2, &output) == 1);
    CHECK(output.tlut_ptr == tlut_bytes);
    CHECK(output.tlut_byte_size == sizeof(tlut_bytes));
    CHECK(output.tlut_entries == 16);
    CHECK(output.tlut_source_kind == PCGX_TEXTURE_SOURCE_RAW_GUEST);

    candidate.tlut_ptr = NULL;
    candidate.tlut_byte_size = 0;
    candidate.tlut_entries = 0;
    candidate.tlut_source_kind = PCGX_TEXTURE_SOURCE_NONE;
    CHECK(pc_gx_texture_source_fixture_store(2, &candidate) == 0);
    CHECK(pc_gx_get_v2_texture_source(2, &output) == 0);

    /* Native-LE TLUT conversion is explicit metadata, never a GL identity. */
    candidate.tlut_ptr = tlut_bytes;
    candidate.tlut_byte_size = sizeof(tlut_bytes);
    candidate.tlut_format = GX_TL_RGB5A3;
    candidate.tlut_entries = 16;
    candidate.tlut_name = 2;
    candidate.tlut_is_be = 0;
    candidate.tlut_source_kind = PCGX_TEXTURE_SOURCE_EMU64_CONVERTED;
    CHECK(pc_gx_texture_source_fixture_store(2, &candidate) == 1);
    CHECK(pc_gx_get_v2_texture_source(2, &output) == 1);
    CHECK(output.tlut_source_kind == PCGX_TEXTURE_SOURCE_EMU64_CONVERTED);

    /* Loading/replacing a TLUT invalidates every per-map borrowed record. */
    GXInitTlutObj(&tlut_object, tlut_bytes, GX_TL_RGB5A3, 16);
    GXLoadTlut(&tlut_object, 2);
    CHECK(pc_gx_get_v2_texture_source(2, &output) == 0);
    CHECK(pc_gx_get_v2_texture_source(3, &output) == 0);

    /* Reinitialization clears records and advances the generation domain. */
    CHECK(pc_gx_texture_source_fixture_store(3, &candidate) == 1);
    CHECK(pc_gx_get_v2_texture_source(3, &output) == 1);
    CHECK(output.generation > first_generation);
    pc_gx_texture_init();
    CHECK(pc_gx_get_v2_texture_source(3, &output) == 0);

    puts("pc_gx_texture_source_record_fixture: PASS");
    puts("invariant: only validated borrowed CPU metadata is exposed; GL-only and invalid paths fail closed");
    return 0;
}
