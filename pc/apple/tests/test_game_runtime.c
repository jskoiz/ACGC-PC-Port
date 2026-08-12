#include "acgc/game_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

/*
 * The reconstructed boot/main/graph closure is intentionally not linked into
 * this Apple-only probe. These named doubles exercise only the unresolved
 * downstream lifecycle and renderer-neutral submission boundaries.
 */
typedef struct AcgcUnresolvedGameSystemsStub {
    int initialize_calls;
    int step_calls;
    int submit_calls;
    int dispose_calls;
    uint64_t last_frame;
} AcgcUnresolvedGameSystemsStub;

static int acgc_unresolved_game_systems_stub_initialize(
    void* context,
    const AcgcBootSourceImages* images
) {
    AcgcUnresolvedGameSystemsStub* stub =
        (AcgcUnresolvedGameSystemsStub*)context;

    if (stub == NULL || images == NULL || images->dol_data == NULL ||
        images->rel_data == NULL || images->manifest.dol_size == 0 ||
        images->rel_size == 0) {
        return 0;
    }
    stub->initialize_calls++;
    return 1;
}

static int acgc_unresolved_game_systems_stub_initialize_failure(
    void* context,
    const AcgcBootSourceImages* images
) {
    AcgcUnresolvedGameSystemsStub* stub =
        (AcgcUnresolvedGameSystemsStub*)context;

    if (stub == NULL || images == NULL) {
        return 0;
    }
    stub->initialize_calls++;
    return 0;
}

static int acgc_unresolved_game_systems_stub_step(
    void* context,
    uint64_t frame_index,
    AcgcGameRuntimeSubmission* submission
) {
    static const char renderer_neutral_command[] = "FRAME";
    AcgcUnresolvedGameSystemsStub* stub =
        (AcgcUnresolvedGameSystemsStub*)context;

    if (stub == NULL || submission == NULL) {
        return 0;
    }
    stub->step_calls++;
    stub->last_frame = frame_index;
    submission->data = renderer_neutral_command;
    submission->size = sizeof(renderer_neutral_command) - 1;
    return 1;
}

static int acgc_unresolved_renderer_submission_stub(
    void* context,
    const AcgcGameRuntimeSubmission* submission
) {
    AcgcUnresolvedGameSystemsStub* stub =
        (AcgcUnresolvedGameSystemsStub*)context;

    if (stub == NULL || submission == NULL || submission->data == NULL ||
        submission->size != 5 || memcmp(submission->data, "FRAME", 5) != 0 ||
        submission->frame_index != 0) {
        return 0;
    }
    stub->submit_calls++;
    return 1;
}

static void acgc_unresolved_game_systems_stub_dispose(void* context) {
    AcgcUnresolvedGameSystemsStub* stub =
        (AcgcUnresolvedGameSystemsStub*)context;

    if (stub != NULL) {
        stub->dispose_calls++;
    }
}

static int boot_source_images_are_zero(const AcgcBootSourceImages* images) {
    uint32_t i;

    if (images == NULL || images->dol_data != NULL || images->rel_data != NULL ||
        images->manifest.dol_offset != 0 || images->manifest.dol_size != 0 ||
        images->manifest.fst_file_count != 0 ||
        images->manifest.rel_input_offset != 0 ||
        images->manifest.rel_input_size != 0 || images->rel_size != 0 ||
        images->rel_format != ACGC_REL_RAW) {
        return 0;
    }
    for (i = 0; i < ACGC_BOOT_SOURCE_REVISION_SIZE; i++) {
        if (images->manifest.revision[i] != 0) {
            return 0;
        }
    }
    return 1;
}

