#include "acgc/bytes.h"
#include "acgc/disc.h"
#include "acgc/yaz0.h"

#include <stdlib.h>
#include <string.h>

#define ACGC_GC_MAGIC UINT32_C(0xC2339F3D)
#define ACGC_CISO_MAGIC UINT32_C(0x4F534943)

static int ciso_checked_add(uint64_t left, uint64_t right, uint64_t* result) {
    if (left > UINT64_MAX - right) {
        return 0;
    }
    *result = left + right;
    return 1;
}

static int ciso_checked_mul(uint64_t left, uint64_t right, uint64_t* result) {
    if (left != 0 && right > UINT64_MAX / left) {
        return 0;
    }
    *result = left * right;
    return 1;
}

static AcgcDiscStatus ciso_validate_map(const AcgcCisoMap* map) {
    uint64_t expected_logical_size;
    uint64_t expected_present_blocks = 0;
    uint64_t block;

    if (map == NULL || map->physical_offsets == NULL) {
        return ACGC_DISC_INVALID_ARGUMENT;
    }
    if (map->block_size == 0 ||
        map->block_size > ACGC_DISC_CISO_MAX_BLOCK_SIZE ||
        map->block_count == 0 ||
        map->block_count > ACGC_DISC_CISO_MAX_BLOCKS ||
        map->present_block_count > map->block_count) {
        return ACGC_DISC_INVALID_RANGE;
    }
    if (!ciso_checked_mul(
            map->block_size,
            map->block_count,
            &expected_logical_size
        ) || expected_logical_size != map->logical_size) {
        return ACGC_DISC_INVALID_RANGE;
    }
    if (map->physical_size < ACGC_DISC_CISO_HEADER_SIZE) {
        return ACGC_DISC_INVALID_RANGE;
    }

    for (block = 0; block < map->block_count; block++) {
        uint64_t physical_offset = map->physical_offsets[(size_t)block];
        uint64_t physical_end;

        if (physical_offset == ACGC_DISC_CISO_SPARSE_OFFSET) {
            continue;
        }
        if (physical_offset < ACGC_DISC_CISO_HEADER_SIZE ||
            !ciso_checked_add(
                physical_offset,
                map->block_size,
                &physical_end
            ) ||
            physical_end > map->physical_size) {
            return ACGC_DISC_INVALID_RANGE;
        }
        expected_present_blocks++;
    }

    if (expected_present_blocks != map->present_block_count) {
        return ACGC_DISC_INVALID_RANGE;
    }
    return ACGC_DISC_OK;
}

