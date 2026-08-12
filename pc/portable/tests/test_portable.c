#include "acgc/address.h"
#include "acgc/bytes.h"
#include "acgc/gbi_reference_registry.h"
#include "acgc/disc.h"
#include "acgc/yaz0.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_OUTPUT_LIMIT UINT32_MAX
#define ADDRESS_CONST(value) ((uintptr_t)(value))

_Static_assert(sizeof(uint8_t) == 1, "uint8_t must be one byte");
_Static_assert(sizeof(uint16_t) == 2, "uint16_t must be two bytes");
_Static_assert(sizeof(uint32_t) == 4, "uint32_t must be four bytes");
_Static_assert(sizeof(uint64_t) == 8, "uint64_t must be eight bytes");
_Static_assert(sizeof(uintptr_t) >= sizeof(void*), "uintptr_t must hold a pointer");
_Static_assert(offsetof(AcgcAddressRange, begin) == 0, "range begins with begin");
_Static_assert(
    offsetof(AcgcAddressRange, end) == sizeof(uintptr_t),
    "range end follows begin"
);
_Static_assert(
    sizeof(AcgcAddressRange) == sizeof(uintptr_t) * 2,
    "range has no unexpected padding"
);
#define SYNTHETIC_IMAGE_SIZE 0x1000u
#define SYNTHETIC_DOL_OFFSET 0x500u
#define SYNTHETIC_FST_OFFSET 0x700u
#define SYNTHETIC_FST_SIZE 0x80u
#define SYNTHETIC_RAW_REL_OFFSET 0x800u
#define SYNTHETIC_YAZ0_REL_OFFSET 0x900u

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct SyntheticImage {
    uint8_t bytes[SYNTHETIC_IMAGE_SIZE];
    size_t size;
    int short_read;
} SyntheticImage;

static void store_be32(uint8_t* destination, uint32_t value) {
    destination[0] = (uint8_t)(value >> 24);
    destination[1] = (uint8_t)(value >> 16);
    destination[2] = (uint8_t)(value >> 8);
    destination[3] = (uint8_t)value;
}

static void store_fst_entry(
    uint8_t* entry,
    uint8_t type,
    uint32_t name_offset,
    uint32_t offset,
    uint32_t size
) {
    entry[0] = type;
    entry[1] = (uint8_t)(name_offset >> 16);
    entry[2] = (uint8_t)(name_offset >> 8);
    entry[3] = (uint8_t)name_offset;
    store_be32(entry + 4, offset);
    store_be32(entry + 8, size);
}

static int synthetic_image_read(
    void* context,
    uint32_t offset,
    void* destination,
    size_t size
) {
    SyntheticImage* image = (SyntheticImage*)context;

    if (image->short_read || offset > image->size || size > image->size - offset) {
        return 0;
    }
    if (size > 0) {
        memcpy(destination, image->bytes + offset, size);
    }
    return 1;
}

static AcgcDiscReader synthetic_reader(SyntheticImage* image) {
    AcgcDiscReader reader;

    reader.context = image;
    reader.size = (uint32_t)image->size;
    reader.read = synthetic_image_read;
    return reader;
}

static void make_synthetic_gcm(SyntheticImage* image) {
    static const uint8_t yaz0_rel[] = {
        'Y', 'a', 'z', '0',
        0x00, 0x00, 0x00, 0x03,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0xE0, 'A', 'B', 'C'
    };
    static const uint8_t strings[] =
        "dir\0nested.bin\0foresta.rel.szs\0";
    uint8_t* fst = image->bytes + SYNTHETIC_FST_OFFSET;

    memset(image, 0, sizeof(*image));
    image->size = SYNTHETIC_IMAGE_SIZE;

    store_be32(image->bytes + 0x1C, UINT32_C(0xC2339F3D));
    store_be32(image->bytes + 0x420, SYNTHETIC_DOL_OFFSET);
    store_be32(image->bytes + 0x424, SYNTHETIC_FST_OFFSET);
    store_be32(image->bytes + 0x428, SYNTHETIC_FST_SIZE);
    store_be32(image->bytes + 0x42C, SYNTHETIC_FST_SIZE);

    /* One synthetic DOL text section at the first byte after the header. */
    store_be32(image->bytes + SYNTHETIC_DOL_OFFSET, 0xE4);
    store_be32(image->bytes + SYNTHETIC_DOL_OFFSET + 0x90, 4);
    memcpy(image->bytes + SYNTHETIC_DOL_OFFSET + 0xE4, "DOL!", 4);

    /* root, dir/, dir/nested.bin, and foresta.rel.szs */
    store_fst_entry(fst, 1, 0, 0, 4);
    store_fst_entry(fst + 12, 1, 0, 0, 3);
    store_fst_entry(fst + 24, 0, 4, SYNTHETIC_RAW_REL_OFFSET, 8);
    store_fst_entry(fst + 36, 0, 15, SYNTHETIC_YAZ0_REL_OFFSET,
                    (uint32_t)sizeof(yaz0_rel));
    memcpy(fst + 48, strings, sizeof(strings));

    memcpy(image->bytes + SYNTHETIC_RAW_REL_OFFSET,
           "REL\0\x10\x20\x30\x40", 8);
    memcpy(image->bytes + SYNTHETIC_YAZ0_REL_OFFSET,
           yaz0_rel, sizeof(yaz0_rel));
}

