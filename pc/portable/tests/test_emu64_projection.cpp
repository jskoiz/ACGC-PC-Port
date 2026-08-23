#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define private public
#include "libforest/emu64/emu64.hpp"
#undef private

#include "../../../src/static/libforest/emu64/emu64.c"

#include "acgc/gbi_runtime.h"
#include "libforest/gbi_extensions.h"
#include "libultra/gu.h"

#include <PR/mbi.h>

extern "C" void pc_gbi_reset_runtime_ptr_registry(void);
extern "C" uint32_t pc_gx_texture_mark_image_converted_call_count(void);
extern "C" void (*pc_gx_texture_mark_image_converted_boundary(void))(unsigned int);

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static constexpr float kFixedPointProjectionTolerance = 1.0f / 4096.0f;

static void make_perspective(float matrix[4][4], float near, float far) {
    std::memset(matrix, 0, sizeof(float) * 16);
    matrix[0][0] = 1.0f;
    matrix[1][1] = 1.0f;
    matrix[2][2] = (near + far) / (near - far);
    matrix[2][3] = -1.0f;
    matrix[3][2] = (2.0f * near * far) / (near - far);
}

static void make_infinite_perspective_limit(float matrix[4][4], float near) {
    std::memset(matrix, 0, sizeof(float) * 16);
    matrix[0][0] = 1.0f;
    matrix[1][1] = 1.0f;
    matrix[2][2] = -1.0f;
    matrix[2][3] = -1.0f;
    matrix[3][2] = -2.0f * near;
}

static void make_orthographic(float matrix[4][4], float left, float right, float bottom, float top,
                              float near, float far) {
    std::memset(matrix, 0, sizeof(float) * 16);
    matrix[0][0] = 2.0f / (right - left);
    matrix[1][1] = 2.0f / (top - bottom);
    matrix[2][2] = -2.0f / (far - near);
    matrix[3][0] = -(right + left) / (right - left);
    matrix[3][1] = -(top + bottom) / (top - bottom);
    matrix[3][2] = -(far + near) / (far - near);
    matrix[3][3] = 1.0f;
}

static int test_real_fixed_point_round_trip(void) {
    float source[4][4] = {
        {1.2345f, -2.3456f, 3.4567f, -4.5678f},
        {-5.6789f, 6.7891f, -7.8912f, 8.9123f},
        {9.1234f, -1.2345f, 2.3456f, -3.4567f},
        {-4.5678f, 5.6789f, -6.7891f, 7.8912f},
    };
    Mtx matrix = {};
    GC_Mtx converted = {};

    guMtxF2L(source, &matrix);
    N64Mtx_to_DOLMtx(&matrix, converted);

    /* The fixed-point wire layout is unchanged; the Dolphin view is transposed. */
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 4; column++) {
            CHECK(std::fabs(converted[row][column] - source[column][row]) < kFixedPointProjectionTolerance);
        }
    }
    return 0;
}

static void dispatch_projection(emu64& runtime, Mtx* matrix) {
    Gmtx* command = reinterpret_cast<Gmtx*>(&runtime.gfx);
    std::memset(&runtime.gfx, 0, sizeof(runtime.gfx));
    command->cmd = G_MTX;
    command->type = G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH;
    command->addr = pc_gbi_pack_runtime_ptr(
        reinterpret_cast<uintptr_t>(matrix), 1, "projection matrix", __FILE__, __LINE__);
    runtime.dl_G_MTX();
    pc_gbi_reset_runtime_ptr_registry();
}

static int test_finite_perspective_reconstruction(void) {
    constexpr float near = 10.0f;
    constexpr float far = 100.0f;
    float source[4][4];
    Mtx matrix = {};
    emu64 runtime = {};

    make_perspective(source, near, far);
    guMtxF2L(source, &matrix);
    runtime.emu64_init();
    dispatch_projection(runtime, &matrix);

    CHECK(runtime.projection_type == GX_PERSPECTIVE);
    CHECK(std::fabs(runtime.near - near) < 0.01f);
    CHECK(std::fabs(runtime.far - far) < 0.01f);
    CHECK(std::isfinite(runtime.near));
    CHECK(std::isfinite(runtime.far));
    CHECK(std::fabs(runtime.projection_mtx[2][2] - (-near / (far - near))) <
          kFixedPointProjectionTolerance);
    CHECK(std::fabs(runtime.projection_mtx[2][3] - (-(near * far) / (far - near))) <
          kFixedPointProjectionTolerance);
    CHECK(std::isfinite(runtime.projection_mtx[2][2]));
    CHECK(std::isfinite(runtime.projection_mtx[2][3]));
    return 0;
}