int main(void) {
    AcgcBootSourceImages images = { 0 };
    AcgcGameRuntime* runtime = NULL;
    AcgcGameRuntimeHooks hooks;
    AcgcGameRuntimeHooks invalid_hooks;
    AcgcUnresolvedGameSystemsStub stub = { 0 };
    uint8_t* dol_data;
    uint8_t* rel_data;
    AcgcBootSourceImages failed_images = { 0 };
    uint8_t* failed_dol_data;
    uint8_t* failed_rel_data;
    AcgcBootSourceImages occupied_output_images = { 0 };
    uint8_t* occupied_output_dol_data;
    uint8_t* occupied_output_rel_data;
    AcgcGameRuntime* occupied_runtime;
    AcgcGameRuntimeHooks failed_initialize_hooks;
    AcgcUnresolvedGameSystemsStub failed_initialize_stub = { 0 };
    AcgcBootSourceImages failed_initialize_images = { 0 };
    uint8_t* failed_initialize_dol_data;
    uint8_t* failed_initialize_rel_data;
    AcgcGameRuntime* failed_initialize_runtime = NULL;

    dol_data = (uint8_t*)malloc(4);
    rel_data = (uint8_t*)malloc(4);
    CHECK(dol_data != NULL && rel_data != NULL);
    memcpy(dol_data, "DOL!", 4);
    memcpy(rel_data, "REL!", 4);
    images.manifest.dol_size = 4;
    images.dol_data = dol_data;
    images.rel_data = rel_data;
    images.rel_size = 4;
    images.rel_format = ACGC_REL_RAW;

    hooks.context = &stub;
    hooks.initialize = acgc_unresolved_game_systems_stub_initialize;
    hooks.step = acgc_unresolved_game_systems_stub_step;
    hooks.submit = acgc_unresolved_renderer_submission_stub;
    hooks.dispose = acgc_unresolved_game_systems_stub_dispose;

    failed_dol_data = (uint8_t*)malloc(1);
    failed_rel_data = (uint8_t*)malloc(1);
    CHECK(failed_dol_data != NULL && failed_rel_data != NULL);
    failed_images.manifest.dol_size = 1;
    failed_images.dol_data = failed_dol_data;
    failed_images.rel_data = failed_rel_data;
    failed_images.rel_size = 1;
    failed_images.rel_format = ACGC_REL_RAW;
    invalid_hooks = hooks;
    invalid_hooks.initialize = NULL;
    CHECK(acgc_game_runtime_create(&runtime, &failed_images, &invalid_hooks) ==
          ACGC_GAME_RUNTIME_INVALID_ARGUMENT);
    CHECK(runtime == NULL);
    CHECK(failed_images.dol_data == failed_dol_data);
    CHECK(failed_images.rel_data == failed_rel_data);
    CHECK(failed_images.manifest.dol_size == 1);
    CHECK(failed_images.rel_size == 1);
    free(failed_dol_data);
    free(failed_rel_data);

    CHECK(acgc_game_runtime_create(&runtime, &images, &hooks) ==
          ACGC_GAME_RUNTIME_OK);
    CHECK(runtime != NULL);
    CHECK(boot_source_images_are_zero(&images));

    occupied_output_dol_data = (uint8_t*)malloc(1);
    occupied_output_rel_data = (uint8_t*)malloc(1);
    CHECK(occupied_output_dol_data != NULL && occupied_output_rel_data != NULL);
    occupied_output_images.manifest.dol_size = 1;
    occupied_output_images.dol_data = occupied_output_dol_data;
    occupied_output_images.rel_data = occupied_output_rel_data;
    occupied_output_images.rel_size = 1;
    occupied_output_images.rel_format = ACGC_REL_RAW;
    occupied_runtime = runtime;
    CHECK(acgc_game_runtime_create(
              &runtime,
              &occupied_output_images,
              &hooks
          ) == ACGC_GAME_RUNTIME_INVALID_ARGUMENT);
    CHECK(runtime == occupied_runtime);
    CHECK(occupied_output_images.dol_data == occupied_output_dol_data);
    CHECK(occupied_output_images.rel_data == occupied_output_rel_data);
    CHECK(occupied_output_images.manifest.dol_size == 1);
    CHECK(occupied_output_images.rel_size == 1);
    free(occupied_output_dol_data);
    free(occupied_output_rel_data);

    CHECK(acgc_game_runtime_step(runtime) ==
          ACGC_GAME_RUNTIME_NOT_INITIALIZED);
    CHECK(acgc_game_runtime_initialize(runtime) == ACGC_GAME_RUNTIME_OK);
    CHECK(stub.initialize_calls == 1);
    CHECK(acgc_game_runtime_initialize(runtime) ==
          ACGC_GAME_RUNTIME_ALREADY_INITIALIZED);
    CHECK(acgc_game_runtime_step(runtime) == ACGC_GAME_RUNTIME_OK);
    CHECK(stub.step_calls == 1);
    CHECK(stub.submit_calls == 1);
    CHECK(stub.last_frame == 0);
    CHECK(acgc_game_runtime_frame_count(runtime) == 1);
    acgc_game_runtime_dispose(runtime);
    CHECK(stub.dispose_calls == 1);

    failed_initialize_dol_data = (uint8_t*)malloc(1);
    failed_initialize_rel_data = (uint8_t*)malloc(1);
    CHECK(failed_initialize_dol_data != NULL && failed_initialize_rel_data != NULL);
    failed_initialize_images.manifest.dol_size = 1;
    failed_initialize_images.dol_data = failed_initialize_dol_data;
    failed_initialize_images.rel_data = failed_initialize_rel_data;
    failed_initialize_images.rel_size = 1;
    failed_initialize_images.rel_format = ACGC_REL_RAW;
    failed_initialize_hooks = hooks;
    failed_initialize_hooks.context = &failed_initialize_stub;
    failed_initialize_hooks.initialize =
        acgc_unresolved_game_systems_stub_initialize_failure;
    CHECK(acgc_game_runtime_create(
              &failed_initialize_runtime,
              &failed_initialize_images,
              &failed_initialize_hooks
          ) == ACGC_GAME_RUNTIME_OK);
    CHECK(acgc_game_runtime_initialize(failed_initialize_runtime) ==
          ACGC_GAME_RUNTIME_INITIALIZE_FAILED);
    CHECK(acgc_game_runtime_initialize(failed_initialize_runtime) ==
          ACGC_GAME_RUNTIME_INITIALIZE_FAILED);
    CHECK(failed_initialize_stub.initialize_calls == 1);
    acgc_game_runtime_dispose(failed_initialize_runtime);
    CHECK(failed_initialize_stub.dispose_calls == 1);

    puts("game runtime probe: PASS (owned boot images, occupied-output rejection, initialize, one nonblocking step, failed-init cleanup exactly once, renderer-neutral submission; reconstructed game systems remain stubbed)");
    return 0;
}