AcgcDiscStatus acgc_ciso_parse(
    const uint8_t* header,
    size_t header_size,
    uint64_t physical_size,
    AcgcCisoMap* map
) {
    AcgcCisoMap parsed = { 0 };
    uint64_t block_size;
    uint64_t block_count = 0;
    uint64_t present_block_count = 0;
    uint64_t logical_size;
    uint64_t physical_cursor;
    size_t block;

    if (header == NULL || map == NULL) {
        return ACGC_DISC_INVALID_ARGUMENT;
    }
    if (header_size < (size_t)ACGC_DISC_CISO_HEADER_SIZE) {
        return ACGC_DISC_TRUNCATED_INPUT;
    }
    if (acgc_load_le32(header) != ACGC_CISO_MAGIC) {
        return ACGC_DISC_INVALID_HEADER;
    }

    block_size = (uint64_t)acgc_load_le32(header + 4);
    if (block_size == 0 || block_size > ACGC_DISC_CISO_MAX_BLOCK_SIZE) {
        return ACGC_DISC_INVALID_HEADER;
    }

    for (block = 0; block < (size_t)ACGC_DISC_CISO_MAP_SIZE; block++) {
        uint8_t map_entry = header[(size_t)ACGC_DISC_CISO_MAP_OFFSET + block];

        if (map_entry > 1) {
            return ACGC_DISC_INVALID_HEADER;
        }
        if (map_entry != 0) {
            block_count = (uint64_t)block + UINT64_C(1);
            present_block_count++;
        }
    }
    if (block_count == 0) {
        return ACGC_DISC_INVALID_HEADER;
    }
    if (!ciso_checked_mul(block_size, block_count, &logical_size)) {
        return ACGC_DISC_INVALID_RANGE;
    }

    physical_cursor = ACGC_DISC_CISO_HEADER_SIZE;
    for (block = 0; block < (size_t)block_count; block++) {
        if (header[(size_t)ACGC_DISC_CISO_MAP_OFFSET + block] != 0) {
            if (!ciso_checked_add(
                    physical_cursor,
                    block_size,
                    &physical_cursor
                )) {
                return ACGC_DISC_INVALID_RANGE;
            }
        }
    }
    if (physical_size < ACGC_DISC_CISO_HEADER_SIZE ||
        physical_cursor > physical_size) {
        return ACGC_DISC_TRUNCATED_INPUT;
    }
    if (block_count > SIZE_MAX / sizeof(uint64_t)) {
        return ACGC_DISC_LIMIT_EXCEEDED;
    }

    parsed.physical_offsets = (uint64_t*)malloc(
        (size_t)block_count * sizeof(*parsed.physical_offsets)
    );
    if (parsed.physical_offsets == NULL) {
        return ACGC_DISC_ALLOCATION_FAILED;
    }

    physical_cursor = ACGC_DISC_CISO_HEADER_SIZE;
    for (block = 0; block < (size_t)block_count; block++) {
        if (header[(size_t)ACGC_DISC_CISO_MAP_OFFSET + block] == 0) {
            parsed.physical_offsets[block] = ACGC_DISC_CISO_SPARSE_OFFSET;
        } else {
            parsed.physical_offsets[block] = physical_cursor;
            if (!ciso_checked_add(
                    physical_cursor,
                    block_size,
                    &physical_cursor
                )) {
                free(parsed.physical_offsets);
                return ACGC_DISC_INVALID_RANGE;
            }
        }
    }

    parsed.block_size = block_size;
    parsed.block_count = block_count;
    parsed.present_block_count = present_block_count;
    parsed.logical_size = logical_size;
    parsed.physical_size = physical_size;
    *map = parsed;
    return ACGC_DISC_OK;
}

void acgc_ciso_dispose(AcgcCisoMap* map) {
    if (map == NULL) {
        return;
    }
    free(map->physical_offsets);
    memset(map, 0, sizeof(*map));
}

AcgcDiscStatus acgc_ciso_plan_chunk(
    const AcgcCisoMap* map,
    uint64_t logical_offset,
    uint64_t requested_size,
    AcgcCisoReadPlan* plan
) {
    AcgcDiscStatus status;
    uint64_t block_index;
    uint64_t block_offset;
    uint64_t block_remaining;
    uint64_t physical_offset;

    if (plan == NULL) {
        return ACGC_DISC_INVALID_ARGUMENT;
    }
    memset(plan, 0, sizeof(*plan));

    status = ciso_validate_map(map);
    if (status != ACGC_DISC_OK) {
        return status;
    }
    if (logical_offset > map->logical_size ||
        requested_size > map->logical_size - logical_offset) {
        return ACGC_DISC_INVALID_RANGE;
    }
    if (requested_size == 0) {
        return ACGC_DISC_OK;
    }

    block_index = logical_offset / map->block_size;
    block_offset = logical_offset % map->block_size;
    block_remaining = map->block_size - block_offset;
    plan->size = requested_size < block_remaining ?
        requested_size : block_remaining;

    physical_offset = map->physical_offsets[(size_t)block_index];
    if (physical_offset == ACGC_DISC_CISO_SPARSE_OFFSET) {
        plan->sparse = 1;
        return ACGC_DISC_OK;
    }
    if (!ciso_checked_add(physical_offset, block_offset, &physical_offset) ||
        plan->size > map->physical_size - physical_offset) {
        memset(plan, 0, sizeof(*plan));
        return ACGC_DISC_INVALID_RANGE;
    }
    plan->physical_offset = physical_offset;
    return ACGC_DISC_OK;
}

