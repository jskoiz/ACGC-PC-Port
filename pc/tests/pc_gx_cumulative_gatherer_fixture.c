#include "pc_gx_cumulative_gatherer.h"
#include "pc_gx_texture_raw_state.h"
#include "pc_settings.h"
#include "pc_texture_pack.h"

#include "acgc/gx_canonical_state.h"
#include "acgc/gx_canonical_transform_state.h"

#include <dolphin/gx/GXEnum.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int g_pc_window_w = PC_SCREEN_WIDTH;
int g_pc_window_h = PC_SCREEN_HEIGHT;
int g_pc_widescreen_stretch = 0;
int g_pc_model_viewer_no_cull = 0;

PCSettings g_pc_settings = {
    .texture_filtering = 1
};

int pc_texture_pack_active(void) {
    return 0;
}

GLuint pc_texture_pack_lookup(
    const void* data,
    int data_size,
    int width,
    int height,
    unsigned int format,
    const void* tlut_data,
    int tlut_entries,
    int tlut_is_be,
    int* out_width,
    int* out_height
) {
    (void)data;
    (void)data_size;
    (void)width;
    (void)height;
    (void)format;
    (void)tlut_data;
    (void)tlut_entries;
    (void)tlut_is_be;
    (void)out_width;
    (void)out_height;
    return 0;
}

void pc_gx_tev_seq_reset(void) {
}

void pc_gx_tev_init(void) {
}

void pc_gx_tev_shutdown(void) {
}

static PCGXShaderVariant g_fixture_shader_variant;

PCGXShaderVariant* pc_gx_tev_get_variant(void) {
    return &g_fixture_shader_variant;
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static PCGXCumulativeSnapshotStorage s_storage;
static uint8_t s_envelope_before[PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES];
static PCGXCumulativeSnapshotSection s_sections_before[
    PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT];

typedef struct FixtureObservation {
    int callback_count;
    int callback_envelope_valid;
    int callback_registration_rejected;
    int callback_clear_rejected;
    int callback_nested_gather_rejected;
    size_t callback_byte_size;
    const PCGXRawGeometryBatch* batch;
    PCGXCumulativeSnapshotStorage* storage;
} FixtureObservation;

static uint32_t read_le32(const uint8_t* source) {
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) |
        ((uint32_t)source[3] << 24);
}

