#include "acgc/boot_source.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYNTHETIC_IMAGE_SIZE UINT32_C(0x2000)
#define SYNTHETIC_DOL_OFFSET UINT32_C(0x500)
#define SYNTHETIC_DOL_SIZE UINT32_C(0xE8)
#define SYNTHETIC_FST_OFFSET UINT32_C(0x700)
#define SYNTHETIC_FST_SIZE UINT32_C(0x80)
#define SYNTHETIC_RAW_REL_OFFSET UINT32_C(0x800)
#define SYNTHETIC_YAZ0_REL_OFFSET UINT32_C(0x900)

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static const uint8_t synthetic_yaz0_rel[] = {
    'Y', 'a', 'z', '0',
    0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0xE0, 'A', 'B', 'C'
};

static const char synthetic_fst_strings[] =
    "dir\0nested.bin\0foresta.rel.szs\0";

typedef enum SyntheticReadMode {
    SYNTHETIC_READ_OK = 0,
    SYNTHETIC_READ_SHORT,
    SYNTHETIC_READ_FAIL,
    SYNTHETIC_READ_TARGETED_FAIL
} SyntheticReadMode;

typedef struct SyntheticImage {
    uint8_t bytes[SYNTHETIC_IMAGE_SIZE];
    uint32_t size;
    SyntheticReadMode read_mode;
    uint32_t fail_offset;
    uint32_t fail_size;
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

static int synthetic_read(
    void* context,
    uint32_t offset,
    void* destination,
    size_t size
) {
    SyntheticImage* image = (SyntheticImage*)context;

    if (offset > image->size || size > image->size - offset) {
        return 0;
    }
    if (image->read_mode == SYNTHETIC_READ_FAIL) {
        return 0;
    }
    if (image->read_mode == SYNTHETIC_READ_TARGETED_FAIL &&
        offset == image->fail_offset && size == image->fail_size) {
        return 0;
    }
    if (image->read_mode == SYNTHETIC_READ_SHORT) {
        if (size > 0) {
            memcpy(destination, image->bytes + offset, size - 1);
        }
        return 0;
    }
    memcpy(destination, image->bytes + offset, size);
    return 1;
}

static AcgcDiscReader synthetic_reader(SyntheticImage* image) {
    AcgcDiscReader reader;

    reader.context = image;
    reader.size = image->size;
    reader.read = synthetic_read;
    return reader;
}

static void make_synthetic_image(SyntheticImage* image) {
    uint8_t* fst;

    memset(image, 0, sizeof(*image));
    image->size = SYNTHETIC_IMAGE_SIZE;
    memcpy(image->bytes, "GAFE01", 6);
    image->bytes[6] = 0;
    image->bytes[7] = 0;
    store_be32(image->bytes + 0x1C, UINT32_C(0xC2339F3D));
    store_be32(image->bytes + 0x420, SYNTHETIC_DOL_OFFSET);
    store_be32(image->bytes + 0x424, SYNTHETIC_FST_OFFSET);
    store_be32(image->bytes + 0x428, SYNTHETIC_FST_SIZE);
    store_be32(image->bytes + 0x42C, SYNTHETIC_FST_SIZE);

    store_be32(image->bytes + SYNTHETIC_DOL_OFFSET, 0xE4);
    store_be32(image->bytes + SYNTHETIC_DOL_OFFSET + 0x90, 4);
    memcpy(image->bytes + SYNTHETIC_DOL_OFFSET + 0xE4, "DOL!", 4);

    fst = image->bytes + SYNTHETIC_FST_OFFSET;
    store_fst_entry(fst, 1, 0, 0, 4);
    store_fst_entry(fst + 12, 1, 0, 0, 3);
    store_fst_entry(fst + 24, 0, 4, SYNTHETIC_RAW_REL_OFFSET, 8);
    store_fst_entry(
        fst + 36,
        0,
        15,
        SYNTHETIC_YAZ0_REL_OFFSET,
        (uint32_t)sizeof(synthetic_yaz0_rel)
    );
    memcpy(fst + 48, synthetic_fst_strings, sizeof(synthetic_fst_strings));

    memcpy(image->bytes + SYNTHETIC_RAW_REL_OFFSET,
           "REL\0\x10\x20\x30\x40", 8);
    memcpy(image->bytes + SYNTHETIC_YAZ0_REL_OFFSET,
           synthetic_yaz0_rel,
           sizeof(synthetic_yaz0_rel));
}

static void make_default_limits(AcgcBootSourceLimits* limits) {
    acgc_boot_source_limits_default(limits);
}

static int manifest_is_zero(const AcgcBootSourceManifest* manifest) {
    AcgcBootSourceManifest zero = { 0 };

    return memcmp(manifest, &zero, sizeof(zero)) == 0;
}

static int images_are_zero(const AcgcBootSourceImages* images) {
    AcgcBootSourceImages zero = { 0 };

    return memcmp(images, &zero, sizeof(zero)) == 0;
}

static int test_exact_revision_and_prepare(void) {
    SyntheticImage image;
    AcgcDiscReader reader;
    AcgcBootSourceManifest manifest = { 0 };
    AcgcBootSourceImages images = { 0 };
    AcgcBootSourceLimits limits;
    static const uint8_t expected_revision[] = {
        'G', 'A', 'F', 'E', '0', '1', 0, 0
    };

    make_synthetic_image(&image);
    reader = synthetic_reader(&image);
    make_default_limits(&limits);

    CHECK(acgc_boot_source_inspect(&reader, &manifest) ==
          ACGC_BOOT_SOURCE_OK);
    CHECK(memcmp(
        manifest.revision,
        expected_revision,
        sizeof(expected_revision)
    ) == 0);
    CHECK(manifest.dol_offset == SYNTHETIC_DOL_OFFSET);
    CHECK(manifest.dol_size == SYNTHETIC_DOL_SIZE);
    CHECK(manifest.fst_file_count == 2);
    CHECK(manifest.rel_input_offset == SYNTHETIC_YAZ0_REL_OFFSET);
    CHECK(manifest.rel_input_size == sizeof(synthetic_yaz0_rel));

    CHECK(acgc_boot_source_prepare(&reader, &limits, &images) ==
          ACGC_BOOT_SOURCE_OK);
    CHECK(memcmp(&images.manifest, &manifest, sizeof(manifest)) == 0);
    CHECK(images.dol_data != NULL);
    CHECK(manifest.dol_size == SYNTHETIC_DOL_SIZE);
    CHECK(memcmp(images.dol_data + 0xE4, "DOL!", 4) == 0);
    CHECK(images.rel_data != NULL);
    CHECK(images.rel_size == 3);
    CHECK(images.rel_format == ACGC_REL_YAZ0);
    CHECK(memcmp(images.rel_data, "ABC", 3) == 0);

    acgc_boot_source_dispose(&images);
    CHECK(images_are_zero(&images));
    acgc_boot_source_dispose(&images);
    acgc_boot_source_dispose(NULL);
    return 0;
}

static int test_revision_rejections(void) {
    static const struct {
        size_t byte;
        uint8_t value;
    } cases[] = {
        { 3, 'J' }, /* wrong region/game ID */
        { 5, '2' }, /* wrong company */
        { 6, 1 },   /* nonzero disc number */
        { 7, 1 }    /* nonzero game version */
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        SyntheticImage image;
        AcgcDiscReader reader;
        AcgcBootSourceManifest manifest = { 0 };
        AcgcBootSourceImages images = { 0 };

        make_synthetic_image(&image);
        image.bytes[cases[i].byte] = cases[i].value;
        reader = synthetic_reader(&image);
        CHECK(acgc_boot_source_inspect(&reader, &manifest) ==
              ACGC_BOOT_SOURCE_UNSUPPORTED_REVISION);
        CHECK(manifest_is_zero(&manifest));
        CHECK(acgc_boot_source_prepare(&reader, NULL, &images) ==
              ACGC_BOOT_SOURCE_UNSUPPORTED_REVISION);
        CHECK(images_are_zero(&images));
        acgc_boot_source_dispose(&images);
    }
    return 0;
}

static int test_missing_and_duplicate_rel(void) {
    SyntheticImage image;
    AcgcDiscReader reader;
    AcgcBootSourceManifest manifest = { 0 };
    AcgcBootSourceImages images = { 0 };
    uint8_t* fst;

    make_synthetic_image(&image);
    fst = image.bytes + SYNTHETIC_FST_OFFSET;
    store_fst_entry(
        fst + 36,
        0,
        4,
        SYNTHETIC_YAZ0_REL_OFFSET,
        (uint32_t)sizeof(synthetic_yaz0_rel)
    );
    reader = synthetic_reader(&image);
    CHECK(acgc_boot_source_inspect(&reader, &manifest) ==
          ACGC_BOOT_SOURCE_MISSING_REL);
    CHECK(manifest_is_zero(&manifest));
    CHECK(acgc_boot_source_prepare(&reader, NULL, &images) ==
          ACGC_BOOT_SOURCE_MISSING_REL);
    CHECK(images_are_zero(&images));

    make_synthetic_image(&image);
    fst = image.bytes + SYNTHETIC_FST_OFFSET;
    /* Turn the directory into a root file so entries 2 and 3 share the name. */
    store_fst_entry(fst + 12, 0, 0, 0, 3);
    store_fst_entry(fst + 24, 0, 15, SYNTHETIC_RAW_REL_OFFSET, 8);
    reader = synthetic_reader(&image);
    memset(&manifest, 0xA5, sizeof(manifest));
    CHECK(acgc_boot_source_inspect(&reader, &manifest) ==
          ACGC_BOOT_SOURCE_DUPLICATE_REL);
    CHECK(manifest_is_zero(&manifest));
    CHECK(acgc_boot_source_prepare(&reader, NULL, &images) ==
          ACGC_BOOT_SOURCE_DUPLICATE_REL);
    CHECK(images_are_zero(&images));
    acgc_boot_source_dispose(&images);
    return 0;
}

static int test_invalid_and_truncated_ranges(void) {
    SyntheticImage image;
    AcgcDiscReader reader;
    AcgcBootSourceManifest manifest = { 0 };
    uint8_t* fst;

    make_synthetic_image(&image);
    image.size = 0x400;
    reader = synthetic_reader(&image);
    CHECK(acgc_boot_source_inspect(&reader, &manifest) ==
          ACGC_BOOT_SOURCE_TRUNCATED_INPUT);
    CHECK(manifest_is_zero(&manifest));

    make_synthetic_image(&image);
    fst = image.bytes + SYNTHETIC_FST_OFFSET;
    store_fst_entry(fst + 24, 0, 4, SYNTHETIC_IMAGE_SIZE - 4, 8);
    reader = synthetic_reader(&image);
    CHECK(acgc_boot_source_inspect(&reader, &manifest) ==
          ACGC_BOOT_SOURCE_INVALID_RANGE);
    CHECK(manifest_is_zero(&manifest));

    make_synthetic_image(&image);
    store_be32(image.bytes + SYNTHETIC_DOL_OFFSET + 0x90, UINT32_MAX);
    reader = synthetic_reader(&image);
    CHECK(acgc_boot_source_inspect(&reader, &manifest) ==
          ACGC_BOOT_SOURCE_INVALID_RANGE);
    CHECK(manifest_is_zero(&manifest));
    return 0;
}

static int test_dol_and_rel_limits(void) {
    SyntheticImage image;
    AcgcDiscReader reader;
    AcgcBootSourceImages images = { 0 };
    AcgcBootSourceLimits limits;

    make_synthetic_image(&image);
    reader = synthetic_reader(&image);
    make_default_limits(&limits);
    limits.max_dol_size = SYNTHETIC_DOL_SIZE - 1;
    CHECK(acgc_boot_source_prepare(&reader, &limits, &images) ==
          ACGC_BOOT_SOURCE_DOL_LIMIT_EXCEEDED);
    CHECK(images_are_zero(&images));

    make_synthetic_image(&image);
    reader = synthetic_reader(&image);
    make_default_limits(&limits);
    limits.rel.max_input_size = (uint32_t)sizeof(synthetic_yaz0_rel) - 1;
    CHECK(acgc_boot_source_prepare(&reader, &limits, &images) ==
          ACGC_BOOT_SOURCE_REL_INPUT_LIMIT_EXCEEDED);
    CHECK(images_are_zero(&images));

    make_synthetic_image(&image);
    reader = synthetic_reader(&image);
    make_default_limits(&limits);
    limits.rel.max_output_size = 2;
    CHECK(acgc_boot_source_prepare(&reader, &limits, &images) ==
          ACGC_BOOT_SOURCE_REL_OUTPUT_LIMIT_EXCEEDED);
    CHECK(images_are_zero(&images));
    acgc_boot_source_dispose(&images);
    return 0;
}

static int test_raw_rel_prepare_success(void) {
    SyntheticImage image;
    AcgcDiscReader reader;
    AcgcBootSourceImages images = { 0 };
    AcgcBootSourceLimits limits;
    uint8_t* fst;

    make_synthetic_image(&image);
    fst = image.bytes + SYNTHETIC_FST_OFFSET;
    store_fst_entry(fst + 36, 0, 15, SYNTHETIC_RAW_REL_OFFSET, 8);
    reader = synthetic_reader(&image);
    make_default_limits(&limits);

    CHECK(acgc_boot_source_prepare(&reader, &limits, &images) ==
          ACGC_BOOT_SOURCE_OK);
    CHECK(images.rel_format == ACGC_REL_RAW);
    CHECK(images.rel_size == 8);
    CHECK(images.rel_data != NULL);
    CHECK(memcmp(images.rel_data, "REL\0\x10\x20\x30\x40", 8) == 0);
    acgc_boot_source_dispose(&images);
    CHECK(images_are_zero(&images));
    return 0;
}

static int test_callback_short_and_failure(void) {
    SyntheticImage image;
    AcgcDiscReader reader;
    AcgcBootSourceManifest manifest = { 0 };
    AcgcBootSourceImages images = { 0 };

    make_synthetic_image(&image);
    image.read_mode = SYNTHETIC_READ_SHORT;
    reader = synthetic_reader(&image);
    CHECK(acgc_boot_source_inspect(&reader, &manifest) ==
          ACGC_BOOT_SOURCE_READ_FAILED);
    CHECK(manifest_is_zero(&manifest));

    make_synthetic_image(&image);
    image.read_mode = SYNTHETIC_READ_FAIL;
    reader = synthetic_reader(&image);
    CHECK(acgc_boot_source_prepare(&reader, NULL, &images) ==
          ACGC_BOOT_SOURCE_READ_FAILED);
    CHECK(images_are_zero(&images));
    acgc_boot_source_dispose(&images);

    make_synthetic_image(&image);
    image.read_mode = SYNTHETIC_READ_TARGETED_FAIL;
    image.fail_offset = SYNTHETIC_DOL_OFFSET;
    image.fail_size = SYNTHETIC_DOL_SIZE;
    reader = synthetic_reader(&image);
    CHECK(acgc_boot_source_prepare(&reader, NULL, &images) ==
          ACGC_BOOT_SOURCE_READ_FAILED);
    CHECK(images_are_zero(&images));
    return 0;
}

static int test_rel_decode_failure_and_partial_dispose(void) {
    SyntheticImage image;
    AcgcDiscReader reader;
    AcgcBootSourceImages images = { 0 };

    make_synthetic_image(&image);
    image.bytes[SYNTHETIC_YAZ0_REL_OFFSET + 16] = 0;
    image.bytes[SYNTHETIC_YAZ0_REL_OFFSET + 17] = 0;
    reader = synthetic_reader(&image);
    CHECK(acgc_boot_source_prepare(&reader, NULL, &images) ==
          ACGC_BOOT_SOURCE_REL_DECODE_FAILED);
    CHECK(images_are_zero(&images));
    acgc_boot_source_dispose(&images);

    make_synthetic_image(&image);
    image.read_mode = SYNTHETIC_READ_TARGETED_FAIL;
    image.fail_offset = SYNTHETIC_YAZ0_REL_OFFSET;
    image.fail_size = (uint32_t)sizeof(synthetic_yaz0_rel);
    reader = synthetic_reader(&image);
    CHECK(acgc_boot_source_prepare(&reader, NULL, &images) ==
          ACGC_BOOT_SOURCE_READ_FAILED);
    CHECK(images_are_zero(&images));
    acgc_boot_source_dispose(&images);
    acgc_boot_source_dispose(&images);
    return 0;
}

static int test_rejects_zero_output_yaz0_rel(void) {
    SyntheticImage image;
    AcgcDiscReader reader;
    AcgcBootSourceImages images = { 0 };

    make_synthetic_image(&image);
    store_be32(image.bytes + SYNTHETIC_YAZ0_REL_OFFSET + 4, 0);
    reader = synthetic_reader(&image);
    CHECK(acgc_boot_source_prepare(&reader, NULL, &images) ==
          ACGC_BOOT_SOURCE_EMPTY_REL);
    CHECK(images_are_zero(&images));
    acgc_boot_source_dispose(&images);
    return 0;
}

int main(void) {
    CHECK(test_exact_revision_and_prepare() == 0);
    CHECK(test_revision_rejections() == 0);
    CHECK(test_missing_and_duplicate_rel() == 0);
    CHECK(test_invalid_and_truncated_ranges() == 0);
    CHECK(test_dol_and_rel_limits() == 0);
    CHECK(test_raw_rel_prepare_success() == 0);
    CHECK(test_callback_short_and_failure() == 0);
    CHECK(test_rel_decode_failure_and_partial_dispose() == 0);
    CHECK(test_rejects_zero_output_yaz0_rel() == 0);

    printf("acgc boot source tests passed\n");
    return 0;
}
