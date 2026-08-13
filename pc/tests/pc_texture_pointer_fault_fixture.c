/*
 * Regression fixture for the arm64 texture-object pointer fault.
 *
 * The GBI path may carry a native pointer in a 32-bit opaque handle, but
 * GXInitTexObj must receive and retain the full native pointer after the handle
 * has been resolved.  The GXTexObj ABI remains a fixed u32 layout; on LP64 its
 * image slot is a live opaque capability rather than a truncated pointer.
 */

#include "acgc/gbi_runtime.h"
#include "acgc/gbi_reference_registry.h"
#include "pc_gx_internal.h"

#include <dolphin/gx/GXGet.h>
#include <dolphin/gx/GXExtra.h>
#include <dolphin/gx/GXTexture.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

int main(void) {
#if UINTPTR_MAX <= UINT32_MAX
    puts("pc_texture_pointer_fault_fixture: SKIP (requires a wider-than-u32 uintptr_t)");
    return 77;
#else
    _Alignas(32) static uint8_t texture_bytes[64];
    const uintptr_t native_address = (uintptr_t)texture_bytes;
    uintptr_t resolved_address = 0;
    uint32_t opaque_reference;
    AcgcGbiRuntimePtrStatus reference_status;
    GXTexObj texture_object;
    GXTexObj shared_object;
    GXTexObj stale_object;
    uintptr_t recovered_address;
    uint32_t first_handle;
    uint32_t stored_handle;

    CHECK(native_address > (uintptr_t)UINT32_MAX);

    pc_gbi_reset_runtime_ptr_registry();
    opaque_reference = pc_gbi_pack_runtime_ptr(
        native_address,
        1,
        "texture_bytes",
        __FILE__,
        __LINE__
    );
    CHECK((opaque_reference & ACGC_GBI_REFERENCE_HANDLE_PREFIX_MASK) ==
          ACGC_GBI_REFERENCE_HANDLE_PREFIX);

    reference_status = pc_gbi_unpack_runtime_ptr(
        opaque_reference,
        &resolved_address
    );
    CHECK(reference_status == ACGC_GBI_RUNTIME_PTR_RESOLVED);
    CHECK(resolved_address == native_address);

    pc_gx_texture_init();

    /* GXInitTexObj must tolerate an uninitialized caller-owned ABI object. */
    memset(&texture_object, 0xF0, sizeof(texture_object));
    memset(&shared_object, 0xF0, sizeof(shared_object));

    /* Exercise the real GXInitTexObj implementation from pc_gx_texture.c. */
    GXInitTexObj(
        &texture_object,
        (void*)resolved_address,
        8,
        8,
        GX_TF_I8,
        GX_CLAMP,
        GX_CLAMP,
        GX_FALSE
    );

    CHECK(GXGetTexObjWidth(&texture_object) == 8);
    CHECK(GXGetTexObjHeight(&texture_object) == 8);
    CHECK(GXGetTexObjFmt(&texture_object) == GX_TF_I8);
    CHECK(GXGetTexObjWrapS(&texture_object) == GX_CLAMP);
    CHECK(GXGetTexObjWrapT(&texture_object) == GX_CLAMP);

    first_handle = texture_object.dummy[0];
    recovered_address = (uintptr_t)GXGetTexObjData(&texture_object);

    printf(
        "pc_texture_pointer_fault_fixture: native=0x%" PRIxPTR
        " opaque=0x%08" PRIx32
        " resolved=0x%" PRIxPTR
        " stored=0x%08" PRIx32
        " recovered=0x%" PRIxPTR "\n",
        native_address,
        opaque_reference,
        resolved_address,
        first_handle,
        recovered_address
    );

    CHECK((first_handle & ACGC_GBI_REFERENCE_HANDLE_PREFIX_MASK) ==
          ACGC_GBI_REFERENCE_HANDLE_PREFIX);
    CHECK(recovered_address == native_address);

    /* Equal native pointers share a capability but retain independent ownership. */
    GXInitTexObj(&shared_object, (void*)native_address, 8, 8, GX_TF_I8,
                 GX_CLAMP, GX_CLAMP, GX_FALSE);
    CHECK(shared_object.dummy[0] == first_handle);
    GXDestroyTexObj(&shared_object);
    CHECK((uintptr_t)GXGetTexObjData(&texture_object) == native_address);

    /* Reinitialization releases the prior capability and obtains a new generation. */
    GXInitTexObj(&texture_object, (void*)native_address, 8, 8, GX_TF_I8,
                 GX_CLAMP, GX_CLAMP, GX_FALSE);
    stored_handle = texture_object.dummy[0];
    CHECK(stored_handle != first_handle);
    stale_object = texture_object;
    stale_object.dummy[0] = first_handle;
    CHECK(GXGetTexObjData(&stale_object) == NULL);

    /* The live object remains valid, then destroy invalidates its capability. */
    CHECK((uintptr_t)GXGetTexObjData(&texture_object) == native_address);
    GXDestroyTexObj(&texture_object);
    CHECK(texture_object.dummy[0] == 0);
    stale_object = texture_object;
    stale_object.dummy[0] = stored_handle;
    CHECK(GXGetTexObjData(&stale_object) == NULL);

    /* A fresh texture subsystem invalidates all remaining capabilities. */
    GXInitTexObj(&texture_object, (void*)native_address, 8, 8, GX_TF_I8,
                 GX_CLAMP, GX_CLAMP, GX_FALSE);
    CHECK(texture_object.dummy[0] != stored_handle);
    CHECK((uintptr_t)GXGetTexObjData(&texture_object) == native_address);
    stored_handle = texture_object.dummy[0];
    pc_gx_texture_init();
    CHECK(GXGetTexObjData(&texture_object) == NULL);

    puts("pc_texture_pointer_fault_fixture: PASS"
         " (LP64 GXTexObj capability preserves full native pointer)");
    puts("invariant: opaque GBI reference resolves to the original native pointer"
         " and that exact pointer survives GXInitTexObj/GXGetTexObjData;");
    puts("stale texture capabilities fail closed after destroy and subsystem reinitialization");
    return 0;
#endif
}
