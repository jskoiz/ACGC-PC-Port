#include "types.h"
#include "PR/mbi.h"
#include "PR/abi.h"
#include "jaudio_NES/audiocommon.h"
#include "jaudio_NES/pc_audiocmd.h"
#include "jaudio_NES/rspsim.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_report_count;
u32 JAC_FRAMESAMPLES = 560;

void OSReport(const char* format, ...) {
    (void)format;
    s_report_count++;
}

void DCTouchRange(void* address, u32 length) {
    (void)address;
    (void)length;
}

void DCZeroRange(void* address, u32 length) {
    (void)address;
    (void)length;
}

static int fail(const char* message) {
    fprintf(stderr, "pc_audio command pointer ABI probe: %s\n", message);
    return 1;
}

static int check_pointer(const Acmd* command, const void* expected) {
    const void* resolved = NULL;

    if (!pc_audio_command_get_native_ptr(command, &resolved)) {
        return 0;
    }
    return resolved == expected;
}

int main(void) {
    const size_t copy_size = 400;
    void* source = NULL;
    void* replacement = NULL;
    void* destination = NULL;
    Acmd commands[2] = {};
    Acmd missing = {};

    if (sizeof(Acmd) != 8 || sizeof(commands[0]) != 8) {
        return fail("Acmd layout changed");
    }
    if (posix_memalign(&source, 16, copy_size) != 0 ||
        posix_memalign(&replacement, 16, copy_size) != 0 ||
        posix_memalign(&destination, 16, copy_size) != 0) {
        return fail("posix_memalign failed");
    }
    if ((uintptr_t)source <= UINT32_MAX ||
        (uintptr_t)replacement <= UINT32_MAX ||
        (uintptr_t)destination <= UINT32_MAX) {
        return fail("allocator did not provide high native addresses");
    }

    memset(source, 0x31, copy_size);
    memset(replacement, 0xA7, copy_size);
    memset(destination, 0, copy_size);

    aLoadBuffer2(&commands[0], source, 0xC40, copy_size);
    aSaveBuffer2(&commands[1], destination, 0xC40, copy_size);
    if (commands[0].words.w1 != (u32)(uintptr_t)source ||
        commands[1].words.w1 != (u32)(uintptr_t)destination) {
        return fail("serialized pointer word changed");
    }
    if (!check_pointer(&commands[0], source) ||
        !check_pointer(&commands[1], destination)) {
        return fail("native pointer sidecar missing");
    }
    if (RspStart((u32*)commands, 2) != 2 ||
        memcmp(source, destination, copy_size) != 0) {
        return fail("LOADBUFFER2/SAVEBUFFER2 round trip failed");
    }

    memset(destination, 0, copy_size);
    aLoadBuffer2(&commands[0], replacement, 0xC40, copy_size);
    if (!check_pointer(&commands[0], replacement) || check_pointer(&commands[0], source)) {
        return fail("command slot replacement retained stale pointer");
    }
    if (RspStart((u32*)commands, 2) != 2 ||
        memcmp(replacement, destination, copy_size) != 0) {
        return fail("reused command slot did not copy replacement pointer");
    }

    aLoadBuffer2(&commands[0], NULL, 0xC40, copy_size);
    if (check_pointer(&commands[0], replacement)) {
        return fail("null replacement retained stale pointer");
    }
    if (RspStart((u32*)&commands[0], 1) != 1) {
        return fail("null command did not fail closed");
    }

    missing.words.w0 = _SHIFTL(A_CMD_LOADBUFFER2, 24, 8) |
                       _SHIFTL(copy_size >> 4, 16, 8) | _SHIFTL(0xC40, 0, 16);
    missing.words.w1 = (u32)(uintptr_t)source;
    if (check_pointer(&missing, source)) {
        return fail("unregistered command unexpectedly resolved");
    }
    if (RspStart((u32*)&missing, 1) != 1 || s_report_count < 2) {
        return fail("missing command did not fail closed with a diagnostic");
    }

    free(source);
    free(replacement);
    free(destination);
    puts("pc_audio command pointer ABI probe: PASS");
    return 0;
}
