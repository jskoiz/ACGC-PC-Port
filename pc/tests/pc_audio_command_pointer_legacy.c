/* Keep this compile-only check independent of the host-incompatible MSL_C headers. */
#define TYPES_H

#include "PR/mbi.h"
#include "PR/abi.h"
#include "jaudio_NES/audiocommon.h"

int main(void) {
    Acmd command = {};
    void* pointer = (void*)0x12345678;

    aLoadBuffer2(&command, pointer, 0xC40, 16);
    if (sizeof(Acmd) != 8 || command.words.w1 != (u32)(unsigned long)pointer) {
        return 1;
    }

    aADPCMdec(&command, 0, pointer);
    aResample(&command, 0, 0x8000, pointer);
    aSetLoop(&command, pointer);
    aLoadADPCM(&command, 16, pointer);
    return command.words.w1 != (u32)(unsigned long)pointer;
}
