#include "pc_audio_bank.h"

#include "pc_bswap.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct PcAudioBankMapEntry {
    uint32_t offset;
    void* value;
} PcAudioBankMapEntry;

typedef struct PcAudioBankMap {
    PcAudioBankMapEntry* entries;
    size_t count;
    size_t capacity;
} PcAudioBankMap;

typedef struct PcAudioBankContext {
    const uint8_t* base;
    size_t size;
    const WaveMedia* wave_media;
    PcAudioBankDecodeResult* result;
    PcAudioBankMap envdat;
    PcAudioBankMap loops;
    PcAudioBankMap books;
    PcAudioBankMap waves;
    PcAudioBankMap voices;
    PcAudioBankMap percussion;
} PcAudioBankContext;

static const uint8_t* pc_audio_bank_wire_at(const PcAudioBankContext* context,
                                            uint32_t offset, size_t length) {
    if (context == NULL || context->base == NULL ||
        (size_t)offset > context->size || length > context->size - (size_t)offset) {
        return NULL;
    }
    return context->base + offset;
}

static int pc_audio_bank_read_u16(const PcAudioBankContext* context,
                                  uint32_t offset, uint16_t* value_out) {
    const uint8_t* wire = pc_audio_bank_wire_at(context, offset, sizeof(uint16_t));
    uint16_t value;

    if (wire == NULL || value_out == NULL) {
        return 0;
    }
    memcpy(&value, wire, sizeof(value));
    *value_out = pc_bswap16(value);
    return 1;
}

int pc_audio_bank_wire_read_u32(const uint8_t* base, size_t size,
                                uint32_t offset, uint32_t* value_out) {
    uint32_t value;

    if (base == NULL || value_out == NULL || (size_t)offset > size ||
        sizeof(value) > size - (size_t)offset) {
        return 0;
    }
    memcpy(&value, base + offset, sizeof(value));
    *value_out = pc_bswap32(value);
    return 1;
}

static int pc_audio_bank_read_u32(const PcAudioBankContext* context,
                                  uint32_t offset, uint32_t* value_out) {
    return pc_audio_bank_wire_read_u32(context->base, context->size, offset, value_out);
}

static int pc_audio_bank_read_f32(const PcAudioBankContext* context,
                                  uint32_t offset, f32* value_out) {
    uint32_t bits;

    if (value_out == NULL || !pc_audio_bank_read_u32(context, offset, &bits)) {
        return 0;
    }
    memcpy(value_out, &bits, sizeof(*value_out));
    return 1;
}

static void* pc_audio_bank_alloc(size_t size) {
    return calloc(1, size);
}

static void pc_audio_bank_map_reset(PcAudioBankMap* map) {
    if (map == NULL) {
        return;
    }
    free(map->entries);
    memset(map, 0, sizeof(*map));
}

static void pc_audio_bank_maps_reset(PcAudioBankContext* context) {
    if (context == NULL) {
        return;
    }
    pc_audio_bank_map_reset(&context->envdat);
    pc_audio_bank_map_reset(&context->loops);
    pc_audio_bank_map_reset(&context->books);
    pc_audio_bank_map_reset(&context->waves);
    pc_audio_bank_map_reset(&context->voices);
    pc_audio_bank_map_reset(&context->percussion);
}

static void* pc_audio_bank_map_find(const PcAudioBankMap* map, uint32_t offset) {
    size_t i;

    if (map == NULL || offset == 0) {
        return NULL;
    }
    for (i = 0; i < map->count; i++) {
        if (map->entries[i].offset == offset) {
            return map->entries[i].value;
        }
    }
    return NULL;
}

static int pc_audio_bank_map_add(PcAudioBankMap* map, uint32_t offset, void* value) {
    PcAudioBankMapEntry* entries;
    size_t capacity;

    if (map == NULL || offset == 0 || value == NULL) {
        return 0;
    }
    if (map->count == map->capacity) {
        capacity = map->capacity == 0 ? 16 : map->capacity * 2;
        if (capacity < map->capacity || capacity > SIZE_MAX / sizeof(*entries)) {
            return 0;
        }
        entries = (PcAudioBankMapEntry*)realloc(map->entries, capacity * sizeof(*entries));
        if (entries == NULL) {
            return 0;
        }
        map->entries = entries;
        map->capacity = capacity;
    }
    map->entries[map->count].offset = offset;
    map->entries[map->count].value = value;
    map->count++;
    return 1;
}

