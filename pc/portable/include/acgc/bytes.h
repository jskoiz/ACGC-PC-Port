#ifndef ACGC_PORTABLE_BYTES_H
#define ACGC_PORTABLE_BYTES_H

#include <stdint.h>

static inline uint32_t acgc_load_be32(const uint8_t* bytes) {
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static inline uint32_t acgc_load_le32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

#endif /* ACGC_PORTABLE_BYTES_H */
