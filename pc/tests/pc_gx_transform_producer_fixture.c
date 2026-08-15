#include "pc_gx_transform_producer.h"

#include <dolphin/gx/GXEnum.h>

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

static uint32_t finite_word(uint32_t seed) {
    switch (seed & UINT32_C(3)) {
    case 0:
        return 0;
    case 1:
        return UINT32_C(0x80000000);
    case 2:
        return UINT32_C(0x3F000000) | (seed & UINT32_C(0x007FFFFF));
    default:
        return UINT32_C(0xBF000000) | (seed & UINT32_C(0x007FFFFF));
    }
}

static void fill_raw(
    PCGXRawTransform* raw,
    uint32_t projection_type,
    int complete
) {
    uint32_t slot;
    uint32_t word;

    memset(raw, 0, sizeof(*raw));
    raw->projection.type = projection_type;
    raw->projection.known = 1;
    for (word = 0; word < PC_GX_TRANSFORM_PROJECTION_WORDS; word++) {
        raw->projection.coefficients[word] = finite_word(word + 10);
    }
    raw->current_position_id = 0;
    raw->current_position_known = 1;
    raw->position[0].known = 1;
    for (word = 0; word < PC_GX_TRANSFORM_POSITION_WORDS; word++) {
        raw->position[0].words[word] = finite_word(word + 100);
    }

    if (!complete) {
        return;
    }

    for (slot = 0; slot < PC_GX_TRANSFORM_POSITION_COUNT; slot++) {
        raw->position[slot].known = 1;
        raw->normal[slot].known = 1;
        for (word = 0; word < PC_GX_TRANSFORM_POSITION_WORDS; word++) {
            raw->position[slot].words[word] =
                finite_word(slot * 31 + word + 200);
        }
        for (word = 0; word < PC_GX_TRANSFORM_NORMAL_WORDS; word++) {
            raw->normal[slot].words[word] =
                finite_word(slot * 37 + word + 400);
        }
    }
}

static int expect_failure_unchanged(const PCGXRawTransform* raw) {
    AcgcGxCanonicalTransformState output;
    unsigned char before[sizeof(output)];

    memset(&output, 0xA5, sizeof(output));
    memcpy(before, &output, sizeof(before));
    CHECK(pc_gx_raw_transform_build_canonical(raw, &output) == 0);
    CHECK(memcmp(before, &output, sizeof(output)) == 0);
    return 0;
}

static int expect_success(
    const PCGXRawTransform* raw,
    AcgcGxCanonicalTransformState* output
) {
    CHECK(pc_gx_raw_transform_build_canonical(raw, output) != 0);
    CHECK(acgc_gx_canonical_transform_state_validate(output) != 0);
    return 0;
}

static int test_initial_partial_and_complete(void) {
    PCGXRawTransform raw;
    AcgcGxCanonicalTransformState output;
    uint32_t slot;
    uint32_t word;

    memset(&raw, 0, sizeof(raw));
    CHECK(expect_failure_unchanged(&raw) == 0);
    CHECK(pc_gx_raw_transform_build_canonical(NULL, &output) == 0);
    CHECK(pc_gx_raw_transform_build_canonical(&raw, NULL) == 0);

    fill_raw(&raw, GX_PERSPECTIVE, 0);
    CHECK(expect_success(&raw, &output) == 0);
    CHECK(output.known_mask ==
          (ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_KNOWN_MASK |
           ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_KNOWN_MASK |
           ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(0)));
    for (slot = 1; slot < PC_GX_TRANSFORM_POSITION_COUNT; slot++) {
        CHECK((output.known_mask &
               ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(slot)) == 0);
        CHECK((output.known_mask &
               ACGC_GX_CANONICAL_TRANSFORM_NORMAL_KNOWN_MASK(slot)) == 0);
        for (word = 0; word < PC_GX_TRANSFORM_POSITION_WORDS; word++) {
            CHECK(output.position[slot][word] == 0);
        }
        for (word = 0; word < PC_GX_TRANSFORM_NORMAL_WORDS; word++) {
            CHECK(output.normal[slot][word] == 0);
        }
    }

    for (slot = 0; slot < 2; slot++) {
        fill_raw(
            &raw,
            slot == 0 ? GX_PERSPECTIVE : GX_ORTHOGRAPHIC,
            1
        );
        CHECK(expect_success(&raw, &output) == 0);
        CHECK(output.projection_type == slot);
        CHECK(output.current_position_id == 0);
        for (word = 0; word < PC_GX_TRANSFORM_PROJECTION_WORDS; word++) {
            CHECK(output.projection[word] == raw.projection.coefficients[word]);
        }
        for (word = 0; word < PC_GX_TRANSFORM_POSITION_WORDS; word++) {
            CHECK(output.position[0][word] == raw.position[0].words[word]);
        }
        for (word = 0; word < PC_GX_TRANSFORM_NORMAL_WORDS; word++) {
            CHECK(output.normal[0][word] == raw.normal[0].words[word]);
        }
        CHECK(output.known_mask == ACGC_GX_CANONICAL_TRANSFORM_KNOWN_MASK);
    }
    return 0;
}

