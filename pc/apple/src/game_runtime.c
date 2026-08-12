#include "acgc/game_runtime.h"

#include <stdlib.h>
#include <string.h>

struct AcgcGameRuntime {
    AcgcBootSourceImages images;
    AcgcGameRuntimeHooks hooks;
    uint64_t frame_count;
    int initialize_called;
    int initialized;
    int downstream_disposed;
};

static int prepared_images_are_valid(const AcgcBootSourceImages* images) {
    return images != NULL &&
           images->dol_data != NULL &&
           images->manifest.dol_size > 0 &&
           images->rel_data != NULL &&
           images->rel_size > 0;
}

AcgcGameRuntimeStatus acgc_game_runtime_create(
    AcgcGameRuntime** runtime,
    AcgcBootSourceImages* prepared_images,
    const AcgcGameRuntimeHooks* hooks
) {
    AcgcGameRuntime* created;

    if (runtime == NULL || *runtime != NULL) {
        return ACGC_GAME_RUNTIME_INVALID_ARGUMENT;
    }
    if (!prepared_images_are_valid(prepared_images) ||
        hooks == NULL || hooks->initialize == NULL || hooks->step == NULL) {
        return ACGC_GAME_RUNTIME_INVALID_ARGUMENT;
    }

    created = (AcgcGameRuntime*)calloc(1, sizeof(*created));
    if (created == NULL) {
        return ACGC_GAME_RUNTIME_ALLOCATION_FAILED;
    }
    created->images = *prepared_images;
    created->hooks = *hooks;
    memset(prepared_images, 0, sizeof(*prepared_images));
    *runtime = created;
    return ACGC_GAME_RUNTIME_OK;
}

AcgcGameRuntimeStatus acgc_game_runtime_initialize(
    AcgcGameRuntime* runtime
) {
    if (runtime == NULL) {
        return ACGC_GAME_RUNTIME_INVALID_ARGUMENT;
    }
    if (runtime->initialize_called) {
        return runtime->initialized
            ? ACGC_GAME_RUNTIME_ALREADY_INITIALIZED
            : ACGC_GAME_RUNTIME_INITIALIZE_FAILED;
    }
    runtime->initialize_called = 1;
    if (!runtime->hooks.initialize(
            runtime->hooks.context,
            &runtime->images
        )) {
        return ACGC_GAME_RUNTIME_INITIALIZE_FAILED;
    }
    runtime->initialized = 1;
    return ACGC_GAME_RUNTIME_OK;
}

AcgcGameRuntimeStatus acgc_game_runtime_step(
    AcgcGameRuntime* runtime
) {
    AcgcGameRuntimeSubmission submission = { 0 };
    const uint64_t frame_index = runtime != NULL ? runtime->frame_count : 0;

    if (runtime == NULL) {
        return ACGC_GAME_RUNTIME_INVALID_ARGUMENT;
    }
    if (!runtime->initialized) {
        return ACGC_GAME_RUNTIME_NOT_INITIALIZED;
    }
    if (!runtime->hooks.step(
            runtime->hooks.context,
            frame_index,
            &submission
        )) {
        return ACGC_GAME_RUNTIME_STEP_FAILED;
    }
    submission.frame_index = frame_index;
    if ((submission.data == NULL) != (submission.size == 0)) {
        return ACGC_GAME_RUNTIME_SUBMISSION_FAILED;
    }
    if (submission.data != NULL) {
        if (runtime->hooks.submit == NULL ||
            !runtime->hooks.submit(
                runtime->hooks.context,
                &submission
            )) {
            return ACGC_GAME_RUNTIME_SUBMISSION_FAILED;
        }
    }
    runtime->frame_count++;
    return ACGC_GAME_RUNTIME_OK;
}

void acgc_game_runtime_dispose(AcgcGameRuntime* runtime) {
    if (runtime == NULL) {
        return;
    }
    if (runtime->initialize_called && !runtime->downstream_disposed) {
        if (runtime->hooks.dispose != NULL) {
            runtime->hooks.dispose(runtime->hooks.context);
        }
        runtime->downstream_disposed = 1;
    }
    acgc_boot_source_dispose(&runtime->images);
    memset(&runtime->hooks, 0, sizeof(runtime->hooks));
    free(runtime);
}

uint64_t acgc_game_runtime_frame_count(const AcgcGameRuntime* runtime) {
    return runtime != NULL ? runtime->frame_count : 0;
}

const char* acgc_game_runtime_status_string(AcgcGameRuntimeStatus status) {
    switch (status) {
        case ACGC_GAME_RUNTIME_OK: return "ok";
        case ACGC_GAME_RUNTIME_INVALID_ARGUMENT: return "invalid argument";
        case ACGC_GAME_RUNTIME_ALLOCATION_FAILED: return "allocation failed";
        case ACGC_GAME_RUNTIME_ALREADY_INITIALIZED: return "already initialized";
        case ACGC_GAME_RUNTIME_INITIALIZE_FAILED: return "initialization failed";
        case ACGC_GAME_RUNTIME_NOT_INITIALIZED: return "not initialized";
        case ACGC_GAME_RUNTIME_STEP_FAILED: return "step failed";
        case ACGC_GAME_RUNTIME_SUBMISSION_FAILED: return "submission failed";
    }
    return "unknown runtime status";
}
