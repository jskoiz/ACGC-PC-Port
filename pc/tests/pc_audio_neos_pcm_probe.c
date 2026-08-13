/* Synthetic NEOS/RSP-to-DAC provenance proof.
 *
 * The fixture replaces only the asset-dependent Neos_Update producer seam. It
 * still runs the real TARGET_PC RSP command interpreter, the real Cpubuf
 * triple buffer, Jac_VframeWork, Jac_UpdateDAC, and the SDL adapter callback.
 * No SDL device is opened, and no ISO or extracted asset is needed.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc_settings.h"
#include "jaudio_NES/audiocommon.h"
#include "jaudio_NES/pc_audiocmd.h"
#include "jaudio_NES/dspbuf.h"
#include "jaudio_NES/rate.h"
#include "dolphin/os/OSAlloc.h"

PCSettings g_pc_settings = {
    .master_volume = 100,
};

u32 JAC_FRAMESAMPLES = 560;
u32 DAC_SIZE = 1120;

void pc_audio_process_frame(void) {}

/* Include the same adapter and engine seams used by the integrated mixer
 * probe, then add the actual RSP and Cpubuf layers below it. */
#include "../src/pc_audio.c"
#include "../../src/static/jaudio_NES/internal/rspsim.c"
#include "../../src/static/jaudio_NES/internal/cpubuf.c"
#include "../../src/static/jaudio_NES/internal/aictrl.c"

enum {
    DAC_SAMPLES = 1120,
    RSP_INPUT_SAMPLES = 144,
    RSP_OUTPUT_SAMPLES = 280,
    RSP_CHUNKS = 4,
    RSP_COMMANDS = RSP_CHUNKS * 4,
    NATIVE_POINTER_SLOTS = 32,
};

static s16 synthetic_dsp_track[DAC_SAMPLES];
static Acmd rsp_commands[RSP_COMMANDS];
static s16 rsp_left[RSP_CHUNKS * RSP_INPUT_SAMPLES];
static s16 rsp_right[RSP_CHUNKS * RSP_INPUT_SAMPLES];
static s16 rsp_output[RSP_CHUNKS][RSP_OUTPUT_SAMPLES];
static s16 neos_pcm[DAC_SAMPLES];
static s16 first_neos_pcm[DAC_SAMPLES];
static unsigned neos_update_count;
static int rsp_error;
static s32 last_rsp_consumed;

typedef struct {
    const void* command;
    const void* pointer;
} NativePointer;

static NativePointer native_pointers[NATIVE_POINTER_SLOTS];

BOOL pc_audio_command_set_native_ptr(const void* command, const void* pointer) {
    int free_slot = -1;

    if (command == NULL) {
        return FALSE;
    }

    for (int i = 0; i < NATIVE_POINTER_SLOTS; ++i) {
        if (native_pointers[i].command == command) {
            native_pointers[i].pointer = pointer;
            return TRUE;
        }
        if (free_slot < 0 && native_pointers[i].command == NULL) {
            free_slot = i;
        }
    }

    if (free_slot < 0) {
        return FALSE;
    }

    native_pointers[free_slot].command = command;
    native_pointers[free_slot].pointer = pointer;
    return TRUE;
}

BOOL pc_audio_command_get_native_ptr(const void* command, const void** pointer) {
    if (command == NULL || pointer == NULL) {
        return FALSE;
    }

    for (int i = 0; i < NATIVE_POINTER_SLOTS; ++i) {
        if (native_pointers[i].command == command) {
            *pointer = native_pointers[i].pointer;
            return TRUE;
        }
    }

    return FALSE;
}

/* The Cpubuf init path uses OSAlloc2 from aictrl.c.  This heap stub keeps the
 * proof independent of the full Dolphin runtime. */
volatile OSHeapHandle __OSCurrHeap = 0;

BOOL OSDisableInterrupts(void) {
    return FALSE;
}

BOOL OSRestoreInterrupts(BOOL enabled) {
    return enabled;
}

void* OSAllocFromHeap(int heap, unsigned long size) {
    (void)heap;
    return calloc(1, (size_t)size);
}

void* Nas_HeapAlloc(ALHeap* heap, s32 size) {
    (void)heap;
    return calloc(1, (size_t)size);
}

void Jac_bcopy(void* src, void* dst, s32 size) {
    if (size > 0) {
        memmove(dst, src, (size_t)size);
    }
}

void Jac_bzero(void* dst, s32 size) {
    if (size > 0) {
        memset(dst, 0, (size_t)size);
    }
}

void DCTouchRange(void* address, u32 length) {
    (void)address;
    (void)length;
}

void DCZeroRange(void* address, u32 length) {
    (void)address;
    (void)length;
}

void OSReport(const char* format, ...) {
    (void)format;
}

void Probe_Start(s32 id, const char* label) {
    (void)id;
    (void)label;
}

void Probe_Finish(s32 id) {
    (void)id;
}

void StreamMain(void) {}

s16* MixDsp(s32 n_samples) {
    if (n_samples != JAC_FRAMESAMPLES) {
        rsp_error = 1;
    }
    return synthetic_dsp_track;
}

