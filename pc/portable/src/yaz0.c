#include "acgc/bytes.h"
#include "acgc/yaz0.h"

#include <stdlib.h>
#include <string.h>

static AcgcYaz0Status fail_decode(
    AcgcYaz0Status status,
    uint8_t* decoded,
    uint8_t** output_data,
    uint32_t* output_size
) {
    free(decoded);
    *output_data = NULL;
    *output_size = 0;
    return status;
}

AcgcYaz0Status acgc_yaz0_decode(
    const uint8_t* source,
    size_t source_size,
    uint32_t maximum_output_size,
    uint8_t** output_data,
    uint32_t* output_size
) {
    uint8_t* decoded = NULL;
    uint32_t decoded_size;
    size_t source_pos = 16;
    size_t decoded_pos = 0;

    if (output_data == NULL || output_size == NULL) {
        return ACGC_YAZ0_INVALID_ARGUMENT;
    }

    *output_data = NULL;
    *output_size = 0;

    if (source == NULL) {
        return ACGC_YAZ0_INVALID_ARGUMENT;
    }

    if (source_size < 16 || memcmp(source, "Yaz0", 4) != 0) {
        return ACGC_YAZ0_INVALID_HEADER;
    }

    decoded_size = acgc_load_be32(source + 4);
    if (decoded_size > maximum_output_size) {
        return ACGC_YAZ0_OUTPUT_LIMIT_EXCEEDED;
    }
    if (decoded_size == 0) {
        return ACGC_YAZ0_OK;
    }

    decoded = (uint8_t*)malloc(decoded_size);
    if (decoded == NULL) {
        return ACGC_YAZ0_ALLOCATION_FAILED;
    }

    while (decoded_pos < decoded_size) {
        uint8_t flags;
        int bit;

        if (source_pos >= source_size) {
            return fail_decode(
                ACGC_YAZ0_TRUNCATED_INPUT,
                decoded,
                output_data,
                output_size
            );
        }

        flags = source[source_pos++];
        for (bit = 7; bit >= 0 && decoded_pos < decoded_size; bit--) {
            if ((flags & (1u << bit)) != 0) {
                if (source_pos >= source_size) {
                    return fail_decode(
                        ACGC_YAZ0_TRUNCATED_INPUT,
                        decoded,
                        output_data,
                        output_size
                    );
                }
                decoded[decoded_pos++] = source[source_pos++];
            } else {
                uint8_t byte0;
                uint8_t byte1;
                size_t distance;
                size_t length;
                size_t reference_pos;

                if (source_size - source_pos < 2) {
                    return fail_decode(
                        ACGC_YAZ0_TRUNCATED_INPUT,
                        decoded,
                        output_data,
                        output_size
                    );
                }

                byte0 = source[source_pos++];
                byte1 = source[source_pos++];
                distance = ((size_t)(byte0 & 0x0F) << 8) | byte1;

                if ((byte0 >> 4) == 0) {
                    if (source_pos >= source_size) {
                        return fail_decode(
                            ACGC_YAZ0_TRUNCATED_INPUT,
                            decoded,
                            output_data,
                            output_size
                        );
                    }
                    length = (size_t)source[source_pos++] + 0x12;
                } else {
                    length = (size_t)(byte0 >> 4) + 2;
                }

                if (distance >= decoded_pos) {
                    return fail_decode(
                        ACGC_YAZ0_INVALID_BACK_REFERENCE,
                        decoded,
                        output_data,
                        output_size
                    );
                }

                reference_pos = decoded_pos - distance - 1;
                while (length > 0 && decoded_pos < decoded_size) {
                    decoded[decoded_pos++] = decoded[reference_pos++];
                    length--;
                }
            }
        }
    }

    *output_data = decoded;
    *output_size = decoded_size;
    return ACGC_YAZ0_OK;
}

const char* acgc_yaz0_status_string(AcgcYaz0Status status) {
    switch (status) {
        case ACGC_YAZ0_OK:
            return "ok";
        case ACGC_YAZ0_INVALID_ARGUMENT:
            return "invalid argument";
        case ACGC_YAZ0_INVALID_HEADER:
            return "invalid header";
        case ACGC_YAZ0_TRUNCATED_INPUT:
            return "truncated input";
        case ACGC_YAZ0_INVALID_BACK_REFERENCE:
            return "invalid back-reference";
        case ACGC_YAZ0_OUTPUT_LIMIT_EXCEEDED:
            return "output limit exceeded";
        case ACGC_YAZ0_ALLOCATION_FAILED:
            return "allocation failed";
        default:
            return "unknown error";
    }
}
