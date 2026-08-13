#include "dolphin/dvd.h"
#include "pc_disc.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    COPYDATE_LENGTH = 19,
    COPYDATE_DISC_OFFSET = 0x1000,
    DVD_TRANSFER_SIZE = 32,
};

static int disc_read_calls;

int pc_disc_is_open(void) {
    return 1;
}

int pc_disc_find_file(const char* path, u32* disc_offset, u32* file_size) {
    if (path == NULL || disc_offset == NULL || file_size == NULL ||
        (strcmp(path, "COPYDATE") != 0 && strcmp(path, "/COPYDATE") != 0)) {
        return 0;
    }
    *disc_offset = COPYDATE_DISC_OFFSET;
    *file_size = COPYDATE_LENGTH;
    return 1;
}

int pc_disc_read(u32 offset, void* dest, u32 size) {
    disc_read_calls++;
    if (offset != COPYDATE_DISC_OFFSET || dest == NULL || size != DVD_TRANSFER_SIZE) {
        return 0;
    }
    memset(dest, 0xA5, size);
    return 1;
}

static int fail(const char* message) {
    fprintf(stderr, "pc_dvd read-boundary probe: %s\n", message);
    return 1;
}

static int expect_read_failure(
    DVDFileInfo* file_info,
    void* buffer,
    s32 length,
    s32 offset,
    const char* message
) {
    if (DVDReadPrio(file_info, buffer, length, offset, 2) >= 0) {
        return fail(message);
    }
    return 0;
}

int main(void) {
    DVDFileInfo file_info;
    char path[] = "/COPYDATE";
    unsigned char buffer[64] __attribute__((aligned(32)));

    memset(&file_info, 0, sizeof(file_info));
    memset(buffer, 0, sizeof(buffer));

    if (!DVDOpen(path, &file_info)) {
        return fail("disc-backed COPYDATE open failed");
    }
    if (file_info.startAddr != COPYDATE_DISC_OFFSET ||
        file_info.length != COPYDATE_LENGTH) {
        return fail("COPYDATE metadata changed");
    }

    if (DVDReadPrio(&file_info, buffer, DVD_TRANSFER_SIZE, 0, 2) != DVD_TRANSFER_SIZE) {
        return fail("sector-rounded 19-byte COPYDATE read failed");
    }
    if (disc_read_calls != 1) {
        return fail("valid read did not reach the disc boundary exactly once");
    }
    for (size_t i = 0; i < DVD_TRANSFER_SIZE; ++i) {
        if (buffer[i] != 0xA5) {
            return fail("disc boundary did not return the requested sector bytes");
        }
    }
    if (file_info.cb.state != DVD_STATE_END ||
        file_info.cb.transferredSize != DVD_TRANSFER_SIZE) {
        return fail("successful read did not publish completion state");
    }

    if (expect_read_failure(&file_info, buffer, DVD_TRANSFER_SIZE * 2, 0,
                            "read beyond one trailing sector was accepted") != 0 ||
        expect_read_failure(&file_info, buffer, DVD_TRANSFER_SIZE, COPYDATE_LENGTH,
                            "offset at file end was accepted") != 0 ||
        expect_read_failure(&file_info, buffer, DVD_TRANSFER_SIZE * 2, 18,
                            "read beyond the GameCube tail allowance was accepted") != 0) {
        return 1;
    }
    if (disc_read_calls != 1) {
        return fail("malformed reads touched the disc boundary");
    }

    if (!DVDClose(&file_info)) {
        return fail("COPYDATE close failed");
    }
    printf("pc_dvd read-boundary probe: 19-byte disc read rounded to 32 bytes; malformed ranges rejected\n");
    return 0;
}
