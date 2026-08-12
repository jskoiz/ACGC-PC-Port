#include <dolphin/ar.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TRANSFER_SIZE = 0x80 };

static int check_bytes(const uint8_t* actual, const uint8_t* expected, size_t size, const char* label) {
    if (memcmp(actual, expected, size) == 0) {
        return 0;
    }

    fprintf(stderr, "%s mismatch\n", label);
    return 1;
}

int main(void) {
#if UINTPTR_MAX <= UINT32_MAX
    fprintf(stderr, "native arm64 address test requires a wider-than-u32 uintptr_t\n");
    return 1;
#else
    uint8_t* source = NULL;
    uint8_t* roundtrip = NULL;
    if (posix_memalign((void**)&source, 32, TRANSFER_SIZE) != 0
        || posix_memalign((void**)&roundtrip, 32, TRANSFER_SIZE) != 0) {
        fprintf(stderr, "posix_memalign failed\n");
        free(source);
        free(roundtrip);
        return 1;
    }

    if ((uintptr_t)source <= UINT32_MAX || (uintptr_t)roundtrip <= UINT32_MAX) {
        fprintf(stderr, "test buffers were not allocated above the legacy u32 range\n");
        free(source);
        free(roundtrip);
        return 1;
    }

    for (size_t i = 0; i < TRANSFER_SIZE; ++i) {
        source[i] = (uint8_t)(0x30u + (i * 7u));
    }
    memset(roundtrip, 0, TRANSFER_SIZE);

    ARInit(NULL, 0);
    const u32 aramAddress = ARAlloc(TRANSFER_SIZE);

    ARStartDMA(ARAM_DIR_MRAM_TO_ARAM, (ARNativeAddress)(uintptr_t)source, aramAddress, TRANSFER_SIZE);
    ARQPostRequestNative(ARAM_DIR_ARAM_TO_MRAM, (ARNativeAddress)(uintptr_t)roundtrip, aramAddress,
                         TRANSFER_SIZE);
    if (check_bytes(roundtrip, source, TRANSFER_SIZE, "ARStartDMA/ARQPostRequestNative")) {
        free(source);
        free(roundtrip);
        return 1;
    }

    for (size_t i = 0; i < TRANSFER_SIZE; ++i) {
        source[i] = (uint8_t)(0xA5u ^ (uint8_t)i);
    }
    memset(roundtrip, 0, TRANSFER_SIZE);
    ARQPostRequestNative(ARAM_DIR_MRAM_TO_ARAM, (ARNativeAddress)(uintptr_t)source, aramAddress,
                         TRANSFER_SIZE);
    ARStartDMA(ARAM_DIR_ARAM_TO_MRAM, (ARNativeAddress)(uintptr_t)roundtrip, aramAddress, TRANSFER_SIZE);
    if (check_bytes(roundtrip, source, TRANSFER_SIZE, "ARQPostRequestNative/ARStartDMA")) {
        free(source);
        free(roundtrip);
        return 1;
    }

    free(source);
    free(roundtrip);
    return 0;
#endif
}