typedef struct FstCapture {
    int count;
    char paths[4][ACGC_DISC_MAX_FST_PATH_SIZE];
    uint32_t offsets[4];
    uint32_t sizes[4];
} FstCapture;

static int capture_fst_file(
    void* context,
    const char* path,
    uint32_t offset,
    uint32_t size
) {
    FstCapture* capture = (FstCapture*)context;

    if (capture->count >= 4) {
        return 0;
    }
    strncpy(capture->paths[capture->count], path,
            sizeof(capture->paths[capture->count]) - 1);
    capture->paths[capture->count][
        sizeof(capture->paths[capture->count]) - 1] = '\0';
    capture->offsets[capture->count] = offset;
    capture->sizes[capture->count] = size;
    capture->count++;
    return 1;
}

static int test_fixed_width_byte_loads(void) {
    static const uint8_t bytes[] = { 0x12, 0x34, 0x56, 0x78 };

    CHECK(sizeof(uint32_t) == 4);
    CHECK(acgc_load_be32(bytes) == UINT32_C(0x12345678));
    CHECK(acgc_load_le32(bytes) == UINT32_C(0x78563412));
    return 0;
}

static int test_checked_address_helpers(void) {
    AcgcAddressRange high_range;
    AcgcAddressRange arena_range;
    AcgcAddressRange low_range;
    uintptr_t result;
    const uintptr_t high_base = UINTPTR_MAX - ADDRESS_CONST(0xFF);

    CHECK(acgc_address_align_up(
        ADDRESS_CONST(0x1003), ADDRESS_CONST(16), &result
    ) == ACGC_ADDRESS_OK);
    CHECK(result == ADDRESS_CONST(0x1010));
    CHECK(acgc_address_align_down(
        ADDRESS_CONST(0x1003), ADDRESS_CONST(16), &result
    ) == ACGC_ADDRESS_OK);
    CHECK(result == ADDRESS_CONST(0x1000));

    CHECK(acgc_address_align_up(
        high_base + ADDRESS_CONST(3), ADDRESS_CONST(16), &result
    ) == ACGC_ADDRESS_OK);
    CHECK(result == high_base + ADDRESS_CONST(16));
    CHECK(acgc_address_align_down(
        UINTPTR_MAX - ADDRESS_CONST(1), ADDRESS_CONST(16), &result
    ) == ACGC_ADDRESS_OK);
    CHECK(result == UINTPTR_MAX - ADDRESS_CONST(15));
    CHECK(acgc_address_align_up(
        UINTPTR_MAX - ADDRESS_CONST(14), ADDRESS_CONST(16), &result
    ) == ACGC_ADDRESS_OVERFLOW);

    CHECK(acgc_address_range_make(
        high_base, ADDRESS_CONST(0xF0), &high_range
    ) == ACGC_ADDRESS_OK);
    CHECK(high_range.begin == high_base);
    CHECK(high_range.end == UINTPTR_MAX - ADDRESS_CONST(0x0F));
    CHECK(acgc_address_range_make(
        high_base, ADDRESS_CONST(0x100), &high_range
    ) == ACGC_ADDRESS_OVERFLOW);
    CHECK(high_range.begin == high_base);
    CHECK(high_range.end == UINTPTR_MAX - ADDRESS_CONST(0x0F));

    CHECK(acgc_address_range_make(
        high_base, ADDRESS_CONST(0xF0), &high_range
    ) == ACGC_ADDRESS_OK);
    CHECK(acgc_address_range_contains(
        &high_range, high_base + ADDRESS_CONST(0x10), ADDRESS_CONST(0x20)
    ) == ACGC_ADDRESS_OK);
    CHECK(acgc_address_range_contains(
        &high_range, high_base - ADDRESS_CONST(1), 0
    ) == ACGC_ADDRESS_OUT_OF_RANGE);
    CHECK(acgc_address_range_contains(
        &high_range, high_range.end - ADDRESS_CONST(4), ADDRESS_CONST(8)
    ) == ACGC_ADDRESS_OUT_OF_RANGE);
    CHECK(acgc_address_tail_alloc(
        &high_range,
        high_range.end,
        ADDRESS_CONST(0x10),
        ADDRESS_CONST(16),
        &result
    ) == ACGC_ADDRESS_OK);
    CHECK(result == high_range.end - ADDRESS_CONST(0x10));

    CHECK(acgc_address_range_make(
        ADDRESS_CONST(0x1000), ADDRESS_CONST(0x100), &arena_range
    ) == ACGC_ADDRESS_OK);
    CHECK(acgc_address_tail_alloc(
        &arena_range,
        ADDRESS_CONST(0x10FF),
        1,
        ADDRESS_CONST(16),
        &result
    ) == ACGC_ADDRESS_OK);
    /* This is the legacy align-down, subtract, align-down sequence. */
    CHECK(result == ADDRESS_CONST(0x10E0));
    CHECK(acgc_address_tail_alloc(
        &arena_range,
        arena_range.end,
        ADDRESS_CONST(0x101),
        ADDRESS_CONST(16),
        &result
    ) == ACGC_ADDRESS_OUT_OF_RANGE);
    CHECK(acgc_address_range_make(
        0, ADDRESS_CONST(0x100), &low_range
    ) == ACGC_ADDRESS_OK);
    CHECK(acgc_address_tail_alloc(
        &low_range,
        ADDRESS_CONST(8),
        ADDRESS_CONST(16),
        ADDRESS_CONST(16),
        &result
    ) == ACGC_ADDRESS_UNDERFLOW);

    CHECK(acgc_address_align_up(
        ADDRESS_CONST(0x1000), 0, &result
    ) == ACGC_ADDRESS_INVALID_ALIGNMENT);
    CHECK(acgc_address_align_down(
        ADDRESS_CONST(0x1000), ADDRESS_CONST(3), &result
    ) == ACGC_ADDRESS_INVALID_ALIGNMENT);
    CHECK(acgc_address_alignment_from_mask(0, &result) == ACGC_ADDRESS_INVALID_ALIGNMENT);
    CHECK(acgc_address_alignment_from_mask(ADDRESS_CONST(3), &result) == ACGC_ADDRESS_INVALID_ALIGNMENT);
    CHECK(acgc_address_alignment_from_mask(UINTPTR_MAX - ADDRESS_CONST(15), &result) == ACGC_ADDRESS_OK);
    CHECK(result == ADDRESS_CONST(16));
    CHECK(acgc_address_tail_alloc(
        &arena_range,
        arena_range.end,
        1,
        0,
        &result
    ) == ACGC_ADDRESS_INVALID_ALIGNMENT);
    CHECK(acgc_address_align_up(
        ADDRESS_CONST(0x1000), ADDRESS_CONST(16), NULL
    ) == ACGC_ADDRESS_INVALID_ARGUMENT);
    CHECK(acgc_address_range_make(
        ADDRESS_CONST(0x1000), ADDRESS_CONST(1), NULL
    ) == ACGC_ADDRESS_INVALID_ARGUMENT);

    return 0;
}