static uint32_t float_bits(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int sections_equal(
    const PCGXCumulativeSnapshotSection* first,
    const PCGXCumulativeSnapshotSection* second
) {
    uint32_t index;

    for (index = 0; index < PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT; index++) {
        if (first[index].section_id != second[index].section_id ||
            first[index].section_version != second[index].section_version ||
            first[index].byte_size != second[index].byte_size ||
            first[index].count != second[index].count ||
            first[index].capacity != second[index].capacity ||
            first[index].valid_mask != second[index].valid_mask ||
            first[index].bytes.data != second[index].bytes.data ||
            first[index].bytes.size != second[index].bytes.size) {
            return 0;
        }
    }
    return 1;
}

static void observe_cumulative_snapshot(
    void* context,
    const uint8_t* envelope,
    size_t envelope_byte_size
) {
    FixtureObservation* observation = (FixtureObservation*)context;
    size_t directory_offset;
    uint32_t index;

    observation->callback_count++;
    observation->callback_byte_size = envelope_byte_size;
    observation->callback_envelope_valid = envelope != NULL &&
        envelope_byte_size >= ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET &&
        read_le32(envelope) == ACGC_GX_CANONICAL_ENVELOPE_MAGIC &&
        read_le32(envelope + 16) ==
            ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT;

    for (index = 0; observation->callback_envelope_valid && index < 14; index++) {
        directory_offset = ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET +
            index * ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE;
        if (read_le32(envelope + directory_offset) != index + 1 ||
            read_le32(envelope + directory_offset + 4) != 1 ||
            read_le32(envelope + directory_offset + 12) == 0) {
            observation->callback_envelope_valid = 0;
        }
    }

    /* Registration and re-entry are fail-closed while the exact borrow is
     * active.  No callback receives the borrow or any resource pointer. */
    observation->callback_registration_rejected =
        pc_gx_set_cumulative_snapshot_callback(
            observe_cumulative_snapshot, observation) == 0;
    observation->callback_clear_rejected =
        pc_gx_clear_cumulative_snapshot_callback() == 0;
    observation->callback_nested_gather_rejected =
        pc_gx_cumulative_snapshot_gather(
            observation->batch, observation->storage) == 0;
}

static void initialize_geometry_batch(PCGXRawGeometryBatch* batch) {
    PCGXRawGeometryAttribute* position;
    uint32_t record;

    memset(batch, 0, sizeof(*batch));
    batch->primitive = ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES;
    batch->vertex_count = 3;
    batch->vtxfmt = 0;
    batch->expected_vertex_count = 3;
    batch->known = 1;

    for (record = 0; record < PC_GX_MAX_ATTR; record++) {
        batch->attr[record].vcd_type = GX_NONE;
        batch->attr[record].descriptor_known = 1;
    }

    position = &batch->attr[GX_VA_POS];
    position->vcd_type = GX_DIRECT;
    position->vat_count = GX_POS_XYZ;
    position->vat_type = GX_F32;
    position->vat_fraction = 0;
    position->descriptor_known = 3;
    position->value_word_count = 3;
    position->value_count = batch->vertex_count;
    position->value_words[0][0] = float_bits(0.0f);
    position->value_words[0][1] = float_bits(0.0f);
    position->value_words[0][2] = float_bits(0.0f);
    position->value_words[1][0] = float_bits(1.0f);
    position->value_words[1][1] = float_bits(0.0f);
    position->value_words[1][2] = float_bits(0.0f);
    position->value_words[2][0] = float_bits(0.0f);
    position->value_words[2][1] = float_bits(1.0f);
    position->value_words[2][2] = float_bits(0.0f);
    for (record = 0; record < batch->vertex_count; record++) {
        position->value_known[record] = 1;
    }
}

static void initialize_raw_state(void) {
    uint32_t index;

    memset(&g_gx, 0, sizeof(g_gx));

    /* A minimal but complete Transform epoch: projection, current position,
     * and the current position matrix are sufficient; unknown inactive slots
     * remain zero and are preserved as unavailable canonical records. */
    g_gx.raw_transform.projection.type = GX_PERSPECTIVE;
    g_gx.raw_transform.projection.known = 1;
    g_gx.raw_transform.projection.coefficients[0] = float_bits(1.0f);
    g_gx.raw_transform.current_position_id = 0;
    g_gx.raw_transform.current_position_known = 1;
    g_gx.raw_transform.position[0].known = 1;
    g_gx.raw_transform.position[0].words[0] = float_bits(1.0f);
    g_gx.raw_transform.position[0].words[5] = float_bits(1.0f);
    g_gx.raw_transform.position[0].words[10] = float_bits(1.0f);

    pc_gx_raw_channels_initialize();
    pc_gx_raw_channels_set_num(0);
    pc_gx_raw_lighting_initialize();

    /* Reset the setter-owned matrix logical IDs without introducing any
     * active Texgen records. */
    pc_gx_raw_texgen_shadow_reset_fixture();
    g_gx.raw_texgen.active_texgen_count = 0;
    g_gx.raw_texgen.active_texgen_count_known = 1;

    memset(&g_gx.raw_tev_indirect, 0, sizeof(g_gx.raw_tev_indirect));
    g_gx.raw_tev_indirect.active_tev_stage_count = 1;
    g_gx.raw_tev_indirect.active_tev_stage_count_known = 1;
    g_gx.raw_tev_indirect.stages[0].known_mask =
        PC_GX_RAW_TEV_STAGE_KNOWN_MASK;
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_REGISTER_COUNT; index++) {
        g_gx.raw_tev_indirect.registers[index].valid = 1;
        g_gx.raw_tev_indirect.registers[index].source =
            PCGX_TEV_RAW_SOURCE_COLOR_U8;
        g_gx.raw_tev_indirect.registers[index].known_mask =
            PC_GX_RAW_TEV_COMPONENT_KNOWN_MASK;
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_KONST_COUNT; index++) {
        g_gx.raw_tev_indirect.konst[index].valid = 1;
        g_gx.raw_tev_indirect.konst[index].source =
            PCGX_TEV_RAW_SOURCE_KCOLOR_U8;
        g_gx.raw_tev_indirect.konst[index].known_mask =
            PC_GX_RAW_TEV_COMPONENT_KNOWN_MASK;
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_SWAP_TABLE_COUNT; index++) {
        g_gx.raw_tev_indirect.swap_tables[index].known_mask =
            PC_GX_RAW_TEV_RECORD_KNOWN_MASK;
    }
    g_gx.raw_tev_indirect.active_indirect_stage_count = 0;
    g_gx.raw_tev_indirect.active_indirect_stage_count_known = 1;

    memset(&g_gx.raw_blend, 0, sizeof(g_gx.raw_blend));
    g_gx.raw_blend.known = 1;

    memset(&g_gx.raw_alpha, 0, sizeof(g_gx.raw_alpha));
    g_gx.raw_alpha.known_mask = PC_GX_RAW_ALPHA_KNOWN_ALL;

    memset(&g_gx.raw_depth, 0, sizeof(g_gx.raw_depth));
    g_gx.raw_depth.known = 1;

    memset(&g_gx.raw_raster, 0, sizeof(g_gx.raw_raster));
    g_gx.raw_raster.known_mask = PC_GX_RAW_RASTER_KNOWN_ALL;
    g_gx.raw_raster.line_texcoord_known_mask =
        PC_GX_RAW_RASTER_TEXCOORD_KNOWN_ALL;
    g_gx.raw_raster.point_texcoord_known_mask =
        PC_GX_RAW_RASTER_TEXCOORD_KNOWN_ALL;

    memset(&g_gx.raw_fog, 0, sizeof(g_gx.raw_fog));
    g_gx.raw_fog.known_mask = PC_GX_RAW_FOG_KNOWN_ALL;

    pc_gx_texture_raw_initialize();
}

