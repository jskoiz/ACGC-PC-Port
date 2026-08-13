/* Silent SDL/CoreAudio boundary probe.
 *
 * This intentionally does not load game data or run the mixer.  It uses a
 * producer stub to keep the existing SPSC ring supplied with silence, then
 * measures the host audio callback cadence and verifies that the adapter does
 * not underrun under ordinary device load.  It is evidence for device-open,
 * callback scheduling, and ring behavior only; it says nothing about audible
 * mixer correctness.
 */
#include <stdint.h>
#include <stdio.h>

#include "pc_settings.h"
#include <dolphin/ai.h>

PCSettings g_pc_settings = {
    .master_volume = 0,
};

static s16 probe_samples[1120];

void pc_audio_process_frame(void) {
    AIInitDMA((AINativeAddress)(uintptr_t)probe_samples, sizeof(probe_samples));
}

#ifndef ACGC_AUDIO_DIAGNOSTICS
#define ACGC_AUDIO_DIAGNOSTICS
#endif
#include "../src/pc_audio.c"

static int probe_fail(const char* message) {
    fprintf(stderr, "pc_audio device probe: %s\n", message);
    return 1;
}

int main(void) {
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "pc_audio device probe: SDL_Init failed: %s\n", SDL_GetError());
        return 77;
    }

    AIInit(NULL);
    if (audio_device == 0) {
        fprintf(stderr, "pc_audio device probe: SDL audio device unavailable: %s\n", SDL_GetError());
        SDL_Quit();
        return 77;
    }

    pc_audio_start_producer_thread();
    const Uint32 fill_deadline = SDL_GetTicks() + 1000;
    while (pc_audio_get_buffer_fill() < AUDIO_PRODUCE_THRESHOLD
           && SDL_GetTicks() < fill_deadline) {
        SDL_Delay(1);
    }
    if (pc_audio_get_buffer_fill() < AUDIO_PRODUCE_THRESHOLD) {
        pc_audio_shutdown();
        SDL_Quit();
        return probe_fail("producer did not prefill the ring");
    }

    AIStartDMA();
    SDL_Delay(1000);
    AIStopDMA();
    pc_audio_shutdown();

    const double tick_frequency = (double)SDL_GetPerformanceFrequency();
    const double elapsed_seconds = audio_callback_count > 1
        ? (double)(audio_callback_last_ticks - audio_callback_first_ticks) / tick_frequency
        : 0.0;
    const double measured_frame_rate = elapsed_seconds > 0.0
        ? (double)audio_callback_samples / 2.0 / elapsed_seconds
        : 0.0;
    const double expected_rate = (double)audio_device_freq;

    printf("[AUDIO_PROBE] opened freq=%d fmt=0x%04X channels=%d buffer_samples=%d callbacks=%llu "
           "samples=%llu measured_frame_rate=%.1f underruns=%llu underrun_samples=%llu "
           "overruns=%llu fill_min=%u fill_max=%u max_gap_ms=%.2f\n",
           audio_device_freq, audio_device_format, audio_device_channels, audio_device_samples,
           (unsigned long long)audio_callback_count,
           (unsigned long long)audio_callback_samples,
           measured_frame_rate,
           (unsigned long long)audio_callback_underrun_count,
           (unsigned long long)audio_callback_underrun_samples,
           (unsigned long long)audio_callback_overrun_count,
           audio_callback_min_fill, audio_callback_max_fill,
           audio_callback_max_gap_ticks * 1000.0 / tick_frequency);

    SDL_Quit();

    if (audio_callback_count < 10) {
        return probe_fail("too few callbacks to establish cadence");
    }
    if (audio_device_freq <= 0 || audio_device_samples <= 0
        || audio_device_format != AUDIO_S16SYS || audio_device_channels != 2) {
        return probe_fail("SDL returned invalid device format");
    }
    if (measured_frame_rate < expected_rate * 0.75 || measured_frame_rate > expected_rate * 1.25) {
        return probe_fail("callback sample cadence differs from the opened device rate");
    }
    if (audio_callback_underrun_count != 0) {
        return probe_fail("ring underrun observed while the silent producer was running");
    }
    if (audio_callback_overrun_count != 0) {
        return probe_fail("ring overrun observed while the silent producer was running");
    }

    puts("pc_audio device probe: PASS");
    return 0;
}