static int test_registry_native_values_and_handles(void) {
    AcgcGbiReferenceRegistry registry;
    uint32_t handle = 0;
    uintptr_t resolved = 0;

    acgc_gbi_reference_registry_init(&registry);
    CHECK(acgc_gbi_reference_registry_register(
        &registry,
        (uintptr_t)0x12345678,
        &handle
    ) == ACGC_GBI_REFERENCE_OK);
    CHECK((handle & ACGC_GBI_REFERENCE_HANDLE_PREFIX_MASK) ==
          ACGC_GBI_REFERENCE_HANDLE_PREFIX);
    CHECK(acgc_gbi_reference_registry_resolve(
        &registry,
        handle,
        &resolved
    ) == ACGC_GBI_REFERENCE_OK);
    CHECK(resolved == (uintptr_t)0x12345678);

#if UINTPTR_MAX > UINT32_MAX
    const uintptr_t synthetic_value = (uintptr_t)UINT32_MAX + (uintptr_t)1;

    CHECK(acgc_gbi_reference_registry_register(
        &registry,
        synthetic_value,
        &handle
    ) == ACGC_GBI_REFERENCE_OK);
    CHECK(acgc_gbi_reference_registry_resolve(
        &registry,
        handle,
        &resolved
    ) == ACGC_GBI_REFERENCE_OK);
    CHECK(resolved == synthetic_value);

    {
        void* allocation = malloc(1);
        if (allocation != NULL && (uintptr_t)allocation > (uintptr_t)UINT32_MAX) {
            CHECK(acgc_gbi_reference_registry_register(
                &registry,
                (uintptr_t)allocation,
                &handle
            ) == ACGC_GBI_REFERENCE_OK);
            CHECK(acgc_gbi_reference_registry_resolve(
                &registry,
                handle,
                &resolved
            ) == ACGC_GBI_REFERENCE_OK);
            CHECK(resolved == (uintptr_t)allocation);
        }
        free(allocation);
    }
#endif

    return 0;
}

