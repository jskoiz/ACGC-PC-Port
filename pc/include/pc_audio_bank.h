#ifndef ACGC_PC_AUDIO_BANK_H
#define ACGC_PC_AUDIO_BANK_H

#include <stddef.h>
#include <stdint.h>

#include "jaudio_NES/audiostruct.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A decoded bank owns native host-side records.  The source blob remains a
 * fixed-width GameCube wire image and is never treated as a C struct on LP64.
 * The allocations intentionally live for the lifetime of the loaded bank;
 * the existing PC audio cache has no bank-object destructor yet.
 */
typedef struct PcAudioBankDecodeResult {
    voicetable** instruments;
    perctable** percussion;
    percvoicetable* effects;
    smzwavetable** used_samples;
    size_t used_sample_count;
} PcAudioBankDecodeResult;

/* Read a big-endian u32 from a bounded wire blob. */
int pc_audio_bank_wire_read_u32(const uint8_t* base, size_t size,
                                uint32_t offset, uint32_t* value_out);

/* Decode a GameCube bank control block on a host wider than 32 bits. */
int pc_audio_bank_decode_lp64(const uint8_t* base, size_t size,
                              int instrument_count, int percussion_count,
                              int effect_count, const WaveMedia* wave_media,
                              PcAudioBankDecodeResult* result_out);

#ifdef __cplusplus
}
#endif

#endif