static int test_orthographic_classification(void) {
    float source[4][4];
    Mtx matrix = {};
    emu64 runtime = {};

    make_orthographic(source, 0.0f, 640.0f, 480.0f, 0.0f, -1000.0f, 1000.0f);
    guMtxF2L(source, &matrix);
    runtime.emu64_init();
    dispatch_projection(runtime, &matrix);

    CHECK(runtime.projection_type == GX_ORTHOGRAPHIC);
    CHECK(std::fabs(runtime.near - -1000.0f) < 16.0f);
    CHECK(std::fabs(runtime.far - 1000.0f) < 16.0f);
    CHECK(std::isfinite(runtime.near));
    CHECK(std::isfinite(runtime.far));
    CHECK(std::isfinite(runtime.projection_mtx[2][2]));
    CHECK(std::isfinite(runtime.projection_mtx[2][3]));
    return 0;
}

static int test_infinite_perspective_fails_closed(void) {
    float source[4][4];
    Mtx matrix = {};
    emu64 runtime = {};
    Mtx44 previous_projection;
    GC_Mtx previous_original_projection;
    GC_Mtx previous_position_mtx;
    const float source_near = 10.0f;
    const float source_far = 100.0f;
    float previous_runtime_near;
    float previous_runtime_far;
    uint32_t previous_errors;
    bool previous_projection_dirty;
    bool previous_fog_dirty;

    make_perspective(source, source_near, source_far);
    guMtxF2L(source, &matrix);
    runtime.emu64_init();
    dispatch_projection(runtime, &matrix);
    std::memcpy(previous_projection, runtime.projection_mtx, sizeof(previous_projection));
    std::memcpy(previous_original_projection, runtime.original_projection_mtx, sizeof(previous_original_projection));
    std::memcpy(previous_position_mtx, runtime.position_mtx, sizeof(previous_position_mtx));
    previous_runtime_near = runtime.near;
    previous_runtime_far = runtime.far;
    previous_errors = runtime.err_count;
    previous_projection_dirty = runtime.dirty_flags[EMU64_DIRTY_FLAG_PROJECTION_MTX];
    previous_fog_dirty = runtime.dirty_flags[EMU64_DIRTY_FLAG_FOG];

    make_infinite_perspective_limit(source, source_near);
    guMtxF2L(source, &matrix);
    dispatch_projection(runtime, &matrix);

    CHECK(runtime.err_count == previous_errors + 1);
    CHECK(runtime.projection_type == GX_PERSPECTIVE);
    CHECK(runtime.near == previous_runtime_near);
    CHECK(runtime.far == previous_runtime_far);
    CHECK(std::memcmp(previous_projection, runtime.projection_mtx, sizeof(previous_projection)) == 0);
    CHECK(std::memcmp(previous_original_projection, runtime.original_projection_mtx,
                      sizeof(previous_original_projection)) == 0);
    CHECK(std::memcmp(previous_position_mtx, runtime.position_mtx, sizeof(previous_position_mtx)) == 0);
    CHECK(runtime.dirty_flags[EMU64_DIRTY_FLAG_PROJECTION_MTX] == previous_projection_dirty);
    CHECK(runtime.dirty_flags[EMU64_DIRTY_FLAG_FOG] == previous_fog_dirty);
    for (int row = 0; row < 4; row++) {
        for (int column = 0; column < 4; column++) {
            CHECK(std::isfinite(runtime.projection_mtx[row][column]));
        }
    }
    return 0;
}

int main(void) {
    pc_gbi_reset_runtime_ptr_registry();
    CHECK(pc_gx_texture_mark_image_converted_boundary() != nullptr);
    CHECK(pc_gx_texture_mark_image_converted_call_count() == 0);
    CHECK(test_real_fixed_point_round_trip() == 0);
    CHECK(test_finite_perspective_reconstruction() == 0);
    CHECK(test_orthographic_classification() == 0);
    CHECK(test_infinite_perspective_fails_closed() == 0);
    CHECK(pc_gx_texture_mark_image_converted_call_count() == 0);
    printf("acgc emu64 projection tests passed\n");
    return 0;
}