AcgcDiscStatus acgc_ciso_read(
    const AcgcCisoMap* map,
    uint64_t logical_offset,
    uint64_t size,
    void* destination,
    AcgcCisoReadFn read,
    void* context
) {
    uint8_t* output = (uint8_t*)destination;
    uint64_t remaining = size;
    AcgcCisoReadPlan validation_plan;
    AcgcDiscStatus status;

    if (size != 0 && (destination == NULL || read == NULL)) {
        return ACGC_DISC_INVALID_ARGUMENT;
    }
    if (size > (uint64_t)SIZE_MAX) {
        return ACGC_DISC_LIMIT_EXCEEDED;
    }

    status = acgc_ciso_plan_chunk(
        map,
        logical_offset,
        size,
        &validation_plan
    );
    if (status != ACGC_DISC_OK) {
        return status;
    }

    while (remaining != 0) {
        AcgcCisoReadPlan plan;
        size_t chunk_size;

        status = acgc_ciso_plan_chunk(
            map,
            logical_offset,
            remaining,
            &plan
        );
        if (status != ACGC_DISC_OK) {
            return status;
        }
        if (plan.size == 0 || plan.size > (uint64_t)SIZE_MAX) {
            return ACGC_DISC_LIMIT_EXCEEDED;
        }
        chunk_size = (size_t)plan.size;
        if (plan.sparse) {
            memset(output, 0, chunk_size);
        } else if (!read(
                context,
                plan.physical_offset,
                output,
                chunk_size
            )) {
            return ACGC_DISC_READ_FAILED;
        }

        output += chunk_size;
        remaining -= plan.size;
        if (!ciso_checked_add(
                logical_offset,
                plan.size,
                &logical_offset
            )) {
            return ACGC_DISC_INVALID_RANGE;
        }
    }
    return ACGC_DISC_OK;
}

static AcgcDiscStatus validate_reader(const AcgcDiscReader* reader) {
    if (reader == NULL || reader->read == NULL) {
        return ACGC_DISC_INVALID_ARGUMENT;
    }
    return ACGC_DISC_OK;
}

static AcgcDiscStatus validate_range(
    const AcgcDiscReader* reader,
    uint32_t offset,
    uint32_t size
) {
    AcgcDiscStatus status = validate_reader(reader);

    if (status != ACGC_DISC_OK) {
        return status;
    }
    if (offset > reader->size || size > reader->size - offset) {
        return ACGC_DISC_INVALID_RANGE;
    }
    return ACGC_DISC_OK;
}

static AcgcDiscStatus read_range(
    const AcgcDiscReader* reader,
    uint32_t offset,
    void* destination,
    uint32_t size
) {
    AcgcDiscStatus status = validate_range(reader, offset, size);

    if (status != ACGC_DISC_OK) {
        return status;
    }
    if (size == 0) {
        return ACGC_DISC_OK;
    }
    if (destination == NULL) {
        return ACGC_DISC_INVALID_ARGUMENT;
    }
    if (!reader->read(reader->context, offset, destination, (size_t)size)) {
        return ACGC_DISC_READ_FAILED;
    }
    return ACGC_DISC_OK;
}

static AcgcDiscStatus read_header(
    const AcgcDiscReader* reader,
    uint32_t offset,
    void* destination,
    uint32_t size
) {
    AcgcDiscStatus status = validate_reader(reader);

    if (status != ACGC_DISC_OK) {
        return status;
    }
    if (offset > reader->size || size > reader->size - offset) {
        return ACGC_DISC_TRUNCATED_INPUT;
    }
    if (!reader->read(reader->context, offset, destination, (size_t)size)) {
        return ACGC_DISC_READ_FAILED;
    }
    return ACGC_DISC_OK;
}

