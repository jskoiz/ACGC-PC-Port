#include "acgc/boot_source.h"

#include <stdlib.h>
#include <string.h>

static const uint8_t supported_revision[ACGC_BOOT_SOURCE_REVISION_SIZE] = {
    'G', 'A', 'F', 'E', '0', '1', 0, 0
};

typedef struct AcgcBootSourceFstCapture {
    uint32_t file_count;
    uint32_t rel_count;
    uint32_t rel_offset;
    uint32_t rel_size;
} AcgcBootSourceFstCapture;

static AcgcBootSourceStatus map_disc_status(AcgcDiscStatus status) {
    switch (status) {
        case ACGC_DISC_OK:
            return ACGC_BOOT_SOURCE_OK;
        case ACGC_DISC_INVALID_ARGUMENT:
            return ACGC_BOOT_SOURCE_INVALID_ARGUMENT;
        case ACGC_DISC_TRUNCATED_INPUT:
            return ACGC_BOOT_SOURCE_TRUNCATED_INPUT;
        case ACGC_DISC_READ_FAILED:
            return ACGC_BOOT_SOURCE_READ_FAILED;
        case ACGC_DISC_INVALID_HEADER:
            return ACGC_BOOT_SOURCE_INVALID_HEADER;
        case ACGC_DISC_INVALID_RANGE:
            return ACGC_BOOT_SOURCE_INVALID_RANGE;
        case ACGC_DISC_LIMIT_EXCEEDED:
            return ACGC_BOOT_SOURCE_LIMIT_EXCEEDED;
        case ACGC_DISC_CALLBACK_FAILED:
            return ACGC_BOOT_SOURCE_CALLBACK_FAILED;
        case ACGC_DISC_ALLOCATION_FAILED:
            return ACGC_BOOT_SOURCE_ALLOCATION_FAILED;
        case ACGC_DISC_EMPTY_INPUT:
            return ACGC_BOOT_SOURCE_EMPTY_REL;
        default:
            return ACGC_BOOT_SOURCE_INVALID_ARGUMENT;
    }
}

static AcgcBootSourceStatus map_rel_status(AcgcDiscStatus status) {
    switch (status) {
        case ACGC_DISC_OK:
            return ACGC_BOOT_SOURCE_OK;
        case ACGC_DISC_INVALID_ARGUMENT:
            return ACGC_BOOT_SOURCE_INVALID_ARGUMENT;
        case ACGC_DISC_TRUNCATED_INPUT:
            return ACGC_BOOT_SOURCE_REL_DECODE_FAILED;
        case ACGC_DISC_READ_FAILED:
            return ACGC_BOOT_SOURCE_READ_FAILED;
        case ACGC_DISC_INVALID_HEADER:
            return ACGC_BOOT_SOURCE_REL_DECODE_FAILED;
        case ACGC_DISC_INVALID_RANGE:
            return ACGC_BOOT_SOURCE_INVALID_RANGE;
        case ACGC_DISC_LIMIT_EXCEEDED:
            return ACGC_BOOT_SOURCE_REL_OUTPUT_LIMIT_EXCEEDED;
        case ACGC_DISC_CALLBACK_FAILED:
            return ACGC_BOOT_SOURCE_CALLBACK_FAILED;
        case ACGC_DISC_ALLOCATION_FAILED:
            return ACGC_BOOT_SOURCE_ALLOCATION_FAILED;
        case ACGC_DISC_EMPTY_INPUT:
            return ACGC_BOOT_SOURCE_EMPTY_REL;
        default:
            return ACGC_BOOT_SOURCE_REL_DECODE_FAILED;
    }
}

static AcgcBootSourceStatus read_revision(
    const AcgcDiscReader* reader,
    uint8_t revision[ACGC_BOOT_SOURCE_REVISION_SIZE]
) {
    if (reader == NULL || reader->read == NULL || revision == NULL) {
        return ACGC_BOOT_SOURCE_INVALID_ARGUMENT;
    }
    if (reader->size < ACGC_BOOT_SOURCE_REVISION_SIZE) {
        return ACGC_BOOT_SOURCE_TRUNCATED_INPUT;
    }
    if (!reader->read(
            reader->context,
            0,
            revision,
            ACGC_BOOT_SOURCE_REVISION_SIZE
        )) {
        return ACGC_BOOT_SOURCE_READ_FAILED;
    }
    if (memcmp(
            revision,
            supported_revision,
            ACGC_BOOT_SOURCE_REVISION_SIZE
        ) != 0) {
        return ACGC_BOOT_SOURCE_UNSUPPORTED_REVISION;
    }
    return ACGC_BOOT_SOURCE_OK;
}

