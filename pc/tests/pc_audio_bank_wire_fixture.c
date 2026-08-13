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

    puts("pc_audio_bank_wire_fixture: PASS");
    puts("invariant: high native address is only used as the wire-base pointer;");
    puts("fixed-width offsets are decoded with bounds checks and malformed ranges fail closed");
    return 0;
#endif
}