static int range_is_valid(
    const AcgcDiscReader* reader,
    uint32_t offset,
    uint32_t size
) {
    return validate_range(reader, offset, size) == ACGC_DISC_OK;
}

AcgcDiscStatus acgc_gcm_parse(
    const AcgcDiscReader* reader,
    AcgcGcmInfo* info
) {
    uint8_t header[ACGC_DISC_GCM_HEADER_SIZE];
    AcgcDiscStatus status;

    if (info == NULL) {
        return ACGC_DISC_INVALID_ARGUMENT;
    }
    memset(info, 0, sizeof(*info));

    status = read_header(reader, 0, header, sizeof(header));
    if (status != ACGC_DISC_OK) {
        return status;
    }
    if (acgc_load_be32(header + 0x1C) != ACGC_GC_MAGIC) {
        return ACGC_DISC_INVALID_HEADER;
    }

    info->dol_offset = acgc_load_be32(header + 0x420);
    info->fst_offset = acgc_load_be32(header + 0x424);
    info->fst_size = acgc_load_be32(header + 0x428);
    info->fst_max_size = acgc_load_be32(header + 0x42C);

    if (!range_is_valid(reader, info->dol_offset, ACGC_DISC_DOL_HEADER_SIZE)) {
        return ACGC_DISC_INVALID_RANGE;
    }
    if (info->fst_size < ACGC_DISC_FST_ENTRY_SIZE ||
        !range_is_valid(reader, info->fst_offset, info->fst_size)) {
        return ACGC_DISC_INVALID_RANGE;
    }
    if (info->fst_max_size != 0 && info->fst_max_size < info->fst_size) {
        return ACGC_DISC_INVALID_HEADER;
    }

    return ACGC_DISC_OK;
}

AcgcDiscStatus acgc_dol_get_size(
    const AcgcDiscReader* reader,
    uint32_t dol_offset,
    uint32_t* dol_size
) {
    uint8_t header[ACGC_DISC_DOL_HEADER_SIZE];
    uint32_t max_end = 0;
    AcgcDiscStatus status;
    int i;

    if (dol_size == NULL) {
        return ACGC_DISC_INVALID_ARGUMENT;
    }
    *dol_size = 0;

    status = read_header(reader, dol_offset, header, sizeof(header));
    if (status != ACGC_DISC_OK) {
        return status;
    }

    for (i = 0; i < 7; i++) {
        uint32_t section_offset = acgc_load_be32(header + i * 4);
        uint32_t section_size = acgc_load_be32(header + 0x90 + i * 4);

        if (section_size == 0) {
            continue;
        }
        if (section_offset < ACGC_DISC_DOL_HEADER_SIZE) {
            return ACGC_DISC_INVALID_RANGE;
        }
        if (section_size > UINT32_MAX - section_offset) {
            return ACGC_DISC_INVALID_RANGE;
        }
        if (section_offset + section_size > max_end) {
            max_end = section_offset + section_size;
        }
    }
    for (i = 0; i < 11; i++) {
        uint32_t section_offset = acgc_load_be32(header + 0x1C + i * 4);
        uint32_t section_size = acgc_load_be32(header + 0xAC + i * 4);

        if (section_size == 0) {
            continue;
        }
        if (section_offset < ACGC_DISC_DOL_HEADER_SIZE) {
            return ACGC_DISC_INVALID_RANGE;
        }
        if (section_size > UINT32_MAX - section_offset) {
            return ACGC_DISC_INVALID_RANGE;
        }
        if (section_offset + section_size > max_end) {
            max_end = section_offset + section_size;
        }
    }

    if (max_end > reader->size - dol_offset) {
        return ACGC_DISC_INVALID_RANGE;
    }
    if (max_end == 0) {
        return ACGC_DISC_INVALID_HEADER;
    }
    if (max_end < ACGC_DISC_DOL_HEADER_SIZE) {
        return ACGC_DISC_INVALID_RANGE;
    }

    *dol_size = max_end;
    return ACGC_DISC_OK;
}

