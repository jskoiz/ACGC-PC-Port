#include "acgc/gbi_runtime.h"
#include "libforest/gbi_extensions.h"

#include <PR/mbi.h>

#include <stdint.h>
#include <stdio.h>

extern "C" void emu64_taskstart(Gfx* gfx);

extern "C" {
extern int pc_emu64_frame_cmds;
extern int pc_emu64_frame_vtx_cmds;
extern int pc_emu64_frame_dl_cmds;
extern int pc_emu64_frame_cull_visible;
extern int pc_emu64_frame_cull_rejected;
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct RuntimeHandles {
    uint32_t vertex;
    uint32_t inner;
    uint32_t segment;
    uint32_t outer;
} RuntimeHandles;

static void build_lists(
    Gfx* inner,
    Gfx* outer,
    Gfx* segment_end,
    Gfx* task,
    Vtx* vertices
) {
    gSPClearGeometryMode(inner + 0, G_FOG | G_LIGHTING);
    gSPVertex(inner + 1, &vertices[0], 8, 0);
    gSPCullDisplayList(inner + 2, 0, 7);
    gSPSetGeometryMode(inner + 3, G_FOG | G_LIGHTING);
    gSPEndDisplayList(inner + 4);

    gSPDisplayList(outer + 0, inner);
    gSPDisplayList(outer + 1, SEGMENT_ADDR(G_MWO_SEGMENT_A, 0));
    gSPEndDisplayList(outer + 2);

    gSPEndDisplayList(segment_end);
    gSPSegment(task + 0, G_MWO_SEGMENT_A, segment_end);
    gSPDisplayList(task + 1, outer);
    gSPEndDisplayList(task + 2);
}

static RuntimeHandles capture_handles(const Gfx* inner, const Gfx* outer, const Gfx* task) {
    RuntimeHandles handles;

    handles.vertex = inner[1].words.w1;
    handles.inner = outer[0].words.w1;
    handles.segment = task[0].words.w1;
    handles.outer = task[1].words.w1;
    return handles;
}

static int check_live_handles(const RuntimeHandles* handles) {
    uintptr_t resolved = 0;

    CHECK(pc_gbi_unpack_runtime_ptr(handles->vertex, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_RESOLVED);
    CHECK(pc_gbi_unpack_runtime_ptr(handles->inner, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_RESOLVED);
    CHECK(pc_gbi_unpack_runtime_ptr(handles->segment, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_RESOLVED);
    CHECK(pc_gbi_unpack_runtime_ptr(handles->outer, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_RESOLVED);
    return 0;
}

static int check_stale_handles(const RuntimeHandles* handles) {
    uintptr_t resolved = UINTPTR_MAX;

    CHECK(pc_gbi_unpack_runtime_ptr(handles->vertex, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
    CHECK(resolved == 0);
    CHECK(pc_gbi_unpack_runtime_ptr(handles->inner, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
    CHECK(pc_gbi_unpack_runtime_ptr(handles->segment, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
    CHECK(pc_gbi_unpack_runtime_ptr(handles->outer, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
    return 0;
}

static void reset_frame_counters(void) {
    pc_emu64_frame_cmds = 0;
    pc_emu64_frame_vtx_cmds = 0;
    pc_emu64_frame_dl_cmds = 0;
    pc_emu64_frame_cull_visible = 0;
    pc_emu64_frame_cull_rejected = 0;
}

static int test_nested_traversal_rebuild(void) {
    Gfx inner[5] = {};
    Gfx outer[3] = {};
    Gfx segment_end[1] = {};
    Gfx task[3] = {};
    Vtx vertices[8] = {};
    RuntimeHandles prior = {};

    for (int iteration = 0; iteration < 32; iteration++) {
        RuntimeHandles current;

        for (int vertex = 0; vertex < 8; vertex++) {
            vertices[vertex].n.ob[2] = -1;
            vertices[vertex].n.flag = MTX_NONSHARED;
        }

        build_lists(inner, outer, segment_end, task, vertices);
        current = capture_handles(inner, outer, task);

#if UINTPTR_MAX > UINT32_MAX
        CHECK(current.vertex != prior.vertex || iteration == 0);
        CHECK(current.inner != prior.inner || iteration == 0);
        CHECK(current.segment != prior.segment || iteration == 0);
        CHECK(current.outer != prior.outer || iteration == 0);
#endif
        CHECK(check_live_handles(&current) == 0);
        if (iteration != 0) {
            CHECK(check_stale_handles(&prior) == 0);
        }

        reset_frame_counters();
        emu64_taskstart(task);

        /* Root task (3), outer list (3), inner list (5), segment target (1). */
        CHECK(pc_emu64_frame_cmds == 12);
        CHECK(pc_emu64_frame_dl_cmds == 3);
        CHECK(pc_emu64_frame_vtx_cmds == 1);
        CHECK(pc_emu64_frame_cull_visible == 1);
        CHECK(pc_emu64_frame_cull_rejected == 0);
        CHECK(check_stale_handles(&current) == 0);
        prior = current;
    }

    return 0;
}

int main(void) {
    pc_gbi_reset_runtime_ptr_registry();
    CHECK(test_nested_traversal_rebuild() == 0);
    printf("acgc emu64 GBI traversal tests passed\n");
    return 0;
}