static int test_registry_reuse_and_stale_handles(void) {
    AcgcGbiReferenceRegistry registry;
    uint32_t first_handle = 0;
    uint32_t reused_handle = 0;
    uintptr_t resolved = 0;

    acgc_gbi_reference_registry_init(&registry);
    CHECK(acgc_gbi_reference_registry_register(
        &registry,
        (uintptr_t)0x101,
        &first_handle
    ) == ACGC_GBI_REFERENCE_OK);
    CHECK(acgc_gbi_reference_registry_release(
        &registry,
        first_handle
    ) == ACGC_GBI_REFERENCE_OK);
    CHECK(acgc_gbi_reference_registry_resolve(
        &registry,
        first_handle,
        &resolved
    ) == ACGC_GBI_REFERENCE_STALE_HANDLE);
    CHECK(resolved == 0);
    CHECK(acgc_gbi_reference_registry_release(
        &registry,
        first_handle
    ) == ACGC_GBI_REFERENCE_STALE_HANDLE);

    CHECK(acgc_gbi_reference_registry_register(
        &registry,
        (uintptr_t)0x203,
        &reused_handle
    ) == ACGC_GBI_REFERENCE_OK);
    CHECK(reused_handle != first_handle);
    CHECK((reused_handle & ACGC_GBI_REFERENCE_HANDLE_SLOT_MASK) ==
          (first_handle & ACGC_GBI_REFERENCE_HANDLE_SLOT_MASK));
    CHECK(acgc_gbi_reference_registry_resolve(
        &registry,
        first_handle,
        &resolved
    ) == ACGC_GBI_REFERENCE_STALE_HANDLE);
    CHECK(acgc_gbi_reference_registry_resolve(
        &registry,
        reused_handle,
        &resolved
    ) == ACGC_GBI_REFERENCE_OK);
    CHECK(resolved == (uintptr_t)0x203);

    {
        uint32_t duplicate_handle = 0;
        CHECK(acgc_gbi_reference_registry_register(
            &registry,
            (uintptr_t)0x203,
            &duplicate_handle
        ) == ACGC_GBI_REFERENCE_OK);
        CHECK(duplicate_handle == reused_handle);
    }

    return 0;
}

static int test_registry_rejects_malformed_handles(void) {
    AcgcGbiReferenceRegistry registry;
    uintptr_t resolved = (uintptr_t)1;

    acgc_gbi_reference_registry_init(&registry);
    CHECK(acgc_gbi_reference_registry_resolve(
        &registry,
        0,
        &resolved
    ) == ACGC_GBI_REFERENCE_INVALID_HANDLE);
    CHECK(resolved == 0);
    CHECK(acgc_gbi_reference_registry_resolve(
        &registry,
        ACGC_GBI_REFERENCE_HANDLE_PREFIX,
        &resolved
    ) == ACGC_GBI_REFERENCE_INVALID_HANDLE);
    CHECK(acgc_gbi_reference_registry_resolve(
        &registry,
        UINT32_C(0xE0000001),
        &resolved
    ) == ACGC_GBI_REFERENCE_INVALID_HANDLE);
    CHECK(acgc_gbi_reference_registry_resolve(
        &registry,
        UINT32_C(0x02F00000),
        &resolved
    ) == ACGC_GBI_REFERENCE_INVALID_HANDLE);
    CHECK(acgc_gbi_reference_registry_resolve(
        &registry,
        UINT32_C(0xFFFFFFFF),
        &resolved
    ) == ACGC_GBI_REFERENCE_STALE_HANDLE);
    CHECK(acgc_gbi_reference_registry_resolve(
        NULL,
        UINT32_C(0xFFFFFFFF),
        &resolved
    ) == ACGC_GBI_REFERENCE_INVALID_ARGUMENT);
    CHECK(acgc_gbi_reference_registry_register(
        &registry,
        (uintptr_t)1,
        NULL
    ) == ACGC_GBI_REFERENCE_INVALID_ARGUMENT);
    return 0;
}