typedef struct AcgcFstFrame {
    uint32_t entry_index;
    uint32_t next_entry;
    size_t restore_path_length;
} AcgcFstFrame;

static AcgcDiscStatus read_fst_name(
    const AcgcDiscReader* reader,
    uint32_t string_table,
    uint32_t string_table_end,
    uint32_t name_offset,
    char* name,
    size_t name_capacity
) {
    uint32_t name_position;
    uint32_t available;
    size_t to_read;
    AcgcDiscStatus status;
    size_t i;

    if (name == NULL || name_capacity < 2 ||
        name_offset > string_table_end - string_table) {
        return ACGC_DISC_INVALID_ARGUMENT;
    }
    if (name_offset == string_table_end - string_table) {
        return ACGC_DISC_INVALID_HEADER;
    }

    name_position = string_table + name_offset;
    available = string_table_end - name_position;
    to_read = (size_t)available;
    if (to_read > name_capacity) {
        to_read = name_capacity;
    }

    status = read_range(reader, name_position, name, (uint32_t)to_read);
    if (status != ACGC_DISC_OK) {
        return status;
    }
    for (i = 0; i < to_read; i++) {
        if (name[i] == '\0') {
            return ACGC_DISC_OK;
        }
    }
    if ((uint32_t)to_read < available) {
        return ACGC_DISC_LIMIT_EXCEEDED;
    }
    return ACGC_DISC_INVALID_HEADER;
}

static AcgcDiscStatus append_path_component(
    char* path,
    size_t* path_length,
    size_t path_capacity,
    const char* component
) {
    size_t component_length;
    size_t separator_length = *path_length == 0 ? 0 : 1;

    component_length = strlen(component);
    if (component_length == 0 ||
        *path_length > path_capacity - 1 ||
        separator_length > path_capacity - 1 - *path_length ||
        component_length > path_capacity - 1 - *path_length - separator_length) {
        return ACGC_DISC_LIMIT_EXCEEDED;
    }

    if (separator_length != 0) {
        path[*path_length] = '/';
        *path_length += 1;
    }
    memcpy(path + *path_length, component, component_length);
    *path_length += component_length;
    path[*path_length] = '\0';
    return ACGC_DISC_OK;
}