static int pc_audio_bank_mul_size(size_t left, size_t right, size_t* result_out) {
    if (result_out == NULL || (right != 0 && left > SIZE_MAX / right)) {
        return 0;
    }
    *result_out = left * right;
    return 1;
}

static int pc_audio_bank_add_size(size_t left, size_t right, size_t* result_out) {
    if (result_out == NULL || left > SIZE_MAX - right) {
        return 0;
    }
    *result_out = left + right;
    return 1;
}

static envdat* pc_audio_bank_copy_envdat(PcAudioBankContext* context, uint32_t offset) {
    envdat* env;
    uint16_t raw_delay;
    uint16_t raw_value;
    size_t count = 0;
    size_t bytes;
    size_t i;

    if (offset == 0) {
        return NULL;
    }
    env = (envdat*)pc_audio_bank_map_find(&context->envdat, offset);
    if (env != NULL) {
        return env;
    }

    /* The original engine bounds envelope scans at 64 entries. */
    for (i = 0; i <= 64; i++) {
        uint32_t entry_offset;
        if (!pc_audio_bank_mul_size(i, sizeof(envdat), &bytes) ||
            !pc_audio_bank_add_size((size_t)offset, bytes, &bytes) ||
            bytes > UINT32_MAX) {
            return NULL;
        }
        entry_offset = (uint32_t)bytes;
        if (!pc_audio_bank_read_u16(context, entry_offset, &raw_delay) ||
            !pc_audio_bank_read_u16(context, entry_offset + 2, &raw_value)) {
            return NULL;
        }
        (void)raw_value;
        count++;
        if ((int16_t)raw_delay == 0 || (int16_t)raw_delay == -1 ||
            (int16_t)raw_delay == -2 || (int16_t)raw_delay == -3) {
            break;
        }
        if (i == 64) {
            return NULL;
        }
    }

    if (!pc_audio_bank_mul_size(count, sizeof(*env), &bytes)) {
        return NULL;
    }
    env = (envdat*)pc_audio_bank_alloc(bytes);
    if (env == NULL || !pc_audio_bank_map_add(&context->envdat, offset, env)) {
        free(env);
        return NULL;
    }
    for (i = 0; i < count; i++) {
        uint32_t entry_offset = offset + (uint32_t)(i * sizeof(envdat));
        if (!pc_audio_bank_read_u16(context, entry_offset, &raw_delay) ||
            !pc_audio_bank_read_u16(context, entry_offset + 2, &raw_value)) {
            return NULL;
        }
        env[i].delay = (s16)raw_delay;
        env[i].value = (s16)raw_value;
    }
    return env;
}

static adpcmloop* pc_audio_bank_copy_loop(PcAudioBankContext* context, uint32_t offset) {
    adpcmloop* loop;
    uint32_t count;
    size_t bytes;
    size_t i;

    if (offset == 0) {
        return NULL;
    }
    loop = (adpcmloop*)pc_audio_bank_map_find(&context->loops, offset);
    if (loop != NULL) {
        return loop;
    }
    if (!pc_audio_bank_read_u32(context, offset + 0x00, &count)) {
        return NULL;
    }
    bytes = count != 0 ? sizeof(adpcmloop) : 0x10;
    loop = (adpcmloop*)pc_audio_bank_alloc(bytes);
    if (loop == NULL || !pc_audio_bank_map_add(&context->loops, offset, loop)) {
        free(loop);
        return NULL;
    }
    if (!pc_audio_bank_read_u32(context, offset + 0x00, &loop->loop_start) ||
        !pc_audio_bank_read_u32(context, offset + 0x04, &loop->loop_end) ||
        !pc_audio_bank_read_u32(context, offset + 0x08, &loop->count) ||
        !pc_audio_bank_read_u32(context, offset + 0x0C, &loop->sample_end)) {
        return NULL;
    }
    for (i = 0; i < 16 && count != 0; i++) {
        uint16_t value;
        if (!pc_audio_bank_read_u16(context, offset + 0x10 + (uint32_t)(i * 2), &value)) {
            return NULL;
        }
        loop->predictor_state[i] = (s16)value;
    }
    return loop;
}