static int test_registry_exhaustion(void) {
    static AcgcGbiReferenceRegistry registry;
    static uint32_t handles[ACGC_GBI_REFERENCE_REGISTRY_CAPACITY];
    uint32_t slot;
    uint32_t failed_handle = UINT32_MAX;
    uint32_t replacement_handle = 0;

    acgc_gbi_reference_registry_init(&registry);
    for (slot = 0; slot < ACGC_GBI_REFERENCE_REGISTRY_CAPACITY; slot++) {
        CHECK(acgc_gbi_reference_registry_register(
            &registry,
            (uintptr_t)0x10000000 + (uintptr_t)slot,
            &handles[slot]
        ) == ACGC_GBI_REFERENCE_OK);
        CHECK(handles[slot] != 0);
        if (slot > 0) {
            CHECK(handles[slot] != handles[slot - 1]);
        }
    }

    CHECK(acgc_gbi_reference_registry_register(
        &registry,
        (uintptr_t)0x20000000,
        &failed_handle
    ) == ACGC_GBI_REFERENCE_EXHAUSTED);
    CHECK(failed_handle == 0);
    CHECK(acgc_gbi_reference_registry_release(
        &registry,
        handles[ACGC_GBI_REFERENCE_REGISTRY_CAPACITY - 1]
    ) == ACGC_GBI_REFERENCE_OK);
    CHECK(acgc_gbi_reference_registry_register(
        &registry,
        (uintptr_t)0x20000000,
        &replacement_handle
    ) == ACGC_GBI_REFERENCE_OK);
    CHECK(replacement_handle != handles[ACGC_GBI_REFERENCE_REGISTRY_CAPACITY - 1]);
    return 0;
}

static int test_registry_reset_is_deterministic_and_invalidates(void) {
    AcgcGbiReferenceRegistry first;
    AcgcGbiReferenceRegistry second;
    uint32_t first_handle = 0;
    uint32_t second_handle = 0;
    uint32_t reset_first_handle = 0;
    uint32_t reset_second_handle = 0;
    uintptr_t resolved = 0;

    acgc_gbi_reference_registry_init(&first);
    acgc_gbi_reference_registry_init(&second);
    CHECK(acgc_gbi_reference_registry_register(
        &first,
        (uintptr_t)0x301,
        &first_handle
    ) == ACGC_GBI_REFERENCE_OK);
    CHECK(acgc_gbi_reference_registry_register(
        &second,
        (uintptr_t)0x301,
        &second_handle
    ) == ACGC_GBI_REFERENCE_OK);
    CHECK(first_handle == second_handle);

    acgc_gbi_reference_registry_reset(&first);
    acgc_gbi_reference_registry_reset(&second);
    CHECK(acgc_gbi_reference_registry_resolve(
        &first,
        first_handle,
        &resolved
    ) == ACGC_GBI_REFERENCE_STALE_HANDLE);
    CHECK(acgc_gbi_reference_registry_register(
        &first,
        (uintptr_t)0x302,
        &reset_first_handle
    ) == ACGC_GBI_REFERENCE_OK);
    CHECK(acgc_gbi_reference_registry_register(
        &second,
        (uintptr_t)0x302,
        &reset_second_handle
    ) == ACGC_GBI_REFERENCE_OK);
    CHECK(reset_first_handle == reset_second_handle);
    CHECK(reset_first_handle != first_handle);

    return 0;
}

