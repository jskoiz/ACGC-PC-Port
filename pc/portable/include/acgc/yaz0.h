#ifndef ACGC_PORTABLE_YAZ0_H
#define ACGC_PORTABLE_YAZ0_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AcgcYaz0Status {
    ACGC_YAZ0_OK = 0,
    ACGC_YAZ0_INVALID_ARGUMENT,
    ACGC_YAZ0_INVALID_HEADER,
    ACGC_YAZ0_TRUNCATED_INPUT,
    ACGC_YAZ0_INVALID_BACK_REFERENCE,
    ACGC_YAZ0_OUTPUT_LIMIT_EXCEEDED,
    ACGC_YAZ0_ALLOCATION_FAILED
} AcgcYaz0Status;

/*
 * Decode a Yaz0 stream whose declared output size is no greater than
 * maximum_output_size into a malloc-owned buffer. On success, the caller owns
 * *output_data and must release it with free(). A valid zero-length stream
 * succeeds with *output_data set to NULL.
 */
AcgcYaz0Status acgc_yaz0_decode(
    const uint8_t* source,
    size_t source_size,
    uint32_t maximum_output_size,
    uint8_t** output_data,
    uint32_t* output_size
);

const char* acgc_yaz0_status_string(AcgcYaz0Status status);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_PORTABLE_YAZ0_H */
