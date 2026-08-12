#include "acgc/gbi_reference_registry.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

static AcgcGbiReferenceRegistry s_runtime_ptr_registry;
static int s_runtime_ptr_registry_initialized = 0;
static int s_warned_odd_ptr = 0;

static void ensure_runtime_ptr_registry(void) {
    if (!s_runtime_ptr_registry_initialized) {
        acgc_gbi_reference_registry_init(&s_runtime_ptr_registry);
        s_runtime_ptr_registry_initialized = 1;
    }
}

void pc_gbi_reset_runtime_ptr_registry(void) {
    if (s_runtime_ptr_registry_initialized) {
        acgc_gbi_reference_registry_reset(&s_runtime_ptr_registry);
    } else {
        acgc_gbi_reference_registry_init(&s_runtime_ptr_registry);
        s_runtime_ptr_registry_initialized = 1;
    }
    s_warned_odd_ptr = 0;
}

uint32_t pc_gbi_pack_runtime_ptr(uintptr_t addr, int is_ptr, const char* expr, const char* file, int line) {
    uint32_t handle;
    uint32_t direct_value;
    AcgcGbiReferenceStatus status;

    ensure_runtime_ptr_registry();

    if (!is_ptr) {
        return (uint32_t)addr;
    }

    if ((addr & 1u) == 0) {
        direct_value = (uint32_t)(addr | 1u);
        if ((direct_value & ACGC_GBI_REFERENCE_HANDLE_PREFIX_MASK) !=
            ACGC_GBI_REFERENCE_HANDLE_PREFIX) {
            return direct_value;
        }
    }

    status = acgc_gbi_reference_registry_register(
        &s_runtime_ptr_registry,
        addr,
        &handle
    );
    if (status != ACGC_GBI_REFERENCE_OK) {
        fprintf(stderr,
                "[GBI] cannot register runtime pointer at %s:%d (%s): %s\n",
                file != NULL ? file : "<unknown>",
                line,
                expr != NULL ? expr : "<unknown>",
                acgc_gbi_reference_status_string(status));
        return 0;
    }

    if (!s_warned_odd_ptr) {
        fprintf(stderr,
                "[GBI] runtime pointer requiring an opaque reference: %s at %s:%d = 0x%" PRIxPTR
                "; using opaque reference registry\n",
                expr != NULL ? expr : "<unknown>",
                file != NULL ? file : "<unknown>",
                line,
                addr);
        s_warned_odd_ptr = 1;
    }

    return handle;
}

uintptr_t pc_gbi_unpack_runtime_ptr(uint32_t packed) {
    uintptr_t value;

    ensure_runtime_ptr_registry();
    if (acgc_gbi_reference_registry_resolve(
            &s_runtime_ptr_registry,
            packed,
            &value
        ) == ACGC_GBI_REFERENCE_OK) {
        return value;
    }

    return 0;
}