AcgcDiscStatus acgc_fst_visit(
    const AcgcDiscReader* reader,
    const AcgcGcmInfo* info,
    AcgcFstFileCallback callback,
    void* context
) {
    uint8_t entry[ACGC_DISC_FST_ENTRY_SIZE];
    AcgcFstFrame frames[64];
    char path[ACGC_DISC_MAX_FST_PATH_SIZE];
    uint32_t entry_count;
    uint32_t table_bytes;
    uint32_t string_table;
    uint32_t string_table_end;
    size_t path_length = 0;
    size_t depth = 0;
    AcgcDiscStatus status;
    uint32_t i;

    if (info == NULL || callback == NULL) {
        return ACGC_DISC_INVALID_ARGUMENT;
    }
    status = validate_reader(reader);
    if (status != ACGC_DISC_OK) {
        return status;
    }
    if (info->fst_size < ACGC_DISC_FST_ENTRY_SIZE ||
        !range_is_valid(reader, info->fst_offset, info->fst_size)) {
        return ACGC_DISC_INVALID_RANGE;
    }

    status = read_range(reader, info->fst_offset, entry, sizeof(entry));
    if (status != ACGC_DISC_OK) {
        return status;
    }
    if (entry[0] != 1 ||
        entry[1] != 0 || entry[2] != 0 || entry[3] != 0 ||
        acgc_load_be32(entry + 4) != 0) {
        return ACGC_DISC_INVALID_HEADER;
    }

    entry_count = acgc_load_be32(entry + 8);
    if (entry_count == 0) {
        return ACGC_DISC_INVALID_HEADER;
    }
    if (entry_count > ACGC_DISC_MAX_FST_ENTRIES) {
        return ACGC_DISC_LIMIT_EXCEEDED;
    }
    if (entry_count > info->fst_size / ACGC_DISC_FST_ENTRY_SIZE) {
        return ACGC_DISC_INVALID_RANGE;
    }

    table_bytes = entry_count * ACGC_DISC_FST_ENTRY_SIZE;
    if (info->fst_offset > UINT32_MAX - table_bytes) {
        return ACGC_DISC_INVALID_RANGE;
    }
    string_table = info->fst_offset + table_bytes;
    if (info->fst_offset > UINT32_MAX - info->fst_size) {
        return ACGC_DISC_INVALID_RANGE;
    }
    string_table_end = info->fst_offset + info->fst_size;
    if (string_table > string_table_end) {
        return ACGC_DISC_INVALID_RANGE;
    }
    path[0] = '\0';
    for (i = 1; i < entry_count; i++) {
        uint32_t name_offset;
        uint32_t file_offset;
        uint32_t file_size;
        char name[ACGC_DISC_MAX_FST_NAME_SIZE];
        size_t restore_path_length;

        while (depth > 0 && i >= frames[depth - 1].next_entry) {
            path_length = frames[depth - 1].restore_path_length;
            path[path_length] = '\0';
            depth--;
        }

        status = read_range(
            reader,
            info->fst_offset + i * ACGC_DISC_FST_ENTRY_SIZE,
            entry,
            sizeof(entry)
        );
        if (status != ACGC_DISC_OK) {
            return status;
        }
        if (entry[0] != 0 && entry[0] != 1) {
            return ACGC_DISC_INVALID_HEADER;
        }

        name_offset = ((uint32_t)entry[1] << 16) |
                      ((uint32_t)entry[2] << 8) |
                      (uint32_t)entry[3];
        status = read_fst_name(
            reader,
            string_table,
            string_table_end,
            name_offset,
            name,
            sizeof(name)
        );
        if (status != ACGC_DISC_OK) {
            return status;
        }

        restore_path_length = path_length;
        status = append_path_component(
            path,
            &path_length,
            sizeof(path),
            name
        );
        if (status != ACGC_DISC_OK) {
            return status;
        }

        if (entry[0] == 1) {
            uint32_t parent_entry = acgc_load_be32(entry + 4);
            uint32_t next_entry = acgc_load_be32(entry + 8);
            uint32_t expected_parent = depth == 0 ? 0 :
                frames[depth - 1].entry_index;

            if (parent_entry != expected_parent ||
                next_entry <= i || next_entry > entry_count ||
                (depth > 0 && next_entry > frames[depth - 1].next_entry)) {
                return ACGC_DISC_INVALID_HEADER;
            }
            if (depth >= sizeof(frames) / sizeof(frames[0])) {
                return ACGC_DISC_LIMIT_EXCEEDED;
            }
            frames[depth].entry_index = i;
            frames[depth].next_entry = next_entry;
            frames[depth].restore_path_length = restore_path_length;
            depth++;
        } else {
            file_offset = acgc_load_be32(entry + 4);
            file_size = acgc_load_be32(entry + 8);
            if (!range_is_valid(reader, file_offset, file_size)) {
                return ACGC_DISC_INVALID_RANGE;
            }
            if (!callback(context, path, file_offset, file_size)) {
                return ACGC_DISC_CALLBACK_FAILED;
            }
            path_length = restore_path_length;
            path[path_length] = '\0';
        }
    }

    return ACGC_DISC_OK;
}

static AcgcDiscStatus map_yaz0_status(AcgcYaz0Status status) {
    switch (status) {
        case ACGC_YAZ0_OK:
            return ACGC_DISC_OK;
        case ACGC_YAZ0_TRUNCATED_INPUT:
            return ACGC_DISC_TRUNCATED_INPUT;
        case ACGC_YAZ0_OUTPUT_LIMIT_EXCEEDED:
            return ACGC_DISC_LIMIT_EXCEEDED;
        case ACGC_YAZ0_ALLOCATION_FAILED:
            return ACGC_DISC_ALLOCATION_FAILED;
        case ACGC_YAZ0_INVALID_ARGUMENT:
            return ACGC_DISC_INVALID_ARGUMENT;
        default:
            return ACGC_DISC_INVALID_HEADER;
    }
}