static int test_synthetic_gcm_fst_dol_and_rel(void) {
    SyntheticImage image;
    AcgcDiscReader reader;
    AcgcGcmInfo gcm;
    AcgcDiscStatus status;
    FstCapture capture = { 0 };
    uint32_t dol_size = 0;
    uint8_t* extracted = NULL;
    uint32_t extracted_size = 0;
    AcgcRelFormat format = ACGC_REL_RAW;
    AcgcRelLimits limits = { 64, 64 };

    make_synthetic_gcm(&image);
    reader = synthetic_reader(&image);

    status = acgc_gcm_parse(&reader, &gcm);
    CHECK(status == ACGC_DISC_OK);
    CHECK(gcm.dol_offset == SYNTHETIC_DOL_OFFSET);
    CHECK(gcm.fst_offset == SYNTHETIC_FST_OFFSET);
    CHECK(gcm.fst_size == SYNTHETIC_FST_SIZE);

    CHECK(acgc_dol_get_size(&reader, gcm.dol_offset, &dol_size) == ACGC_DISC_OK);
    CHECK(dol_size == 0xE8);

    CHECK(acgc_fst_visit(&reader, &gcm, capture_fst_file, &capture) == ACGC_DISC_OK);
    CHECK(capture.count == 2);
    CHECK(strcmp(capture.paths[0], "dir/nested.bin") == 0);
    CHECK(capture.offsets[0] == SYNTHETIC_RAW_REL_OFFSET);
    CHECK(capture.sizes[0] == 8);
    CHECK(strcmp(capture.paths[1], "foresta.rel.szs") == 0);
    CHECK(capture.offsets[1] == SYNTHETIC_YAZ0_REL_OFFSET);

    CHECK(acgc_rel_extract(
        &reader,
        capture.offsets[0],
        capture.sizes[0],
        &limits,
        &extracted,
        &extracted_size,
        &format
    ) == ACGC_DISC_OK);
    CHECK(format == ACGC_REL_RAW);
    CHECK(extracted_size == 8);
    CHECK(memcmp(extracted, "REL\0\x10\x20\x30\x40", 8) == 0);
    free(extracted);

    extracted = NULL;
    extracted_size = 0;
    CHECK(acgc_rel_extract(
        &reader,
        capture.offsets[1],
        capture.sizes[1],
        &limits,
        &extracted,
        &extracted_size,
        &format
    ) == ACGC_DISC_OK);
    CHECK(format == ACGC_REL_YAZ0);
    CHECK(extracted_size == 3);
    CHECK(memcmp(extracted, "ABC", 3) == 0);
    free(extracted);
    return 0;
}

static int test_rejects_truncated_and_short_reads(void) {
    SyntheticImage image;
    AcgcDiscReader reader;
    AcgcGcmInfo gcm;

    make_synthetic_gcm(&image);
    image.size = 0x100;
    reader = synthetic_reader(&image);
    CHECK(acgc_gcm_parse(&reader, &gcm) == ACGC_DISC_TRUNCATED_INPUT);

    make_synthetic_gcm(&image);
    image.short_read = 1;
    reader = synthetic_reader(&image);
    CHECK(acgc_gcm_parse(&reader, &gcm) == ACGC_DISC_READ_FAILED);
    return 0;
}

static int test_rejects_bad_offsets_and_sizes(void) {
    SyntheticImage image;
    AcgcDiscReader reader;
    AcgcGcmInfo gcm;
    FstCapture capture = { 0 };
    uint32_t dol_size = 0;
    uint8_t* extracted = (uint8_t*)(uintptr_t)1;
    uint32_t extracted_size = 1;
    AcgcRelLimits limits = { 64, 64 };

    make_synthetic_gcm(&image);
    store_be32(image.bytes + SYNTHETIC_FST_OFFSET + 24 + 4,
               (uint32_t)image.size - 4);
    reader = synthetic_reader(&image);
    CHECK(acgc_gcm_parse(&reader, &gcm) == ACGC_DISC_OK);
    CHECK(acgc_fst_visit(&reader, &gcm, capture_fst_file, &capture) ==
          ACGC_DISC_INVALID_RANGE);
    CHECK(acgc_rel_extract(
        &reader,
        (uint32_t)image.size - 4,
        8,
        &limits,
        &extracted,
        &extracted_size,
        NULL
    ) == ACGC_DISC_INVALID_RANGE);
    CHECK(extracted == NULL);
    CHECK(extracted_size == 0);

    make_synthetic_gcm(&image);
    store_be32(image.bytes + SYNTHETIC_DOL_OFFSET, UINT32_C(0xFFFFFFF0));
    store_be32(image.bytes + SYNTHETIC_DOL_OFFSET + 0x90, 0x40);
    reader = synthetic_reader(&image);
    CHECK(acgc_gcm_parse(&reader, &gcm) == ACGC_DISC_OK);
    CHECK(acgc_dol_get_size(&reader, gcm.dol_offset, &dol_size) ==
          ACGC_DISC_INVALID_RANGE);

    make_synthetic_gcm(&image);
    store_be32(image.bytes + SYNTHETIC_FST_OFFSET + 8,
               ACGC_DISC_MAX_FST_ENTRIES + 1);
    reader = synthetic_reader(&image);
    CHECK(acgc_gcm_parse(&reader, &gcm) == ACGC_DISC_OK);
    CHECK(acgc_fst_visit(&reader, &gcm, capture_fst_file, &capture) ==
          ACGC_DISC_LIMIT_EXCEEDED);

    make_synthetic_gcm(&image);
    store_be32(image.bytes + 0x424, SYNTHETIC_IMAGE_SIZE - 0x40);
    store_be32(image.bytes + 0x428, SYNTHETIC_FST_SIZE);
    store_be32(image.bytes + 0x42C, SYNTHETIC_FST_SIZE);
    reader = synthetic_reader(&image);
    CHECK(acgc_gcm_parse(&reader, &gcm) == ACGC_DISC_INVALID_RANGE);
    return 0;
}