static int capture_fst_file(
    void* context,
    const char* path,
    uint32_t offset,
    uint32_t size
) {
    AcgcBootSourceFstCapture* capture =
        (AcgcBootSourceFstCapture*)context;

    if (capture->file_count == UINT32_MAX) {
        return 0;
    }
    capture->file_count++;
    if (strcmp(path, "foresta.rel.szs") == 0) {
        if (capture->rel_count == UINT32_MAX) {
            return 0;
        }
        capture->rel_count++;
        capture->rel_offset = offset;
        capture->rel_size = size;
    }
    return 1;
}

void acgc_boot_source_limits_default(AcgcBootSourceLimits* limits) {
    if (limits == NULL) {
        return;
    }
    limits->max_dol_size = ACGC_BOOT_SOURCE_DEFAULT_MAX_DOL_SIZE;
    limits->rel.max_input_size = ACGC_DISC_DEFAULT_REL_MAX_INPUT_SIZE;
    limits->rel.max_output_size = ACGC_DISC_DEFAULT_REL_MAX_OUTPUT_SIZE;
}

AcgcBootSourceStatus acgc_boot_source_inspect(
    const AcgcDiscReader* reader,
    AcgcBootSourceManifest* manifest
) {
    AcgcBootSourceManifest parsed = { 0 };
    AcgcBootSourceFstCapture capture = { 0 };
    AcgcGcmInfo gcm;
    AcgcDiscStatus disc_status;
    AcgcBootSourceStatus status;
    uint8_t revision[ACGC_BOOT_SOURCE_REVISION_SIZE];

    if (manifest == NULL) {
        return ACGC_BOOT_SOURCE_INVALID_ARGUMENT;
    }
    memset(manifest, 0, sizeof(*manifest));

    status = read_revision(reader, revision);
    if (status != ACGC_BOOT_SOURCE_OK) {
        return status;
    }

    disc_status = acgc_gcm_parse(reader, &gcm);
    status = map_disc_status(disc_status);
    if (status != ACGC_BOOT_SOURCE_OK) {
        return status;
    }

    disc_status = acgc_dol_get_size(reader, gcm.dol_offset, &parsed.dol_size);
    status = map_disc_status(disc_status);
    if (status != ACGC_BOOT_SOURCE_OK) {
        return status;
    }

    disc_status = acgc_fst_visit(
        reader,
        &gcm,
        capture_fst_file,
        &capture
    );
    status = map_disc_status(disc_status);
    if (status != ACGC_BOOT_SOURCE_OK) {
        return status;
    }
    if (capture.rel_count == 0) {
        return ACGC_BOOT_SOURCE_MISSING_REL;
    }
    if (capture.rel_count != 1) {
        return ACGC_BOOT_SOURCE_DUPLICATE_REL;
    }

    memcpy(
        parsed.revision,
        revision,
        sizeof(parsed.revision)
    );
    parsed.dol_offset = gcm.dol_offset;
    parsed.fst_file_count = capture.file_count;
    parsed.rel_input_offset = capture.rel_offset;
    parsed.rel_input_size = capture.rel_size;
    *manifest = parsed;
    return ACGC_BOOT_SOURCE_OK;
}

static AcgcBootSourceStatus read_loaded_range(
    const AcgcDiscReader* reader,
    uint32_t offset,
    uint32_t size,
    uint8_t* destination
) {
    if (reader == NULL || reader->read == NULL || destination == NULL) {
        return ACGC_BOOT_SOURCE_INVALID_ARGUMENT;
    }
    if (offset > reader->size || size > reader->size - offset) {
        return ACGC_BOOT_SOURCE_INVALID_RANGE;
    }
    if ((uint64_t)size > (uint64_t)SIZE_MAX) {
        return ACGC_BOOT_SOURCE_LIMIT_EXCEEDED;
    }
    if (!reader->read(reader->context, offset, destination, (size_t)size)) {
        return ACGC_BOOT_SOURCE_READ_FAILED;
    }
    return ACGC_BOOT_SOURCE_OK;
}

