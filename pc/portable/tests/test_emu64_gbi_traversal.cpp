#include "acgc/gbi_runtime.h"
#include "libforest/gbi_extensions.h"

#include <PR/mbi.h>

#include <stdint.h>
#include <stdio.h>

extern "C" void emu64_taskstart(Gfx* gfx);
extern "C" void emu64_init(void);

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

static Vtx static_vertices[8] = {};
static Mtx static_matrix = {};
static u8 static_texture[128] = {};
static u16 static_palette[16] = {};
static Gfx static_inner[] = {
    gsSPVertex(static_vertices, 8, 0),
    gsSPMatrix(&static_matrix, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH),
    gsDPSetTextureImage_Dolphin(G_IM_FMT_CI, G_IM_SIZ_4b, 16, 32, static_texture),
    gsDPLoadTLUT_Dolphin(15, 16, 1, static_palette),
    gsSPEndDisplayList(),
};
static Gfx static_outer[] = {
    gsSPDisplayList(static_inner),
    gsSPEndDisplayList(),
};
static Gfx static_task[] = {
    gsSPDisplayList(static_outer),
    gsSPEndDisplayList(),
};
static Gfx static_scalar_commands[] = {
    gsMoveWd(G_MW_SEGMENT, G_MWO_SEGMENT_A, SEGMENT_ADDR(G_MWO_SEGMENT_A, 0)),
    gsSPPopMatrixN(0, 0),
    gsSPEndDisplayList(),
};
static Gfx static_grouped_commands[] = {
    gsSPClipRatio(FRUSTRATIO_1),
    gsSPLightColor(LIGHT_1, 0x01020304),
    gsSPForceMatrix(&static_matrix),
    gsSPEndDisplayList(),
};

static Gfx raw_e_reserved_collision[] = {
    {{
        _SHIFTL(G_RDPHALF_1, 24, 8),
        ACGC_GBI_STATIC_REFERENCE_PREFIX |
            _SHIFTL(G_RDPHALF_1, ACGC_GBI_STATIC_REFERENCE_COMMAND_SHIFT, 8) |
            UINT32_C(1),
    }},
    {{
        _SHIFTL(G_SPNOOP, 24, 8),
        UINT32_C(0xCAFEBABE),
    }},
    gsSPEndDisplayList(),
};

static Gfx raw_e_opcode_collision[] = {
    {{
        _SHIFTL(G_RDPHALF_1, 24, 8),
        ACGC_GBI_STATIC_REFERENCE_TAG(0, 0, G_DL),
    }},
    {{
        _SHIFTL(G_SPNOOP, 24, 8),
        UINT32_C(0x13579BDF),
    }},
    gsSPEndDisplayList(),
};

static Gfx raw_e_exact_collision[] = {
    {{
        _SHIFTL(G_RDPHALF_1, 24, 8),
        ACGC_GBI_STATIC_REFERENCE_TAG(0, 0, G_RDPHALF_1),
    }},
    gsSPEndDisplayList(),
    gsSPEndDisplayList(),
};

static Gfx raw_e_pointer_exact_collision[] = {
    {{
        _SHIFTL(G_RDPHALF_1, 24, 8),
        ACGC_GBI_STATIC_REFERENCE_TAG(0, 1, G_RDPHALF_1),
    }},
    gsSPEndDisplayList(),
    gsSPEndDisplayList(),
};

static void reset_frame_counters(void) {
    pc_emu64_frame_cmds = 0;
    pc_emu64_frame_vtx_cmds = 0;
    pc_emu64_frame_dl_cmds = 0;
    pc_emu64_frame_cull_visible = 0;
    pc_emu64_frame_cull_rejected = 0;
}

static int test_static_reference_traversal(void) {
    for (int vertex = 0; vertex < 8; vertex++) {
        static_vertices[vertex].n.ob[2] = -1;
        static_vertices[vertex].n.flag = MTX_NONSHARED;
    }
    for (int row = 0; row < 4; row++) {
        for (int column = 0; column < 4; column++) {
            static_matrix.m[row][column] = row == column ? 0x00010000L : 0;
        }
    }
    for (int entry = 0; entry < 16; entry++) {
        static_palette[entry] = (u16)entry;
    }

#if UINTPTR_MAX > UINT32_MAX
    CHECK((uintptr_t)static_vertices > (uintptr_t)UINT32_MAX);
    CHECK((uintptr_t)&static_matrix > (uintptr_t)UINT32_MAX);
    CHECK((uintptr_t)static_texture > (uintptr_t)UINT32_MAX);
    CHECK((uintptr_t)static_palette > (uintptr_t)UINT32_MAX);
#endif

    emu64_init();
    reset_frame_counters();
    emu64_taskstart(static_task);

    /* Root and outer lists each contain a nested G_DL; inner contains four
       pointer-bearing commands plus ENDDL. Payload entries must not count. */
    CHECK(pc_emu64_frame_cmds == 9);
    CHECK(pc_emu64_frame_dl_cmds == 2);
    CHECK(pc_emu64_frame_vtx_cmds == 1);

    /* A second synchronous traversal must use the immutable source tags and
       payloads again after the transient registry reset. */
    reset_frame_counters();
    emu64_taskstart(static_task);
    CHECK(pc_emu64_frame_cmds == 9);
    CHECK(pc_emu64_frame_dl_cmds == 2);
    CHECK(pc_emu64_frame_vtx_cmds == 1);
    return 0;
}

