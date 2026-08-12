#ifndef ACGC_ADDRESS_H
#define ACGC_ADDRESS_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    ACGC_ADDRESS_OK = 0,
    ACGC_ADDRESS_INVALID_ARGUMENT,
    ACGC_ADDRESS_INVALID_RANGE,
    ACGC_ADDRESS_INVALID_ALIGNMENT,
    ACGC_ADDRESS_OVERFLOW,
    ACGC_ADDRESS_UNDERFLOW,
    ACGC_ADDRESS_OUT_OF_RANGE
} AcgcAddressStatus;

/* A half-open address range: [begin, end). */
typedef struct {
    uintptr_t begin;
    uintptr_t end;
} AcgcAddressRange;

AcgcAddressStatus acgc_address_range_make(
    uintptr_t begin,
    size_t size,
    AcgcAddressRange* range
);

AcgcAddressStatus acgc_address_range_contains(
    const AcgcAddressRange* range,
    uintptr_t address,
    size_t size
);

AcgcAddressStatus acgc_address_align_up(
    uintptr_t address,
    uintptr_t alignment,
    uintptr_t* aligned
);

AcgcAddressStatus acgc_address_align_down(
    uintptr_t address,
    uintptr_t alignment,
    uintptr_t* aligned
);

/* Convert a legacy ~(alignment - 1) mask into a checked alignment. */
AcgcAddressStatus acgc_address_alignment_from_mask(
    uintptr_t mask,
    uintptr_t* alignment
);

/*
 * Reproduce TwoHeadArena's downward allocation formula while checking every
 * address operation:
 *
 *     align_down(align_down(tail, alignment) - size, alignment)
 */
AcgcAddressStatus acgc_address_tail_alloc(
    const AcgcAddressRange* range,
    uintptr_t tail,
    size_t size,
    uintptr_t alignment,
    uintptr_t* next_tail
);

#endif /* ACGC_ADDRESS_H */