static int check_envelope(
    const PCGXCumulativeSnapshotStorage* storage,
    size_t expected_byte_size
) {
    size_t offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    uint32_t index;

    CHECK(storage->envelope_byte_size == expected_byte_size);
    CHECK(acgc_gx_canonical_envelope_validate(
        (const AcgcGxCanonicalEnvelope*)storage->envelope,
        storage->envelope_byte_size));
    CHECK(read_le32(storage->envelope) == ACGC_GX_CANONICAL_ENVELOPE_MAGIC);
    CHECK(read_le32(storage->envelope + 4) ==
        ACGC_GX_CANONICAL_ENVELOPE_VERSION);
    CHECK(read_le32(storage->envelope + 16) ==
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT);

    for (index = 0; index < PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT; index++) {
        const size_t directory_offset =
            ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET +
            index * ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE;
        const size_t section_offset = read_le32(
            storage->envelope + directory_offset + 8);
        const size_t section_size = read_le32(
            storage->envelope + directory_offset + 12);

        CHECK(read_le32(storage->envelope + directory_offset) == index + 1);
        CHECK(read_le32(storage->envelope + directory_offset + 4) == 1);
        CHECK(section_offset == offset);
        CHECK(section_size != 0 && (section_size % 4) == 0);
        CHECK(section_offset + section_size <= expected_byte_size);
        offset += section_size;
    }
    CHECK(offset == expected_byte_size);

    /* Verify representative encoded values from every section. These are
     * read through LE byte access, never by casting the wire to a struct. */
    offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    CHECK(read_le32(storage->envelope + offset) ==
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES);
    offset += read_le32(storage->envelope +
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET + 12);
    CHECK(read_le32(storage->envelope + offset) ==
        ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    offset += read_le32(storage->envelope +
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET + 32 + 12);
    CHECK(read_le32(storage->envelope + offset) == 0);
    offset += read_le32(storage->envelope +
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET + 64 + 12);
    CHECK(read_le32(storage->envelope + offset) == 0);
    offset += read_le32(storage->envelope +
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET + 96 + 12);
    CHECK(read_le32(storage->envelope + offset) == 0);
    offset += read_le32(storage->envelope +
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET + 128 + 12);
    CHECK(read_le32(storage->envelope + offset) ==
        ACGC_GX_CANONICAL_TEV_STATE_VERSION);
    offset += read_le32(storage->envelope +
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET + 160 + 12);
    CHECK(read_le32(storage->envelope + offset) == 0);
    offset += read_le32(storage->envelope +
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET + 192 + 12);
    CHECK(read_le32(storage->envelope + offset) == 0);
    offset += read_le32(storage->envelope +
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET + 224 + 12);
    CHECK(read_le32(storage->envelope + offset) == 0);
    offset += read_le32(storage->envelope +
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET + 256 + 12);
    CHECK(read_le32(storage->envelope + offset) == 0);
    offset += read_le32(storage->envelope +
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET + 288 + 12);
    CHECK(read_le32(storage->envelope + offset) == 0);
    offset += read_le32(storage->envelope +
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET + 320 + 12);
    CHECK(read_le32(storage->envelope + offset) == 0);
    offset += read_le32(storage->envelope +
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET + 352 + 12);
    CHECK(read_le32(storage->envelope + offset) ==
        ACGC_GX_CANONICAL_INDIRECT_STATE_VERSION);
    offset += read_le32(storage->envelope +
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET + 384 + 12);
    CHECK(read_le32(storage->envelope + offset) != 0);
    return 0;
}

static int test_success_failure_and_reuse(void) {
    PCGXRawGeometryBatch batch;
    PCGXRawGeometryBatch invalid_batch;
    FixtureObservation observation;
    size_t expected_byte_size;

    initialize_raw_state();
    initialize_geometry_batch(&batch);
    memset(&observation, 0, sizeof(observation));
    observation.batch = &batch;
    observation.storage = &s_storage;
    CHECK(pc_gx_clear_cumulative_snapshot_callback());
    CHECK(pc_gx_set_cumulative_snapshot_callback(
        observe_cumulative_snapshot, &observation));

    CHECK(pc_gx_cumulative_snapshot_gather(&batch, &s_storage));
    CHECK(observation.callback_count == 1);
    CHECK(observation.callback_envelope_valid);
    CHECK(observation.callback_byte_size == s_storage.envelope_byte_size);
    CHECK(observation.callback_registration_rejected);
    CHECK(observation.callback_clear_rejected);
    CHECK(observation.callback_nested_gather_rejected);
    CHECK(!pc_gx_texture_raw_borrow_is_active());

    expected_byte_size = s_storage.envelope_byte_size;
    CHECK(check_envelope(&s_storage, expected_byte_size) == 0);

    /* The same caller-owned storage and callback transaction are reusable. */
    CHECK(pc_gx_cumulative_snapshot_gather(&batch, &s_storage));
    CHECK(observation.callback_count == 2);
    CHECK(s_storage.envelope_byte_size == expected_byte_size);
    CHECK(!pc_gx_texture_raw_borrow_is_active());

    memcpy(s_envelope_before, s_storage.envelope, sizeof(s_envelope_before));
    memcpy(s_sections_before, s_storage.sections, sizeof(s_sections_before));
    expected_byte_size = s_storage.envelope_byte_size;
    invalid_batch = batch;
    invalid_batch.known = 0;
    CHECK(!pc_gx_cumulative_snapshot_gather(
        &invalid_batch, &s_storage));
    CHECK(observation.callback_count == 2);
    CHECK(s_storage.envelope_byte_size == expected_byte_size);
    CHECK(memcmp(s_envelope_before, s_storage.envelope,
                 sizeof(s_envelope_before)) == 0);
    CHECK(sections_equal(s_sections_before, s_storage.sections));
    CHECK(!pc_gx_texture_raw_borrow_is_active());

    CHECK(pc_gx_clear_cumulative_snapshot_callback());
    memcpy(s_envelope_before, s_storage.envelope, sizeof(s_envelope_before));
    expected_byte_size = s_storage.envelope_byte_size;
    CHECK(!pc_gx_cumulative_snapshot_gather(&batch, &s_storage));
    CHECK(observation.callback_count == 2);
    CHECK(s_storage.envelope_byte_size == expected_byte_size);
    CHECK(memcmp(s_envelope_before, s_storage.envelope,
                 sizeof(s_envelope_before)) == 0);
    CHECK(!pc_gx_texture_raw_borrow_is_active());
    return 0;
}

static int test_null_and_borrow_cleanup(void) {
    PCGXRawGeometryBatch batch;
    FixtureObservation observation;
    PCGXTextureRawBorrow external_borrow = {0};

    initialize_raw_state();
    initialize_geometry_batch(&batch);
    memset(&observation, 0, sizeof(observation));
    observation.batch = &batch;
    observation.storage = &s_storage;
    CHECK(pc_gx_set_cumulative_snapshot_callback(
        observe_cumulative_snapshot, &observation));
    CHECK(!pc_gx_cumulative_snapshot_gather(NULL, &s_storage));
    CHECK(!pc_gx_cumulative_snapshot_gather(&batch, NULL));
    CHECK(observation.callback_count == 0);
    CHECK(!pc_gx_texture_raw_borrow_is_active());

    /* An already-active external borrow makes the gatherer's begin step fail
     * closed without consuming or ending the caller's token. */
    CHECK(pc_gx_texture_raw_begin_borrow(&external_borrow));
    CHECK(!pc_gx_cumulative_snapshot_gather(&batch, &s_storage));
    CHECK(observation.callback_count == 0);
    CHECK(pc_gx_texture_raw_borrow_is_active());
    CHECK(pc_gx_texture_raw_end_borrow(&external_borrow));
    CHECK(!pc_gx_texture_raw_borrow_is_active());
    CHECK(pc_gx_clear_cumulative_snapshot_callback());
    return 0;
}

int main(void) {
    CHECK(test_success_failure_and_reuse() == 0);
    CHECK(test_null_and_borrow_cleanup() == 0);

    puts("pc GX cumulative canonical gatherer fixture: PASS");
    puts("proof boundary: production raw builders, explicit little-endian encoders, fourteen-section assembly, synchronous envelope-only callback, registration fail-closed behavior, Geometry failure immutability, and Texture/TLUT borrow cleanup/reuse; no flush insertion, legacy GL, renderer, Metal, device, or playability claim");
    return 0;
}
