#include <dolphin/ai.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "pc_settings.h"

PCSettings g_pc_settings = {
    .master_volume = 100,
};

void pc_audio_process_frame(void) {}

/* Include the implementation so the focused probe can inspect the private
 * ring contents as well as exercise the public AIInitDMA boundary. */
#include "../src/pc_audio.c"

static int fail(const char* message) {
    fprintf(stderr, "pc_audio AI DMA probe: %s\n", message);
    return 1;
}

int main(void) {
#if UINTPTR_MAX <= UINT32_MAX
    return fail("requires a native pointer wider than u32");
#else
    enum { SMALL_SAMPLES = 8 };
    s16* high_samples = NULL;
    s16* ring_samples = NULL;
    const size_t ring_bytes = (size_t)RING_BUF_SAMPLES * sizeof(s16);

    if (posix_memalign((void**)&high_samples, 32, SMALL_SAMPLES * sizeof(s16)) != 0
        || posix_memalign((void**)&ring_samples, 32, ring_bytes) != 0) {
        free(high_samples);
        free(ring_samples);
        return fail("posix_memalign failed");
    }

    if ((uintptr_t)high_samples <= UINT32_MAX || (uintptr_t)ring_samples <= UINT32_MAX) {
        free(high_samples);
        free(ring_samples);
        return fail("probe buffers were not above the legacy u32 range");
    }

    for (int i = 0; i < SMALL_SAMPLES; ++i) {
        high_samples[i] = (s16)(0x1100 + i * 37);
    }
    for (u32 i = 0; i < RING_BUF_SAMPLES; ++i) {
        ring_samples[i] = (s16)(-0x2000 + (int)(i & 0x3F));
    }

    if (pc_audio_get_buffer_fill() != 0) {
        free(high_samples);
        free(ring_samples);
        return fail("ring was not initially empty");
    }

    AIInitDMA((AINativeAddress)(uintptr_t)high_samples,
              SMALL_SAMPLES * sizeof(*high_samples));
    if (pc_audio_get_buffer_fill() != SMALL_SAMPLES) {
        free(high_samples);
        free(ring_samples);
        return fail("high-pointer buffer was not enqueued");
    }
    for (int i = 0; i < SMALL_SAMPLES; ++i) {
        if (ring_buffer[i] != high_samples[i]) {
            free(high_samples);
            free(ring_samples);
            return fail("high-pointer samples were not copied faithfully");
        }
    }

    AIInitDMA((AINativeAddress)0, SMALL_SAMPLES * sizeof(*high_samples));
    AIInitDMA((AINativeAddress)(uintptr_t)high_samples, 0);
    AIInitDMA((AINativeAddress)(uintptr_t)high_samples, sizeof(s16));
    if (pc_audio_get_buffer_fill() != SMALL_SAMPLES) {
        free(high_samples);
        free(ring_samples);
        return fail("null or undersized DMA changed the ring");
    }

    AIInitDMA((AINativeAddress)(uintptr_t)ring_samples, (u32)ring_bytes);
    if (pc_audio_get_buffer_fill() != RING_BUF_SAMPLES
        || pc_audio_get_buffer_fill() > RING_BUF_SAMPLES
        || (pc_audio_get_buffer_fill() & 1) != 0) {
        free(high_samples);
        free(ring_samples);
        return fail("DMA exceeded the stereo ring bound");
    }

    AIInitDMA((AINativeAddress)(uintptr_t)high_samples,
              SMALL_SAMPLES * sizeof(*high_samples));
    if (pc_audio_get_buffer_fill() != RING_BUF_SAMPLES) {
        free(high_samples);
        free(ring_samples);
        return fail("full ring accepted additional samples");
    }

    free(high_samples);
    free(ring_samples);
    puts("pc_audio AI DMA probe: PASS");
    return 0;
#endif
}