AcgcDiscStatus acgc_rel_extract(
    const AcgcDiscReader* reader,
    uint32_t offset,
    uint32_t size,
    const AcgcRelLimits* limits,
    uint8_t** output_data,
    uint32_t* output_size,
    AcgcRelFormat* format
) {
    AcgcRelLimits effective_limits = {
        ACGC_DISC_DEFAULT_REL_MAX_INPUT_SIZE,
        ACGC_DISC_DEFAULT_REL_MAX_OUTPUT_SIZE
    };
    uint8_t magic[4];
    uint8_t* source;
    AcgcDiscStatus status;

    if (output_data == NULL || output_size == NULL) {
        return ACGC_DISC_INVALID_ARGUMENT;
    }
    *output_data = NULL;
    *output_size = 0;
    if (format != NULL) {
        *format = ACGC_REL_RAW;
    }

    if (limits != NULL) {
        effective_limits = *limits;
    }
    if (size == 0) {
        return ACGC_DISC_EMPTY_INPUT;
    }
    if (size > effective_limits.max_input_size) {
        return ACGC_DISC_LIMIT_EXCEEDED;
    }
    status = validate_range(reader, offset, size);
    if (status != ACGC_DISC_OK) {
        return status;
    }

    if (size >= sizeof(magic)) {
        status = read_range(reader, offset, magic, sizeof(magic));
        if (status != ACGC_DISC_OK) {
            return status;
        }
    } else {
        memset(magic, 0, sizeof(magic));
    }

    if (memcmp(magic, "Yaz0", sizeof(magic)) != 0 &&
        size > effective_limits.max_output_size) {
        return ACGC_DISC_LIMIT_EXCEEDED;
    }

    source = (uint8_t*)malloc((size_t)size);
    if (source == NULL) {
        return ACGC_DISC_ALLOCATION_FAILED;
    }
    status = read_range(reader, offset, source, size);
    if (status != ACGC_DISC_OK) {
        free(source);
        return status;
    }

    if (memcmp(magic, "Yaz0", sizeof(magic)) == 0) {
        uint8_t* decoded = NULL;
        uint32_t decoded_size = 0;
        AcgcYaz0Status yaz0_status;

        yaz0_status = acgc_yaz0_decode(
            source,
            (size_t)size,
            effective_limits.max_output_size,
            &decoded,
            &decoded_size
        );
        free(source);
        status = map_yaz0_status(yaz0_status);
        if (status != ACGC_DISC_OK) {
            return status;
        }
        *output_data = decoded;
        *output_size = decoded_size;
        if (format != NULL) {
            *format = ACGC_REL_YAZ0;
        }
        return ACGC_DISC_OK;
    }

    *output_data = source;
    *output_size = size;
    return ACGC_DISC_OK;
}

const char* acgc_disc_status_string(AcgcDiscStatus status) {
    switch (status) {
        case ACGC_DISC_OK:
            return "ok";
        case ACGC_DISC_INVALID_ARGUMENT:
            return "invalid argument";
        case ACGC_DISC_TRUNCATED_INPUT:
            return "truncated input";
        case ACGC_DISC_READ_FAILED:
            return "read failed";
        case ACGC_DISC_INVALID_HEADER:
            return "invalid header";
        case ACGC_DISC_INVALID_RANGE:
            return "invalid range";
        case ACGC_DISC_LIMIT_EXCEEDED:
            return "limit exceeded";
        case ACGC_DISC_CALLBACK_FAILED:
            return "callback failed";
        case ACGC_DISC_ALLOCATION_FAILED:
            return "allocation failed";
        case ACGC_DISC_EMPTY_INPUT:
            return "empty input";
        default:
            return "unknown error";
    }
}
