#ifndef JAUDIO_NES_PC_AUDIOCMD_H
#define JAUDIO_NES_PC_AUDIOCMD_H

#include "types.h"

#ifdef TARGET_PC

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Acmd remains the fixed 8-byte GameCube command record. TARGET_PC keeps
 * native pointers beside that record and keys them by the record address so
 * the RSP simulator never reconstructs a host pointer from a truncated word.
 */
BOOL pc_audio_command_set_native_ptr(const void* command, const void* pointer);
BOOL pc_audio_command_get_native_ptr(const void* command, const void** pointer);

#ifdef __cplusplus
}
#endif

#define AUDIO_COMMAND_WRITE_POINTER(command, pointer)                     \
    do {                                                                   \
        uintptr_t _audio_native_pointer = (uintptr_t)(pointer);            \
        (command)->words.w1 = (u32)_audio_native_pointer;                  \
        pc_audio_command_set_native_ptr((command),                        \
                                        (const void*)_audio_native_pointer); \
    } while (0)

#else

#define AUDIO_COMMAND_WRITE_POINTER(command, pointer)                     \
    do {                                                                   \
        (command)->words.w1 = (u32)(pointer);                              \
    } while (0)

#endif

#endif
