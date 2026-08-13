#include "pc_audio_bank.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static void write_be32(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

int main(void) {
#if UINTPTR_MAX <= UINT32_MAX
    puts("pc_audio_bank_wire_fixture: SKIP (requires a wider-than-u32 uintptr_t)");
    return 77;
#else
    uint8_t wire[0x80];
    uint32_t value = 0;
    PcAudioBankDecodeResult result;
    WaveMedia wave_media;

    memset(wire, 0, sizeof(wire));
    memset(&result, 0, sizeof(result));
    memset(&wave_media, 0, sizeof(wave_media));
    CHECK((uintptr_t)wire > (uintptr_t)UINT32_MAX);

    /* A control block may be high in native memory while its offsets stay u32. */
    write_be32(wire + 0x00, 0x00000020);
    write_be32(wire + 0x04, 0x00000000);
    write_be32(wire + 0x08, 0x00000030);
    CHECK(pc_audio_bank_wire_read_u32(wire, sizeof(wire), 0x00, &value));
    CHECK(value == 0x20);
    CHECK(pc_audio_bank_wire_read_u32(wire, sizeof(wire), 0x08, &value));
    CHECK(value == 0x30);

    /* Zero instruments/effects is a valid empty bank and must not follow pointers. */
    CHECK(pc_audio_bank_decode_lp64(wire, sizeof(wire), 0, 0, 0,
                                    &wave_media, &result));
    CHECK(result.instruments == NULL);
    CHECK(result.percussion == NULL);
    CHECK(result.effects == NULL);
    CHECK(result.used_sample_count == 0);
    free(result.used_samples);

    CHECK(!pc_audio_bank_wire_read_u32(wire, sizeof(wire), 0x7D, &value));
    CHECK(!pc_audio_bank_decode_lp64(wire, 0x0B, 1, 0, 0,
                                     &wave_media, &result));

    /*
     * Compact banks may stop before the final null table entry. A zero
     * partial tail is implicit, while a nonzero partial word remains invalid.
     */
    memset(wire, 0, sizeof(wire));
    write_be32(wire + 0x00, 0x00000070);
    CHECK(pc_audio_bank_decode_lp64(wire, 0x73, 0, 2, 0,
                                    &wave_media, &result));
    CHECK(result.percussion != NULL);
    CHECK(result.percussion[0] == NULL);
    CHECK(result.percussion[1] == NULL);
    free(result.percussion);
    result.percussion = NULL;

    wire[0x72] = 0x01;
    CHECK(!pc_audio_bank_decode_lp64(wire, 0x73, 0, 2, 0,
                                     &wave_media, &result));

    /*
     * A wire MEDIUM_RAM wave uses the WaveMedia base for the source slot even
     * when the backing wave bank is declared as cart media.
     */
    memset(wire, 0, sizeof(wire));
    memset(&wave_media, 0, sizeof(wave_media));
    write_be32(wire + 0x00, 0x00000000);
    write_be32(wire + 0x08, 0x00000020);
    write_be32(wire + 0x20, 0x00000000);
    write_be32(wire + 0x30, 0x00000040);
    write_be32(wire + 0x40, 0x00000100);
    write_be32(wire + 0x44, 0x00000100);
    wave_media.wave0_media = MEDIUM_CART;
    wave_media.wave0_p = (void*)(uintptr_t)0x100000000ULL;
    CHECK(pc_audio_bank_decode_lp64(wire, sizeof(wire), 1, 0, 0,
                                    &wave_media, &result));
    CHECK(result.instruments != NULL);
    CHECK(result.instruments[0] != NULL);
    CHECK(result.instruments[0]->normal_pitch_tuned_sample.wavetable != NULL);
    CHECK((uintptr_t)result.instruments[0]->normal_pitch_tuned_sample.wavetable->sample ==
          (uintptr_t)0x100000100ULL);
    free(result.instruments);

    puts("pc_audio_bank_wire_fixture: PASS");
    puts("invariant: high native address is only used as the wire-base pointer;");
    puts("fixed-width offsets are decoded with bounds checks and malformed ranges fail closed");
    return 0;
#endif
}
