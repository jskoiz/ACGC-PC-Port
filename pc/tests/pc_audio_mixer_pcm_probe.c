/* Synthetic software-mixer-to-callback proof.
 *
 * This is intentionally separate from pc_audio_device_probe.c: it never opens
 * an SDL device and does not measure callback cadence.  It feeds distinct
 * interleaved PCM through the actual TARGET_PC Jac_VframeWork mixer handoff,
 * enqueues the resulting DAC block with AIInitDMA, and invokes the real SDL
 * callback directly to verify sample order and values at the ring boundary.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc_settings.h"
#include "jaudio_NES/dspbuf.h"
#include "jaudio_NES/rate.h"

PCSettings g_pc_settings = {
    .master_volume = 100,
};

u32 JAC_FRAMESAMPLES = 560;
u32 DAC_SIZE = 1120;

void pc_audio_process_frame(void) {}

/* Include the adapter so this probe exercises its private ring and callback,
 * without opening a device through AIInit. */
#include "../src/pc_audio.c"

static s16 synthetic_dsp_track[1120];
static s16 synthetic_cpu_track[1120];
static int mix_dsp_arg_error;

/* Include the real software mixer handoff.  The focused link below uses
 * function-section dead stripping so unrelated audio-engine entry points are
 * not part of this proof. */
#include "../../src/static/jaudio_NES/internal/aictrl.c"

static s16* synthetic_mix_dsp(s32 n_samples) {
    if (n_samples != 560) {
        mix_dsp_arg_error = 1;
    }
    return synthetic_dsp_track;
}

static s16* synthetic_mix_cpu(s32 n_samples) {
    if (n_samples != 560) {
        mix_dsp_arg_error = 1;
    }
    return synthetic_cpu_track;
}

s16* MixDsp(s32 n_samples) {
    return synthetic_mix_dsp(n_samples);
}

void Jac_bcopy(void* src, void* dst, s32 size) {
    memcpy(dst, src, (size_t)size);
}

void Jac_bzero(void* dst, s32 size) {
    memset(dst, 0, (size_t)size);
}

void Probe_Start(s32 id, const char* label) {
    (void)id;
    (void)label;
}

void Probe_Finish(s32 id) {
    (void)id;
}

static int fail(const char* message) {
    fprintf(stderr, "pc_audio mixer PCM probe: %s\n", message);
    return 1;
}

static s16 expected_mix(s16 dsp, s16 cpu) {
    int mixed = (int)dsp + (int)cpu;
    if (mixed > S16_MAX) {
        mixed = S16_MAX;
    }
    if (mixed < S16_MIN) {
        mixed = S16_MIN + 1;
    }
    return (s16)mixed;
}

int main(void) {
    enum { STEREO_SAMPLES = 1120 };
    s16 callback_output[STEREO_SAMPLES];
    s16 expected_output[STEREO_SAMPLES];

    if (audio_device != 0) {
        return fail("device was opened before the proof started");
    }

    for (int frame = 0; frame < STEREO_SAMPLES / 2; ++frame) {
        int left = 1000 + frame;
        int right = -2000 - frame;
        int cpu_left = 300 + (frame * 2);
        int cpu_right = 400 + (frame * 3);

        if (frame == 17) {
            left = 30000;
            cpu_left = 10000;
        }

        synthetic_dsp_track[frame * 2] = (s16)left;
        synthetic_dsp_track[frame * 2 + 1] = (s16)right;
        synthetic_cpu_track[frame * 2] = (s16)cpu_left;
        synthetic_cpu_track[frame * 2 + 1] = (s16)cpu_right;

        expected_output[frame * 2] = expected_mix((s16)left, (s16)cpu_left);
        expected_output[frame * 2 + 1] = expected_mix((s16)right, (s16)cpu_right);
    }

    for (int i = 0; i < 3; ++i) {
        dac[i] = (s16*)calloc(STEREO_SAMPLES, sizeof(s16));
        if (dac[i] == NULL) {
            return fail("DAC allocation failed");
        }
    }

    Jac_RegisterMixcallback(synthetic_mix_cpu, MixMode_Interleave);
    Jac_VframeWork();

    if (mix_dsp_arg_error != 0) {
        return fail("mixer received an unexpected frame size");
    }
    if (last_rsp_madep != dac[0]) {
        return fail("mixer did not publish its DAC block");
    }
    if (memcmp(dac[0], expected_output, sizeof(expected_output)) != 0) {
        return fail("software mixer changed interleaved PCM order or values");
    }

    AIInitDMA((AINativeAddress)(uintptr_t)last_rsp_madep, DAC_SIZE * 2);
    if (pc_audio_get_buffer_fill() != STEREO_SAMPLES) {
        return fail("mixer DAC block was not fully enqueued");
    }

    memset(callback_output, 0x5A, sizeof(callback_output));
    pc_audio_callback(NULL, (Uint8*)callback_output, sizeof(callback_output));
    if (pc_audio_get_buffer_fill() != 0) {
        return fail("callback did not consume the complete mixer block");
    }
    if (memcmp(callback_output, expected_output, sizeof(expected_output)) != 0) {
        return fail("callback PCM differed from the mixer PCM");
    }

    for (int i = 0; i < 3; ++i) {
        free(dac[i]);
        dac[i] = NULL;
    }

    puts("pc_audio mixer PCM probe: PASS (software mixer -> DAC -> callback; device not opened)");
    return 0;
}