static adpcmbook* pc_audio_bank_copy_book(PcAudioBankContext* context, uint32_t offset) {
    adpcmbook* book;
    uint32_t order;
    uint32_t predictors;
    size_t entries;
    size_t codebook_bytes;
    size_t bytes;
    size_t i;

    if (offset == 0) {
        return NULL;
    }
    book = (adpcmbook*)pc_audio_bank_map_find(&context->books, offset);
    if (book != NULL) {
        return book;
    }
    if (!pc_audio_bank_read_u32(context, offset + 0x00, &order) ||
        !pc_audio_bank_read_u32(context, offset + 0x04, &predictors) ||
        order == 0 || order > 32 || predictors == 0 || predictors > 32 ||
        !pc_audio_bank_mul_size(order, predictors, &entries) ||
        !pc_audio_bank_mul_size(entries, sizeof(s16) * 8, &codebook_bytes) ||
        !pc_audio_bank_add_size(offsetof(adpcmbook, codebook), codebook_bytes, &bytes)) {
        return NULL;
    }
    book = (adpcmbook*)pc_audio_bank_alloc(bytes);
    if (book == NULL || !pc_audio_bank_map_add(&context->books, offset, book)) {
        free(book);
        return NULL;
    }
    book->order = (s32)order;
    book->n_predictors = (s32)predictors;
    for (i = 0; i < entries * 8; i++) {
        uint16_t value;
        if (!pc_audio_bank_read_u16(context, offset + 0x08 + (uint32_t)(i * 2), &value)) {
            return NULL;
        }
        book->codebook[i] = (s16)value;
    }
    return book;
}

static uintptr_t pc_audio_bank_sample_base(const PcAudioBankContext* context,
                                           uint32_t medium) {
    if (context == NULL || context->wave_media == NULL) {
        return 0;
    }
    switch (medium) {
        case MEDIUM_RAM:
            if (context->wave_media->wave0_media == MEDIUM_RAM) {
                return (uintptr_t)context->wave_media->wave0_p;
            }
            break;
        case MEDIUM_DISK:
            if (context->wave_media->wave1_media == MEDIUM_DISK) {
                return (uintptr_t)context->wave_media->wave1_p;
            }
            break;
        default:
            break;
    }
    return 0;
}

static smzwavetable* pc_audio_bank_copy_wave(PcAudioBankContext* context,
                                              uint32_t offset) {
    smzwavetable* wave;
    smzwavetable flags_value;
    uint32_t flags;
    uint32_t sample_offset;
    uint32_t loop_offset;
    uint32_t book_offset;
    uintptr_t sample_base;

    if (offset == 0) {
        return NULL;
    }
    wave = (smzwavetable*)pc_audio_bank_map_find(&context->waves, offset);
    if (wave != NULL) {
        return wave;
    }
    if (!pc_audio_bank_read_u32(context, offset + 0x00, &flags) ||
        !pc_audio_bank_read_u32(context, offset + 0x04, &sample_offset) ||
        !pc_audio_bank_read_u32(context, offset + 0x08, &loop_offset) ||
        !pc_audio_bank_read_u32(context, offset + 0x0C, &book_offset)) {
        return NULL;
    }

    wave = (smzwavetable*)pc_audio_bank_alloc(sizeof(*wave));
    if (wave == NULL || !pc_audio_bank_map_add(&context->waves, offset, wave)) {
        free(wave);
        return NULL;
    }

    memset(&flags_value, 0, sizeof(flags_value));
    memcpy(&flags_value, &flags, sizeof(flags));
    wave->size = flags_value.size;
    wave->is_relocated = flags_value.is_relocated;
    wave->bit26 = flags_value.bit26;
    wave->medium = flags_value.medium;
    wave->codec = flags_value.codec;
    wave->bit31 = flags_value.bit31;
    wave->loop = pc_audio_bank_copy_loop(context, loop_offset);
    wave->book = pc_audio_bank_copy_book(context, book_offset);
    sample_base = pc_audio_bank_sample_base(context, wave->medium);
    wave->sample = (u8*)(sample_base != 0 ? sample_base + sample_offset :
                         (uintptr_t)sample_offset);
    return wave;
}

static wtstr pc_audio_bank_copy_wtstr(PcAudioBankContext* context,
                                      uint32_t offset) {
    wtstr value;
    uint32_t wave_offset;

    memset(&value, 0, sizeof(value));
    if (pc_audio_bank_read_u32(context, offset + 0x00, &wave_offset) &&
        pc_audio_bank_read_f32(context, offset + 0x04, &value.tuning)) {
        value.wavetable = pc_audio_bank_copy_wave(context, wave_offset);
    }
    return value;
}

