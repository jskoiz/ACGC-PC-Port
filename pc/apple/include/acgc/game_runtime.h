#ifndef ACGC_GAME_RUNTIME_H
#define ACGC_GAME_RUNTIME_H

#include "acgc/boot_source.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AcgcGameRuntime AcgcGameRuntime;

/*
 * A renderer-neutral result from one game step. The Apple runtime does not
 * interpret the payload or name a graphics API; a platform submission hook
 * owns that decision. A NULL data pointer means that the step produced no
 * submission.
 */
typedef struct AcgcGameRuntimeSubmission {
    const void* data;
    size_t size;
    uint64_t frame_index;
} AcgcGameRuntimeSubmission;

/*
 * These hooks are the explicit closure boundary for the still-unported boot,
 * main, game, and renderer systems. The runtime borrows the hook context and
 * the prepared images during callbacks, but retains and disposes the image
 * buffers itself after create() succeeds.
 */
typedef int (*AcgcGameRuntimeInitializeCallback)(
    void* context,
    const AcgcBootSourceImages* images
);
typedef int (*AcgcGameRuntimeStepCallback)(
    void* context,
    uint64_t frame_index,
    AcgcGameRuntimeSubmission* submission
);
typedef int (*AcgcGameRuntimeSubmitCallback)(
    void* context,
    const AcgcGameRuntimeSubmission* submission
);
typedef void (*AcgcGameRuntimeDisposeCallback)(void* context);

typedef struct AcgcGameRuntimeHooks {
    void* context;
    AcgcGameRuntimeInitializeCallback initialize;
    AcgcGameRuntimeStepCallback step;
    AcgcGameRuntimeSubmitCallback submit;
    AcgcGameRuntimeDisposeCallback dispose;
} AcgcGameRuntimeHooks;

typedef enum AcgcGameRuntimeStatus {
    ACGC_GAME_RUNTIME_OK = 0,
    ACGC_GAME_RUNTIME_INVALID_ARGUMENT,
    ACGC_GAME_RUNTIME_ALLOCATION_FAILED,
    ACGC_GAME_RUNTIME_ALREADY_INITIALIZED,
    ACGC_GAME_RUNTIME_INITIALIZE_FAILED,
    ACGC_GAME_RUNTIME_NOT_INITIALIZED,
    ACGC_GAME_RUNTIME_STEP_FAILED,
    ACGC_GAME_RUNTIME_SUBMISSION_FAILED
} AcgcGameRuntimeStatus;

/*
 * Move prepared DOL/REL ownership into a newly allocated Apple runtime. The
 * output slot must point to NULL; an occupied slot is rejected so an existing
 * runtime cannot be orphaned. The source image object and output slot remain
 * untouched on failure; on success the image object is zeroed and must not be
 * disposed by the caller.
 */
AcgcGameRuntimeStatus acgc_game_runtime_create(
    AcgcGameRuntime** runtime,
    AcgcBootSourceImages* prepared_images,
    const AcgcGameRuntimeHooks* hooks
);

/* Run the downstream initialization hook exactly once. A failed attempt still
 * establishes cleanup responsibility for dispose(). */
AcgcGameRuntimeStatus acgc_game_runtime_initialize(
    AcgcGameRuntime* runtime
);

/* Run exactly one nonblocking downstream step and optional submission. */
AcgcGameRuntimeStatus acgc_game_runtime_step(
    AcgcGameRuntime* runtime
);

/* Dispose downstream state and the retained DOL/REL buffers. NULL is safe. */
void acgc_game_runtime_dispose(AcgcGameRuntime* runtime);

/* Number of successfully completed nonblocking steps. */
uint64_t acgc_game_runtime_frame_count(const AcgcGameRuntime* runtime);

const char* acgc_game_runtime_status_string(AcgcGameRuntimeStatus status);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_GAME_RUNTIME_H */