static int test_rejects_oversized_rel_input_and_output(void) {
    SyntheticImage image;
    AcgcDiscReader reader;
    uint8_t* extracted = (uint8_t*)(uintptr_t)1;
    uint32_t extracted_size = 1;
    AcgcRelLimits limits;

    make_synthetic_gcm(&image);
    reader = synthetic_reader(&image);

    limits.max_input_size = 7;
    limits.max_output_size = 64;
    CHECK(acgc_rel_extract(
        &reader,
        SYNTHETIC_RAW_REL_OFFSET,
        8,
        &limits,
        &extracted,
        &extracted_size,
        NULL
    ) == ACGC_DISC_LIMIT_EXCEEDED);
    CHECK(extracted == NULL);
    CHECK(extracted_size == 0);

    limits.max_input_size = 64;
    limits.max_output_size = 7;
    CHECK(acgc_rel_extract(
        &reader,
        SYNTHETIC_RAW_REL_OFFSET,
        8,
        &limits,
        &extracted,
        &extracted_size,
        NULL
    ) == ACGC_DISC_LIMIT_EXCEEDED);
    CHECK(extracted == NULL);
    CHECK(extracted_size == 0);

    limits.max_input_size = 64;
    limits.max_output_size = 2;
    CHECK(acgc_rel_extract(
        &reader,
        SYNTHETIC_YAZ0_REL_OFFSET,
        20,
        &limits,
        &extracted,
        &extracted_size,
        NULL
    ) == ACGC_DISC_LIMIT_EXCEEDED);
    CHECK(extracted == NULL);
    CHECK(extracted_size == 0);
    return 0;
}