static voicetable* pc_audio_bank_copy_voice(PcAudioBankContext* context,
                                            uint32_t offset) {
    voicetable* voice;
    uint32_t envelope_offset;

    if (offset == 0) {
        return NULL;
    }
    voice = (voicetable*)pc_audio_bank_map_find(&context->voices, offset);
    if (voice != NULL) {
        return voice;
    }
    if (pc_audio_bank_wire_at(context, offset, 0x20) == NULL) {
        return NULL;
    }
    voice = (voicetable*)pc_audio_bank_alloc(sizeof(*voice));
    if (voice == NULL || !pc_audio_bank_map_add(&context->voices, offset, voice)) {
        free(voice);
        return NULL;
    }
    voice->is_relocated = context->base[offset + 0x00];
    voice->normal_range_low = context->base[offset + 0x01];
    voice->normal_range_high = context->base[offset + 0x02];
    voice->adsr_decay_idx = context->base[offset + 0x03];
    if (!pc_audio_bank_read_u32(context, offset + 0x04, &envelope_offset)) {
        return NULL;
    }
    voice->envelope = pc_audio_bank_copy_envdat(context, envelope_offset);
    voice->low_pitch_tuned_sample = pc_audio_bank_copy_wtstr(context, offset + 0x08);
    voice->normal_pitch_tuned_sample = pc_audio_bank_copy_wtstr(context, offset + 0x10);
    voice->high_pitch_tuned_sample = pc_audio_bank_copy_wtstr(context, offset + 0x18);
    return voice;
}

static perctable* pc_audio_bank_copy_percussion(PcAudioBankContext* context,
                                                uint32_t offset) {
    perctable* percussion;
    uint32_t envelope_offset;

    if (offset == 0) {
        return NULL;
    }
    percussion = (perctable*)pc_audio_bank_map_find(&context->percussion, offset);
    if (percussion != NULL) {
        return percussion;
    }
    if (pc_audio_bank_wire_at(context, offset, 0x10) == NULL) {
        return NULL;
    }
    percussion = (perctable*)pc_audio_bank_alloc(sizeof(*percussion));
    if (percussion == NULL || !pc_audio_bank_map_add(&context->percussion, offset, percussion)) {
        free(percussion);
        return NULL;
    }
    percussion->adsr_decay_idx = context->base[offset + 0x00];
    percussion->pan = context->base[offset + 0x01];
    percussion->is_relocated = context->base[offset + 0x02];
    percussion->tuned_sample = pc_audio_bank_copy_wtstr(context, offset + 0x04);
    if (!pc_audio_bank_read_u32(context, offset + 0x0C, &envelope_offset)) {
        return NULL;
    }
    percussion->envelope = pc_audio_bank_copy_envdat(context, envelope_offset);
    return percussion;
}

static percvoicetable pc_audio_bank_copy_effect(PcAudioBankContext* context,
                                                uint32_t offset) {
    percvoicetable effect;

    memset(&effect, 0, sizeof(effect));
    effect.tuned_sample = pc_audio_bank_copy_wtstr(context, offset);
    return effect;
}

static int pc_audio_bank_add_used_sample(PcAudioBankDecodeResult* result,
                                         smzwavetable* wave) {
    smzwavetable** samples;
    size_t capacity;
    size_t i;

    if (result == NULL || wave == NULL || wave->size == 0 || !wave->bit26 ||
        wave->medium == MEDIUM_RAM) {
        return 1;
    }
    for (i = 0; i < result->used_sample_count; i++) {
        if (result->used_samples[i] == wave) {
            return 1;
        }
    }
    capacity = result->used_sample_count == 0 ? 16 : result->used_sample_count * 2;
    if (capacity < result->used_sample_count || capacity > SIZE_MAX / sizeof(*samples)) {
        return 0;
    }
    samples = (smzwavetable**)realloc(result->used_samples, capacity * sizeof(*samples));
    if (samples == NULL) {
        return 0;
    }
    result->used_samples = samples;
    result->used_samples[result->used_sample_count++] = wave;
    return 1;
}

static int pc_audio_bank_add_voice_samples(PcAudioBankDecodeResult* result,
                                            voicetable* voice) {
    if (voice == NULL) {
        return 1;
    }
    return pc_audio_bank_add_used_sample(result, voice->low_pitch_tuned_sample.wavetable) &&
           pc_audio_bank_add_used_sample(result, voice->normal_pitch_tuned_sample.wavetable) &&
           pc_audio_bank_add_used_sample(result, voice->high_pitch_tuned_sample.wavetable);
}

