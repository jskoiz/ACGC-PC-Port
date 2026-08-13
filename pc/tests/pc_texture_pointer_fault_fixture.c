/*
 * Forensic fixture for the arm64 texture-object pointer fault.
 *
 * This intentionally captures the current failure instead of implementing the
 * fix.  The GBI path may carry a native pointer in a 32-bit opaque handle, but
 * GXInitTexObj must receive and retain the full native pointer after the handle
 * has been resolved.  The current PC texture object stores that pointer in a
 * u32 slot, so GXGetTexObjData returns only the low word on arm64.
 */

#include "acgc/gbi_runtime.h"
#include "acgc/gbi_reference_registry.h"

#include <dolphin/gx/GXGet.h>
#include <dolphin/gx/GXTexture.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

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
    uintptr_t recovered_address;
    uint32_t stored_low_word;

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

    stored_low_word = texture_object.dummy[0];
    recovered_address = (uintptr_t)GXGetTexObjData(&texture_object);

    printf(
        "pc_texture_pointer_fault_fixture: native=0x%" PRIxPTR
        " opaque=0x%08" PRIx32
        " resolved=0x%" PRIxPTR
        " stored_low=0x%08" PRIx32
        " recovered=0x%" PRIxPTR "\n",
        native_address,
        opaque_reference,
        resolved_address,
        stored_low_word,
        recovered_address
    );

    /* This is the expected arm64 failure in the pre-fix source. */
    CHECK(stored_low_word == (uint32_t)native_address);
    CHECK(recovered_address == (uintptr_t)stored_low_word);
    CHECK(recovered_address != native_address);

    puts("pc_texture_pointer_fault_fixture: EXPECTED_FAILURE"
         " (GXTexObj image pointer truncates from native uintptr_t to u32)");
    puts("invariant: opaque GBI reference resolves to the original native pointer"
         " and that exact pointer must survive GXInitTexObj/GXGetTexObjData");
    return 0;
#endif
}