static int test_all_legal_current_ids_and_exact_records(void) {
    PCGXRawTransform raw;
    AcgcGxCanonicalTransformState output;
    uint32_t slot;
    uint32_t word;

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    for (slot = 0; slot < PC_GX_TRANSFORM_POSITION_COUNT; slot++) {
        raw.current_position_id = slot * 3;
        raw.current_position_known = 1;
        CHECK(expect_success(&raw, &output) == 0);
        CHECK(output.current_position_id == slot * 3);
        for (word = 0; word < PC_GX_TRANSFORM_POSITION_WORDS; word++) {
            CHECK(output.position[slot][word] == raw.position[slot].words[word]);
        }
        for (word = 0; word < PC_GX_TRANSFORM_NORMAL_WORDS; word++) {
            CHECK(output.normal[slot][word] == raw.normal[slot].words[word]);
        }
    }
    return 0;
}

static int test_current_dependency_and_illegal_ids(void) {
    PCGXRawTransform raw;

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.current_position_id = 3;
    raw.current_position_known = 1;
    raw.position[1].known = 0;
    memset(raw.position[1].words, 0, sizeof(raw.position[1].words));
    CHECK(expect_failure_unchanged(&raw) == 0);

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.current_position_id = 1;
    CHECK(expect_failure_unchanged(&raw) == 0);

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.current_position_known = 0;
    CHECK(expect_failure_unchanged(&raw) == 0);
    return 0;
}

static int test_unresolved_and_sticky_invalid(void) {
    PCGXRawTransform raw;

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.position_indexed_unresolved[2] = 1;
    raw.position[2].known = 0;
    memset(raw.position[2].words, 0, sizeof(raw.position[2].words));
    CHECK(expect_failure_unchanged(&raw) == 0);

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.normal_indexed_unresolved[4] = 1;
    raw.normal[4].known = 0;
    memset(raw.normal[4].words, 0, sizeof(raw.normal[4].words));
    CHECK(expect_failure_unchanged(&raw) == 0);

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.invalid = 1;
    CHECK(expect_failure_unchanged(&raw) == 0);
    raw.invalid = 2;
    CHECK(expect_failure_unchanged(&raw) == 0);
    return 0;
}

static int test_nonfinite_and_malformed_knownness(void) {
    PCGXRawTransform raw;

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.projection.coefficients[0] = UINT32_C(0x7F800000);
    CHECK(expect_failure_unchanged(&raw) == 0);

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.position[3].words[5] = UINT32_C(0x7FC00000);
    CHECK(expect_failure_unchanged(&raw) == 0);

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.normal[5].words[2] = UINT32_C(0xFF800000);
    CHECK(expect_failure_unchanged(&raw) == 0);

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.projection.known = 2;
    CHECK(expect_failure_unchanged(&raw) == 0);

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.position[1].known = 2;
    CHECK(expect_failure_unchanged(&raw) == 0);

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.normal[1].known = 2;
    CHECK(expect_failure_unchanged(&raw) == 0);

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.current_position_known = 2;
    CHECK(expect_failure_unchanged(&raw) == 0);

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.position_indexed_unresolved[1] = 2;
    CHECK(expect_failure_unchanged(&raw) == 0);

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.normal_indexed_unresolved[1] = 2;
    CHECK(expect_failure_unchanged(&raw) == 0);

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.reserved[0] = 1;
    CHECK(expect_failure_unchanged(&raw) == 0);

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.projection.reserved[0] = 1;
    CHECK(expect_failure_unchanged(&raw) == 0);

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.position[0].reserved[1] = 1;
    CHECK(expect_failure_unchanged(&raw) == 0);

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.normal[0].reserved[2] = 1;
    CHECK(expect_failure_unchanged(&raw) == 0);

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.position[2].known = 0;
    raw.position[2].words[0] = finite_word(999);
    CHECK(expect_failure_unchanged(&raw) == 0);

    fill_raw(&raw, GX_PERSPECTIVE, 1);
    raw.projection.type = 99;
    CHECK(expect_failure_unchanged(&raw) == 0);
    return 0;
}

int main(void) {
    CHECK(test_initial_partial_and_complete() == 0);
    CHECK(test_all_legal_current_ids_and_exact_records() == 0);
    CHECK(test_current_dependency_and_illegal_ids() == 0);
    CHECK(test_unresolved_and_sticky_invalid() == 0);
    CHECK(test_nonfinite_and_malformed_knownness() == 0);
    puts("pc GX Transform canonical producer fixture: PASS");
    puts("proof boundary: value-only CPU Transform conversion and fail-closed validation; no flush callback, packet, renderer, Metal, device, pixel, Windows runtime, or playability claim");
    return 0;
}