int pc_audio_bank_decode_lp64(const uint8_t* base, size_t size,
                              int instrument_count, int percussion_count,
                              int effect_count, const WaveMedia* wave_media,
                              PcAudioBankDecodeResult* result_out) {
#if UINTPTR_MAX <= UINT32_MAX
    (void)base;
    (void)size;
    (void)instrument_count;
    (void)percussion_count;
    (void)effect_count;
    (void)wave_media;
    (void)result_out;
    return 0;
#else
    PcAudioBankContext context;
    size_t control_entries;
    size_t control_bytes;
    size_t i;

    if (base == NULL || result_out == NULL ||
        instrument_count < 0 || instrument_count > 126 ||
        percussion_count < 0 || percussion_count > 128 ||
        effect_count < 0 || effect_count > 256) {
        return 0;
    }
    memset(result_out, 0, sizeof(*result_out));
    memset(&context, 0, sizeof(context));
    context.base = base;
    context.size = size;
    context.wave_media = wave_media;
    context.result = result_out;

    control_entries = (size_t)instrument_count + 2;
    if (!pc_audio_bank_mul_size(control_entries, sizeof(uint32_t), &control_bytes) ||
        pc_audio_bank_wire_at(&context, 0, control_bytes) == NULL) {
        return 0;
    }

    if (instrument_count != 0) {
        result_out->instruments = (voicetable**)pc_audio_bank_alloc(
            (size_t)instrument_count * sizeof(*result_out->instruments));
        if (result_out->instruments == NULL) {
            pc_audio_bank_maps_reset(&context);
            return 0;
        }
    }
    for (i = 0; i < (size_t)instrument_count; i++) {
        uint32_t offset;
        if (!pc_audio_bank_read_u32(&context, (uint32_t)((i + 2) * 4), &offset)) {
            pc_audio_bank_maps_reset(&context);
            return 0;
        }
        result_out->instruments[i] = pc_audio_bank_copy_voice(&context, offset);
        if (offset != 0 && result_out->instruments[i] == NULL) {
            pc_audio_bank_maps_reset(&context);
            return 0;
        }
        if (!pc_audio_bank_add_voice_samples(result_out, result_out->instruments[i])) {
            pc_audio_bank_maps_reset(&context);
            return 0;
        }
    }

    if (percussion_count != 0) {
        uint32_t table_offset;
        result_out->percussion = (perctable**)pc_audio_bank_alloc(
            (size_t)percussion_count * sizeof(*result_out->percussion));
        if (result_out->percussion == NULL ||
            !pc_audio_bank_read_u32(&context, 0, &table_offset) || table_offset == 0) {
            pc_audio_bank_maps_reset(&context);
            return 0;
        }
        for (i = 0; i < (size_t)percussion_count; i++) {
            uint32_t offset;
            if (!pc_audio_bank_read_u32(&context, table_offset + (uint32_t)(i * 4), &offset)) {
                pc_audio_bank_maps_reset(&context);
                return 0;
            }
            result_out->percussion[i] = pc_audio_bank_copy_percussion(&context, offset);
            if (offset != 0 && result_out->percussion[i] == NULL) {
                pc_audio_bank_maps_reset(&context);
                return 0;
            }
            if (result_out->percussion[i] != NULL &&
                !pc_audio_bank_add_used_sample(result_out,
                    result_out->percussion[i]->tuned_sample.wavetable)) {
                pc_audio_bank_maps_reset(&context);
                return 0;
            }
        }
    }

    if (effect_count != 0) {
        uint32_t table_offset;
        result_out->effects = (percvoicetable*)pc_audio_bank_alloc(
            (size_t)effect_count * sizeof(*result_out->effects));
        if (result_out->effects == NULL ||
            !pc_audio_bank_read_u32(&context, 4, &table_offset) || table_offset == 0 ||
            pc_audio_bank_wire_at(&context, table_offset, (size_t)effect_count * 8) == NULL) {
            pc_audio_bank_maps_reset(&context);
            return 0;
        }
        for (i = 0; i < (size_t)effect_count; i++) {
            result_out->effects[i] = pc_audio_bank_copy_effect(
                &context, table_offset + (uint32_t)(i * 8));
            if (!pc_audio_bank_add_used_sample(result_out,
                    result_out->effects[i].tuned_sample.wavetable)) {
                pc_audio_bank_maps_reset(&context);
                return 0;
            }
        }
    }

    pc_audio_bank_maps_reset(&context);
    return 1;
#endif
}