static int test_literal_stream(void) {
    static const uint8_t encoded[] = {
        'Y', 'a', 'z', '0',
        0x00, 0x00, 0x00, 0x03,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0xE0, 'A', 'B', 'C'
    };
    uint8_t* decoded = NULL;
    uint32_t decoded_size = 0;

    CHECK(acgc_yaz0_decode(
        encoded,
        sizeof(encoded),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_OK);
    CHECK(decoded != NULL);
    CHECK(decoded_size == 3);
    CHECK(memcmp(decoded, "ABC", 3) == 0);
    free(decoded);
    return 0;
}

static int test_back_reference_stream(void) {
    static const uint8_t encoded[] = {
        'Y', 'a', 'z', '0',
        0x00, 0x00, 0x00, 0x09,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0xE0, 'A', 'B', 'C', 0x40, 0x02
    };
    uint8_t* decoded = NULL;
    uint32_t decoded_size = 0;

    CHECK(acgc_yaz0_decode(
        encoded,
        sizeof(encoded),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_OK);
    CHECK(decoded != NULL);
    CHECK(decoded_size == 9);
    CHECK(memcmp(decoded, "ABCABCABC", 9) == 0);
    free(decoded);
    return 0;
}

static int test_extended_back_reference_stream(void) {
    static const uint8_t encoded[] = {
        'Y', 'a', 'z', '0',
        0x00, 0x00, 0x00, 0x15,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x80, 'A', 0x00, 0x00, 0x02
    };
    static const char expected[] = "AAAAAAAAAAAAAAAAAAAAA";
    uint8_t* decoded = NULL;
    uint32_t decoded_size = 0;

    CHECK(acgc_yaz0_decode(
        encoded,
        sizeof(encoded),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_OK);
    CHECK(decoded != NULL);
    CHECK(decoded_size == sizeof(expected) - 1);
    CHECK(memcmp(decoded, expected, sizeof(expected) - 1) == 0);
    free(decoded);
    return 0;
}

static int test_zero_length_stream(void) {
    static const uint8_t encoded[] = {
        'Y', 'a', 'z', '0',
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };
    uint8_t* decoded = (uint8_t*)(uintptr_t)1;
    uint32_t decoded_size = 1;

    CHECK(acgc_yaz0_decode(
        encoded,
        sizeof(encoded),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_OK);
    CHECK(decoded == NULL);
    CHECK(decoded_size == 0);
    return 0;
}

static int test_rejects_malformed_streams(void) {
    static const uint8_t truncated_literal[] = {
        'Y', 'a', 'z', '0',
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x80
    };
    static const uint8_t invalid_reference[] = {
        'Y', 'a', 'z', '0',
        0x00, 0x00, 0x00, 0x03,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x10, 0x00
    };
    static const uint8_t truncated_reference[] = {
        'Y', 'a', 'z', '0',
        0x00, 0x00, 0x00, 0x03,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x10
    };
    static const uint8_t truncated_extended_length[] = {
        'Y', 'a', 'z', '0',
        0x00, 0x00, 0x00, 0x03,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x80, 'A', 0x00, 0x00
    };
    static const uint8_t bad_header[16] = { 0 };
    static const uint8_t short_yaz0_header[] = { 'Y', 'a', 'z', '0' };
    static const uint8_t oversized_output[] = {
        'Y', 'a', 'z', '0',
        0x00, 0x00, 0x10, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };
    uint8_t* decoded = (uint8_t*)(uintptr_t)1;
    uint32_t decoded_size = 1;

    CHECK(acgc_yaz0_decode(
        NULL,
        0,
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_INVALID_ARGUMENT);
    CHECK(decoded == NULL);
    CHECK(decoded_size == 0);

    CHECK(acgc_yaz0_decode(
        truncated_literal,
        sizeof(truncated_literal),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_TRUNCATED_INPUT);
    CHECK(decoded == NULL);
    CHECK(decoded_size == 0);

    CHECK(acgc_yaz0_decode(
        truncated_reference,
        sizeof(truncated_reference),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_TRUNCATED_INPUT);
    CHECK(decoded == NULL);
    CHECK(decoded_size == 0);

    CHECK(acgc_yaz0_decode(
        truncated_extended_length,
        sizeof(truncated_extended_length),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_TRUNCATED_INPUT);
    CHECK(decoded == NULL);
    CHECK(decoded_size == 0);

    CHECK(acgc_yaz0_decode(
        invalid_reference,
        sizeof(invalid_reference),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_INVALID_BACK_REFERENCE);
    CHECK(decoded == NULL);
    CHECK(decoded_size == 0);

    CHECK(acgc_yaz0_decode(
        bad_header,
        sizeof(bad_header),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_INVALID_HEADER);
    CHECK(decoded == NULL);
    CHECK(decoded_size == 0);

    CHECK(acgc_yaz0_decode(
        short_yaz0_header,
        sizeof(short_yaz0_header),
        TEST_OUTPUT_LIMIT,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_INVALID_HEADER);
    CHECK(decoded == NULL);
    CHECK(decoded_size == 0);

    CHECK(acgc_yaz0_decode(
        oversized_output,
        sizeof(oversized_output),
        1024,
        &decoded,
        &decoded_size
    ) == ACGC_YAZ0_OUTPUT_LIMIT_EXCEEDED);
    CHECK(decoded == NULL);
    CHECK(decoded_size == 0);

    CHECK(acgc_yaz0_decode(
        bad_header,
        sizeof(bad_header),
        TEST_OUTPUT_LIMIT,
        NULL,
        &decoded_size
    ) == ACGC_YAZ0_INVALID_ARGUMENT);
    CHECK(acgc_yaz0_decode(
        bad_header,
        sizeof(bad_header),
        TEST_OUTPUT_LIMIT,
        &decoded,
        NULL
    ) == ACGC_YAZ0_INVALID_ARGUMENT);
    return 0;
}

int main(void) {
    CHECK(test_fixed_width_byte_loads() == 0);
    CHECK(test_checked_address_helpers() == 0);
    CHECK(test_registry_native_values_and_handles() == 0);
    CHECK(test_registry_reuse_and_stale_handles() == 0);
    CHECK(test_registry_rejects_malformed_handles() == 0);
    CHECK(test_registry_exhaustion() == 0);
    CHECK(test_registry_reset_is_deterministic_and_invalidates() == 0);
    CHECK(test_synthetic_gcm_fst_dol_and_rel() == 0);
    CHECK(test_rejects_truncated_and_short_reads() == 0);
    CHECK(test_rejects_bad_offsets_and_sizes() == 0);
    CHECK(test_rejects_oversized_rel_input_and_output() == 0);
    CHECK(test_literal_stream() == 0);
    CHECK(test_back_reference_stream() == 0);
    CHECK(test_extended_back_reference_stream() == 0);
    CHECK(test_zero_length_stream() == 0);
    CHECK(test_rejects_malformed_streams() == 0);

    printf("acgc portable tests passed\n");
    return 0;
}
