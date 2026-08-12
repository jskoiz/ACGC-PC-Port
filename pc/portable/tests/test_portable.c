#include "acgc/bytes.h"
#include "acgc/yaz0.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_OUTPUT_LIMIT UINT32_MAX

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int test_fixed_width_byte_loads(void) {
    static const uint8_t bytes[] = { 0x12, 0x34, 0x56, 0x78 };

    CHECK(sizeof(uint32_t) == 4);
    CHECK(acgc_load_be32(bytes) == UINT32_C(0x12345678));
    CHECK(acgc_load_le32(bytes) == UINT32_C(0x78563412));
    return 0;
}

static int test_literal_stream(void) {
    static const uint8_t encoded[] = {
        'Y', 'a', 'z', '0',
        0x00, 0x00, 0x00, 0x03,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0xE0, 'A', 'B', 'C'
    };
    uint8_t* decoded = NULL;
    uint32_t decoded_size = 0;

    CHECK(acgc_yaz0_decode(
        encoded,
        sizeof(encoded),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_OK);
    CHECK(decoded != NULL);
    CHECK(decoded_size == 3);
    CHECK(memcmp(decoded, "ABC", 3) == 0);
    free(decoded);
    return 0;
}

static int test_back_reference_stream(void) {
    static const uint8_t encoded[] = {
        'Y', 'a', 'z', '0',
        0x00, 0x00, 0x00, 0x09,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0xE0, 'A', 'B', 'C', 0x40, 0x02
    };
    uint8_t* decoded = NULL;
    uint32_t decoded_size = 0;

    CHECK(acgc_yaz0_decode(
        encoded,
        sizeof(encoded),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_OK);
    CHECK(decoded != NULL);
    CHECK(decoded_size == 9);
    CHECK(memcmp(decoded, "ABCABCABC", 9) == 0);
    free(decoded);
    return 0;
}

static int test_extended_back_reference_stream(void) {
    static const uint8_t encoded[] = {
        'Y', 'a', 'z', '0',
        0x00, 0x00, 0x00, 0x15,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x80, 'A', 0x00, 0x00, 0x02
    };
    static const char expected[] = "AAAAAAAAAAAAAAAAAAAAA";
    uint8_t* decoded = NULL;
    uint32_t decoded_size = 0;

    CHECK(acgc_yaz0_decode(
        encoded,
        sizeof(encoded),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_OK);
    CHECK(decoded != NULL);
    CHECK(decoded_size == sizeof(expected) - 1);
    CHECK(memcmp(decoded, expected, sizeof(expected) - 1) == 0);
    free(decoded);
    return 0;
}

static int test_zero_length_stream(void) {
    static const uint8_t encoded[] = {
        'Y', 'a', 'z', '0',
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };
    uint8_t* decoded = (uint8_t*)(uintptr_t)1;
    uint32_t decoded_size = 1;

    CHECK(acgc_yaz0_decode(
        encoded,
        sizeof(encoded),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_OK);
    CHECK(decoded == NULL);
    CHECK(decoded_size == 0);
    return 0;
}

static int test_rejects_malformed_streams(void) {
    static const uint8_t truncated_literal[] = {
        'Y', 'a', 'z', '0',
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x80
    };
    static const uint8_t invalid_reference[] = {
        'Y', 'a', 'z', '0',
        0x00, 0x00, 0x00, 0x03,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x10, 0x00
    };
    static const uint8_t truncated_reference[] = {
        'Y', 'a', 'z', '0',
        0x00, 0x00, 0x00, 0x03,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x10
    };
    static const uint8_t truncated_extended_length[] = {
        'Y', 'a', 'z', '0',
        0x00, 0x00, 0x00, 0x03,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x80, 'A', 0x00, 0x00
    };
    static const uint8_t bad_header[16] = { 0 };
    static const uint8_t short_yaz0_header[] = { 'Y', 'a', 'z', '0' };
    static const uint8_t oversized_output[] = {
        'Y', 'a', 'z', '0',
        0x00, 0x00, 0x10, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };
    uint8_t* decoded = (uint8_t*)(uintptr_t)1;
    uint32_t decoded_size = 1;

    CHECK(acgc_yaz0_decode(
        NULL,
        0,
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_INVALID_ARGUMENT);
    CHECK(decoded == NULL);
    CHECK(decoded_size == 0);

    CHECK(acgc_yaz0_decode(
        truncated_literal,
        sizeof(truncated_literal),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_TRUNCATED_INPUT);
    CHECK(decoded == NULL);
    CHECK(decoded_size == 0);

    CHECK(acgc_yaz0_decode(
        truncated_reference,
        sizeof(truncated_reference),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_TRUNCATED_INPUT);
    CHECK(decoded == NULL);
    CHECK(decoded_size == 0);

    CHECK(acgc_yaz0_decode(
        truncated_extended_length,
        sizeof(truncated_extended_length),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_TRUNCATED_INPUT);
    CHECK(decoded == NULL);
    CHECK(decoded_size == 0);

    CHECK(acgc_yaz0_decode(
        invalid_reference,
        sizeof(invalid_reference),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_INVALID_BACK_REFERENCE);
    CHECK(decoded == NULL);
    CHECK(decoded_size == 0);

    CHECK(acgc_yaz0_decode(
        bad_header,
        sizeof(bad_header),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_INVALID_HEADER);
    CHECK(decoded == NULL);
    CHECK(decoded_size == 0);

    CHECK(acgc_yaz0_decode(
        short_yaz0_header,
        sizeof(short_yaz0_header),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_INVALID_HEADER);
    CHECK(decoded == NULL);
    CHECK(decoded_size == 0);

    CHECK(acgc_yaz0_decode(
        oversized_output,
        sizeof(oversized_output),
        1024,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_OUTPUT_LIMIT_EXCEEDED);
    CHECK(decoded == NULL);
    CHECK(decoded_size == 0);

    CHECK(acgc_yaz0_decode(
        bad_header,
        sizeof(bad_header),
        TEST_OUTPUT_LIMIT,
        NULL,
        &decoded_size
    ) == ACGC_YAZ0_INVALID_ARGUMENT);
    CHECK(acgc_yaz0_decode(
        bad_header,
        sizeof(bad_header),
        TEST_OUTPUT_LIMIT,
        &decoded,
        NULL
    ) == ACGC_YAZ0_INVALID_ARGUMENT);
    return 0;
}

int main(void) {
    CHECK(test_fixed_width_byte_loads() == 0);
    CHECK(test_literal_stream() == 0);
    CHECK(test_back_reference_stream() == 0);
    CHECK(test_extended_back_reference_stream() == 0);
    CHECK(test_zero_length_stream() == 0);
    CHECK(test_rejects_malformed_streams() == 0);

    printf("acgc portable tests passed\n");
    return 0;
}