static void make_rsp_frame(void) {
    int command_index = 0;

    memset(rsp_commands, 0, sizeof(rsp_commands));
    memset(neos_pcm, 0, sizeof(neos_pcm));

    for (int chunk = 0; chunk < RSP_CHUNKS; ++chunk) {
        for (int sample = 0; sample < RSP_INPUT_SAMPLES; ++sample) {
            rsp_left[chunk * RSP_INPUT_SAMPLES + sample] =
                (s16)(12000 + chunk * 700 + sample * 13);
            rsp_right[chunk * RSP_INPUT_SAMPLES + sample] =
                (s16)(-18000 + chunk * 500 - sample * 11);
        }

        memset(rsp_output[chunk], 0, sizeof(rsp_output[chunk]));

        aLoadBuffer2(&rsp_commands[command_index++],
                     &rsp_left[chunk * RSP_INPUT_SAMPLES], 0x000,
                     RSP_INPUT_SAMPLES * sizeof(s16));
        aLoadBuffer2(&rsp_commands[command_index++],
                     &rsp_right[chunk * RSP_INPUT_SAMPLES], 0x200,
                     RSP_INPUT_SAMPLES * sizeof(s16));
        aInterleave2(&rsp_commands[command_index++], 0x400, 0x000, 0x200,
                     RSP_INPUT_SAMPLES * sizeof(s16));
        aSaveBuffer2(&rsp_commands[command_index++], rsp_output[chunk],
                     0x400, RSP_OUTPUT_SAMPLES * sizeof(s16));
    }

    last_rsp_consumed = RspStart((u32*)rsp_commands, RSP_COMMANDS);
    if (last_rsp_consumed != RSP_COMMANDS) {
        rsp_error = 1;
        return;
    }

    for (int chunk = 0; chunk < RSP_CHUNKS; ++chunk) {
        memcpy(&neos_pcm[chunk * RSP_OUTPUT_SAMPLES], rsp_output[chunk],
               sizeof(rsp_output[chunk]));
    }
}

u32 Neos_Update(s16* dst) {
    if (neos_update_count == 0) {
        make_rsp_frame();
        if (rsp_error != 0) {
            return FALSE;
        }
        memcpy(first_neos_pcm, neos_pcm, sizeof(first_neos_pcm));
    } else {
        /* Keep the ring assertion focused on transport: the real RSP
         * resampler carries phase/history across tasks, so repeat the first
         * generated block after the one-shot provenance check. */
        memcpy(neos_pcm, first_neos_pcm, sizeof(neos_pcm));
    }
    memcpy(dst, neos_pcm, sizeof(neos_pcm));
    ++neos_update_count;
    return TRUE;
}

static int count_nonzero(const s16* samples) {
    int count = 0;

    for (int i = 0; i < DAC_SAMPLES; ++i) {
        if (samples[i] != 0) {
            ++count;
        }
    }

    return count;
}

static int fail(const char* message) {
    fprintf(stderr, "pc_audio NEOS PCM probe: %s\n", message);
    return 1;
}

int main(void) {
    s16 callback_output[DAC_SAMPLES];

    if (audio_device != 0) {
        return fail("device was opened before the proof started");
    }

    memset(synthetic_dsp_track, 0, sizeof(synthetic_dsp_track));
    CpubufProcess(DSPBUF_EVENT_INIT);

    for (int i = 0; i < 3; ++i) {
        dac[i] = (s16*)calloc(DAC_SAMPLES, sizeof(s16));
        if (dac[i] == NULL) {
            return fail("DAC allocation failed");
        }
    }

    /* Cpubuf's initial read/write indices intentionally prebuffer the ring.
     * Advance it through the real MIX path until the first generated block
     * becomes readable; each MIX also drives its PC FRAME_END publication. */
    CpubufProcess(DSPBUF_EVENT_MIX);
    CpubufProcess(DSPBUF_EVENT_MIX);
    s16* first_ring_block = CpubufProcess(DSPBUF_EVENT_MIX);

    if (rsp_error != 0 || neos_update_count != 3) {
        fprintf(stderr, "RSP fixture state: error=%d consumed=%d updates=%u\n",
                rsp_error, last_rsp_consumed, neos_update_count);
        return fail("RSP fixture did not publish its first NEOS frame");
    }
    if (count_nonzero(first_neos_pcm) < 100) {
        return fail("RSP fixture produced only zero PCM");
    }
    if (memcmp(first_ring_block, first_neos_pcm, sizeof(first_neos_pcm)) != 0) {
        return fail("Cpubuf did not expose the first RSP-generated block");
    }

    Jac_RegisterMixcallback(MixCpu, MixMode_Interleave);
    Jac_VframeWork();

    if (rsp_error != 0 || neos_update_count < 4) {
        return fail("Cpubuf mix did not advance the NEOS producer");
    }
    if (last_rsp_madep != dac[0]) {
        return fail("Jac_VframeWork did not publish a DAC block");
    }
    if (memcmp(dac[0], first_neos_pcm, sizeof(first_neos_pcm)) != 0) {
        return fail("RSP PCM was changed between Cpubuf and Jac_VframeWork");
    }

    /* Jac_UpdateDAC calls the real TARGET_PC AIInitDMA handoff for the block
     * published above.  No SDL device is initialized by this call. */
    Jac_UpdateDAC();
    if (pc_audio_get_buffer_fill() != DAC_SAMPLES) {
        return fail("Jac_UpdateDAC did not enqueue the complete NEOS block");
    }

    memset(callback_output, 0x5A, sizeof(callback_output));
    pc_audio_callback(NULL, (Uint8*)callback_output, sizeof(callback_output));
    if (pc_audio_get_buffer_fill() != 0) {
        return fail("callback did not consume the complete NEOS block");
    }
    if (memcmp(callback_output, first_neos_pcm, sizeof(first_neos_pcm)) != 0) {
        return fail("callback PCM differed from the RSP-generated PCM");
    }

    for (int i = 0; i < 3; ++i) {
        free(dac[i]);
        dac[i] = NULL;
    }

    printf("pc_audio NEOS PCM probe: PASS (RSP nonzero=%d; RSP -> Cpubuf -> DAC -> callback; device not opened)\n",
           count_nonzero(first_neos_pcm));
    return 0;
}