AcgcBootSourceStatus acgc_boot_source_prepare(
    const AcgcDiscReader* reader,
    const AcgcBootSourceLimits* limits,
    AcgcBootSourceImages* images
) {
    AcgcBootSourceLimits effective_limits;
    AcgcBootSourceManifest manifest;
    AcgcBootSourceImages prepared = { 0 };
    AcgcBootSourceStatus status;
    AcgcDiscStatus disc_status;
    uint8_t* dol_data = NULL;
    uint8_t* rel_data = NULL;
    uint32_t rel_size = 0;
    AcgcRelFormat rel_format = ACGC_REL_RAW;

    if (images == NULL) {
        return ACGC_BOOT_SOURCE_INVALID_ARGUMENT;
    }
    memset(images, 0, sizeof(*images));

    if (limits == NULL) {
        acgc_boot_source_limits_default(&effective_limits);
    } else {
        effective_limits = *limits;
    }
    if (effective_limits.max_dol_size > ACGC_BOOT_SOURCE_MAX_DOL_SIZE) {
        effective_limits.max_dol_size = ACGC_BOOT_SOURCE_MAX_DOL_SIZE;
    }
    if (effective_limits.rel.max_input_size >
        ACGC_DISC_DEFAULT_REL_MAX_INPUT_SIZE) {
        effective_limits.rel.max_input_size =
            ACGC_DISC_DEFAULT_REL_MAX_INPUT_SIZE;
    }
    if (effective_limits.rel.max_output_size >
        ACGC_DISC_DEFAULT_REL_MAX_OUTPUT_SIZE) {
        effective_limits.rel.max_output_size =
            ACGC_DISC_DEFAULT_REL_MAX_OUTPUT_SIZE;
    }

    status = acgc_boot_source_inspect(reader, &manifest);
    if (status != ACGC_BOOT_SOURCE_OK) {
        return status;
    }
    if (manifest.dol_size > effective_limits.max_dol_size) {
        return ACGC_BOOT_SOURCE_DOL_LIMIT_EXCEEDED;
    }
    if (manifest.rel_input_size > effective_limits.rel.max_input_size) {
        return ACGC_BOOT_SOURCE_REL_INPUT_LIMIT_EXCEEDED;
    }
    if ((uint64_t)manifest.dol_size > (uint64_t)SIZE_MAX) {
        return ACGC_BOOT_SOURCE_DOL_LIMIT_EXCEEDED;
    }

    dol_data = (uint8_t*)malloc((size_t)manifest.dol_size);
    if (dol_data == NULL) {
        return ACGC_BOOT_SOURCE_ALLOCATION_FAILED;
    }
    status = read_loaded_range(
        reader,
        manifest.dol_offset,
        manifest.dol_size,
        dol_data
    );
    if (status != ACGC_BOOT_SOURCE_OK) {
        free(dol_data);
        return status;
    }

    disc_status = acgc_rel_extract(
        reader,
        manifest.rel_input_offset,
        manifest.rel_input_size,
        &effective_limits.rel,
        &rel_data,
        &rel_size,
        &rel_format
    );
    status = map_rel_status(disc_status);
    if (status != ACGC_BOOT_SOURCE_OK) {
        free(dol_data);
        free(rel_data);
        return status;
    }
    if (rel_size == 0 || rel_data == NULL) {
        free(dol_data);
        free(rel_data);
        return ACGC_BOOT_SOURCE_EMPTY_REL;
    }

    prepared.manifest = manifest;
    prepared.dol_data = dol_data;
    prepared.rel_data = rel_data;
    prepared.rel_size = rel_size;
    prepared.rel_format = rel_format;
    *images = prepared;
    return ACGC_BOOT_SOURCE_OK;
}

void acgc_boot_source_dispose(AcgcBootSourceImages* images) {
    if (images == NULL) {
        return;
    }
    free(images->dol_data);
    free(images->rel_data);
    memset(images, 0, sizeof(*images));
}

const char* acgc_boot_source_status_string(AcgcBootSourceStatus status) {
    switch (status) {
        case ACGC_BOOT_SOURCE_OK:
            return "ok";
        case ACGC_BOOT_SOURCE_INVALID_ARGUMENT:
            return "invalid argument";
        case ACGC_BOOT_SOURCE_UNSUPPORTED_REVISION:
            return "unsupported revision";
        case ACGC_BOOT_SOURCE_MISSING_REL:
            return "missing foresta.rel.szs";
        case ACGC_BOOT_SOURCE_DUPLICATE_REL:
            return "duplicate foresta.rel.szs";
        case ACGC_BOOT_SOURCE_DOL_LIMIT_EXCEEDED:
            return "DOL limit exceeded";
        case ACGC_BOOT_SOURCE_REL_INPUT_LIMIT_EXCEEDED:
            return "REL input limit exceeded";
        case ACGC_BOOT_SOURCE_REL_OUTPUT_LIMIT_EXCEEDED:
            return "REL output limit exceeded";
        case ACGC_BOOT_SOURCE_REL_DECODE_FAILED:
            return "REL decode failed";
        case ACGC_BOOT_SOURCE_TRUNCATED_INPUT:
            return "truncated input";
        case ACGC_BOOT_SOURCE_READ_FAILED:
            return "read failed";
        case ACGC_BOOT_SOURCE_INVALID_HEADER:
            return "invalid header";
        case ACGC_BOOT_SOURCE_INVALID_RANGE:
            return "invalid range";
        case ACGC_BOOT_SOURCE_LIMIT_EXCEEDED:
            return "limit exceeded";
        case ACGC_BOOT_SOURCE_CALLBACK_FAILED:
            return "callback failed";
        case ACGC_BOOT_SOURCE_ALLOCATION_FAILED:
            return "allocation failed";
        case ACGC_BOOT_SOURCE_EMPTY_REL:
            return "empty REL";
        default:
            return "unknown error";
    }
}