static int test_raw_e_prefix_collisions(void) {
    emu64_init();

    reset_frame_counters();
    emu64_taskstart(raw_e_reserved_collision);
    CHECK(pc_emu64_frame_cmds == 3);

    reset_frame_counters();
    emu64_taskstart(raw_e_opcode_collision);
    CHECK(pc_emu64_frame_cmds == 3);
    return 0;
}

static int test_exact_raw_e_tag_fails_closed(void) {
#if UINTPTR_MAX > UINT32_MAX
    uintptr_t resolved = UINTPTR_MAX;
    int is_pointer = 1;

    CHECK(ACGC_GBI_STATIC_REFERENCE_IS_WELL_FORMED(
        raw_e_exact_collision[0].words.w1));
    CHECK(ACGC_GBI_STATIC_REFERENCE_COMMAND(
        raw_e_exact_collision[0].words.w1) == G_RDPHALF_1);
    CHECK(!ACGC_GBI_STATIC_REFERENCE_IS_RAW_PAYLOAD(
        raw_e_exact_collision[1].static_reference));
    CHECK(pc_gbi_unpack_static_reference(
        raw_e_exact_collision[0].words.w1,
        raw_e_exact_collision[1].static_reference,
        raw_e_exact_collision[2].words.w0,
        raw_e_exact_collision[2].words.w1,
        &resolved,
        &is_pointer
    ) == ACGC_GBI_STATIC_REFERENCE_INVALID_REFERENCE);
    CHECK(resolved == 0);
    CHECK(is_pointer == 0);

    emu64_init();
    reset_frame_counters();
    emu64_taskstart(raw_e_exact_collision);
    /* The exact raw tag matches a capable opcode, but the following ENDDL
       lacks both the raw payload marker and the static trailer. Fail closed
       before consuming either command. */
    CHECK(pc_emu64_frame_cmds == 0);
#endif
    return 0;
}

static int test_exact_pointer_e_tag_fails_closed(void) {
#if UINTPTR_MAX > UINT32_MAX
    uintptr_t resolved = UINTPTR_MAX;
    int is_pointer = 1;

    CHECK(ACGC_GBI_STATIC_REFERENCE_IS_WELL_FORMED(
        raw_e_pointer_exact_collision[0].words.w1));
    CHECK(ACGC_GBI_STATIC_REFERENCE_IS_POINTER(
        raw_e_pointer_exact_collision[0].words.w1));
    CHECK(ACGC_GBI_STATIC_REFERENCE_COMMAND(
        raw_e_pointer_exact_collision[0].words.w1) == G_RDPHALF_1);
    CHECK(!ACGC_GBI_STATIC_REFERENCE_TRAILER_IS_VALID(
        raw_e_pointer_exact_collision[2].words.w0,
        raw_e_pointer_exact_collision[2].words.w1));
    CHECK(pc_gbi_unpack_static_reference(
        raw_e_pointer_exact_collision[0].words.w1,
        raw_e_pointer_exact_collision[1].static_reference,
        raw_e_pointer_exact_collision[2].words.w0,
        raw_e_pointer_exact_collision[2].words.w1,
        &resolved,
        &is_pointer
    ) == ACGC_GBI_STATIC_REFERENCE_INVALID_REFERENCE);
    CHECK(resolved == 0);
    CHECK(is_pointer == 0);

    emu64_init();
    reset_frame_counters();
    emu64_taskstart(raw_e_pointer_exact_collision);
    /* Pointer payloads preserve all bits, but still require the trailer; an
       ordinary ENDDL cannot be consumed as an arbitrary host pointer. */
    CHECK(pc_emu64_frame_cmds == 0);
#endif
    return 0;
}

static int test_static_scalar_commands(void) {
    emu64_init();
    reset_frame_counters();
    emu64_taskstart(static_scalar_commands);
    CHECK(pc_emu64_frame_cmds == 3);
    return 0;
}

static int test_static_grouped_commands(void) {
    emu64_init();
    reset_frame_counters();
    emu64_taskstart(static_grouped_commands);
    /* Clip ratio, light color, and force matrix each collapse their
       multi-command macro; their LP64 payload/trailer entries must not be
       dispatched as ordinary GBI commands. */
    CHECK(pc_emu64_frame_cmds == 4);
    return 0;
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
    CHECK(test_static_reference_traversal() == 0);
    CHECK(test_raw_e_prefix_collisions() == 0);
    CHECK(test_exact_raw_e_tag_fails_closed() == 0);
    CHECK(test_exact_pointer_e_tag_fails_closed() == 0);
    CHECK(test_static_scalar_commands() == 0);
    CHECK(test_static_grouped_commands() == 0);
    CHECK(test_nested_traversal_rebuild() == 0);
    printf("acgc emu64 GBI traversal tests passed\n");
    return 0;
}
