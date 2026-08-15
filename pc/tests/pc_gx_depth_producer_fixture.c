#include "pc_gx_depth_producer.h"

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

static void init_valid_raw_depth(PCGXRawDepth* input) {
    memset(input, 0, sizeof(*input));
    input->compare_enable = GX_TRUE;
    input->compare_func = GX_LEQUAL;
    input->update_enable = GX_TRUE;
    input->known = 1;
}

static void init_output_sentinel(AcgcGxCanonicalDepthState* output) {
    memset(output, 0xA5, sizeof(*output));
}

static int output_is_sentinel(
    const AcgcGxCanonicalDepthState* output,
    const AcgcGxCanonicalDepthState* sentinel
) {
    return memcmp(output, sentinel, sizeof(*output)) == 0;
}

static int test_null_and_unknown_provenance_fail_closed(void) {
    PCGXRawDepth input;
    AcgcGxCanonicalDepthState output;
    AcgcGxCanonicalDepthState sentinel;

    init_valid_raw_depth(&input);
    init_output_sentinel(&output);
    sentinel = output;

    CHECK(!pc_gx_raw_depth_build_canonical(NULL, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    CHECK(!pc_gx_raw_depth_build_canonical(&input, NULL));

    input.known = 0;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_depth_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    /* Any noncanonical provenance marker is rejected as invalid, rather than
     * being mistaken for a known setter-owned triple. */
    input.known = 2;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_depth_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    input.known = 1;
    input.reserved[0] = 1;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_depth_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));
    return 0;
}

static int test_all_valid_gx_depth_domains(void) {
    PCGXRawDepth input;
    AcgcGxCanonicalDepthState output;
    uint32_t compare_enable;
    uint32_t compare_func;
    uint32_t update_enable;

    for (compare_enable = 0; compare_enable <= 1; compare_enable++) {
        for (compare_func = GX_NEVER;
             compare_func <= GX_ALWAYS;
             compare_func++) {
            for (update_enable = 0; update_enable <= 1; update_enable++) {
                init_valid_raw_depth(&input);
                input.compare_enable = compare_enable;
                input.compare_func = compare_func;
                input.update_enable = update_enable;
                memset(&output, 0, sizeof(output));

                CHECK(pc_gx_raw_depth_build_canonical(&input, &output));
                CHECK(output.z_compare_enable == compare_enable);
                CHECK(output.z_compare_func == compare_func);
                CHECK(output.z_update_enable == update_enable);
                CHECK(output.reserved == 0);
                CHECK(acgc_gx_canonical_depth_state_validate(&output));
            }
        }
    }
    return 0;
}

static int test_invalid_domains_and_canonical_validation_fail_closed(void) {
    PCGXRawDepth input;
    AcgcGxCanonicalDepthState output;
    AcgcGxCanonicalDepthState sentinel;
    static const uint32_t invalid_boolean[] = {2, UINT32_MAX};
    static const uint32_t invalid_compare[] = {8, UINT32_MAX};
    size_t index;

    init_valid_raw_depth(&input);

    for (index = 0;
         index < sizeof(invalid_boolean) / sizeof(invalid_boolean[0]);
         index++) {
        input.compare_enable = invalid_boolean[index];
        init_output_sentinel(&output);
        sentinel = output;
        CHECK(!pc_gx_raw_depth_build_canonical(&input, &output));
        CHECK(output_is_sentinel(&output, &sentinel));

        init_valid_raw_depth(&input);
        input.update_enable = invalid_boolean[index];
        init_output_sentinel(&output);
        sentinel = output;
        CHECK(!pc_gx_raw_depth_build_canonical(&input, &output));
        CHECK(output_is_sentinel(&output, &sentinel));
        init_valid_raw_depth(&input);
    }

    for (index = 0;
         index < sizeof(invalid_compare) / sizeof(invalid_compare[0]);
         index++) {
        input.compare_func = invalid_compare[index];
        init_output_sentinel(&output);
        sentinel = output;
        /* The candidate reaches the existing canonical validator, which
         * rejects the same out-of-range GX compare domain. */
        CHECK(!pc_gx_raw_depth_build_canonical(&input, &output));
        CHECK(output_is_sentinel(&output, &sentinel));
        init_valid_raw_depth(&input);
    }
    return 0;
}

static int test_success_is_repeatable_and_does_not_mutate_input(void) {
    PCGXRawDepth input;
    PCGXRawDepth input_before;
    AcgcGxCanonicalDepthState first;
    AcgcGxCanonicalDepthState second;

    init_valid_raw_depth(&input);
    input.compare_enable = GX_FALSE;
    input.compare_func = GX_GREATER;
    input.update_enable = GX_FALSE;
    input_before = input;

    CHECK(pc_gx_raw_depth_build_canonical(&input, &first));
    CHECK(memcmp(&input, &input_before, sizeof(input)) == 0);

    memset(&second, 0x5A, sizeof(second));
    CHECK(pc_gx_raw_depth_build_canonical(&input, &second));
    CHECK(memcmp(&first, &second, sizeof(first)) == 0);
    CHECK(first.z_compare_enable == GX_FALSE);
    CHECK(first.z_compare_func == GX_GREATER);
    CHECK(first.z_update_enable == GX_FALSE);
    CHECK(first.reserved == 0);
    return 0;
}

int main(void) {
    if (test_null_and_unknown_provenance_fail_closed() != 0 ||
        test_all_valid_gx_depth_domains() != 0 ||
        test_invalid_domains_and_canonical_validation_fail_closed() != 0 ||
        test_success_is_repeatable_and_does_not_mutate_input() != 0) {
        return 1;
    }

    puts("pc GX raw Depth producer fixture: PASS");
    puts("proof boundary: setter-owned CPU Depth snapshot conversion and canonical validation only; no renderer, Metal, device, pixel, or playability claim");
    return 0;
}
