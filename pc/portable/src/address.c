#include "acgc/address.h"

#include <stdint.h>

static AcgcAddressStatus validate_range(const AcgcAddressRange* range) {
    if (range == NULL) {
        return ACGC_ADDRESS_INVALID_ARGUMENT;
    }
    if (range->begin > range->end) {
        return ACGC_ADDRESS_INVALID_RANGE;
    }
    return ACGC_ADDRESS_OK;
}

static AcgcAddressStatus validate_alignment(uintptr_t alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return ACGC_ADDRESS_INVALID_ALIGNMENT;
    }
    return ACGC_ADDRESS_OK;
}

static AcgcAddressStatus checked_add(
    uintptr_t address,
    size_t size,
    uintptr_t* result
) {
    if ((uintmax_t)size > (uintmax_t)UINTPTR_MAX - (uintmax_t)address) {
        return ACGC_ADDRESS_OVERFLOW;
    }
    *result = address + (uintptr_t)size;
    return ACGC_ADDRESS_OK;
}

AcgcAddressStatus acgc_address_range_make(
    uintptr_t begin,
    size_t size,
    AcgcAddressRange* range
) {
    uintptr_t end;
    AcgcAddressStatus status;

    if (range == NULL) {
        return ACGC_ADDRESS_INVALID_ARGUMENT;
    }

    status = checked_add(begin, size, &end);
    if (status != ACGC_ADDRESS_OK) {
        return status;
    }

    range->begin = begin;
    range->end = end;
    return ACGC_ADDRESS_OK;
}

AcgcAddressStatus acgc_address_range_contains(
    const AcgcAddressRange* range,
    uintptr_t address,
    size_t size
) {
    uintptr_t end;
    AcgcAddressStatus status;

    status = validate_range(range);
    if (status != ACGC_ADDRESS_OK) {
        return status;
    }

    /* A zero-length range may name the one-past address, but not outside it. */
    if (address < range->begin || address > range->end) {
        return ACGC_ADDRESS_OUT_OF_RANGE;
    }

    status = checked_add(address, size, &end);
    if (status != ACGC_ADDRESS_OK) {
        return status;
    }
    if (end > range->end) {
        return ACGC_ADDRESS_OUT_OF_RANGE;
    }

    return ACGC_ADDRESS_OK;
}

AcgcAddressStatus acgc_address_align_up(
    uintptr_t address,
    uintptr_t alignment,
    uintptr_t* aligned
) {
    uintptr_t remainder;
    uintptr_t increment;
    AcgcAddressStatus status;

    if (aligned == NULL) {
        return ACGC_ADDRESS_INVALID_ARGUMENT;
    }

    status = validate_alignment(alignment);
    if (status != ACGC_ADDRESS_OK) {
        return status;
    }

    remainder = address & (alignment - 1);
    increment = remainder == 0 ? 0 : alignment - remainder;
    if (address > UINTPTR_MAX - increment) {
        return ACGC_ADDRESS_OVERFLOW;
    }

    *aligned = address + increment;
    return ACGC_ADDRESS_OK;
}

AcgcAddressStatus acgc_address_align_down(
    uintptr_t address,
    uintptr_t alignment,
    uintptr_t* aligned
) {
    AcgcAddressStatus status;

    if (aligned == NULL) {
        return ACGC_ADDRESS_INVALID_ARGUMENT;
    }

    status = validate_alignment(alignment);
    if (status != ACGC_ADDRESS_OK) {
        return status;
    }

    *aligned = address & ~(alignment - 1);
    return ACGC_ADDRESS_OK;
}

AcgcAddressStatus acgc_address_alignment_from_mask(
    uintptr_t mask,
    uintptr_t* alignment
) {
    uintptr_t candidate;
    AcgcAddressStatus status;

    if (alignment == NULL) {
        return ACGC_ADDRESS_INVALID_ARGUMENT;
    }

    candidate = (~mask) + 1;
    status = validate_alignment(candidate);
    if (status != ACGC_ADDRESS_OK || mask != ~(candidate - 1)) {
        return ACGC_ADDRESS_INVALID_ALIGNMENT;
    }

    *alignment = candidate;
    return ACGC_ADDRESS_OK;
}

AcgcAddressStatus acgc_address_tail_alloc(
    const AcgcAddressRange* range,
    uintptr_t tail,
    size_t size,
    uintptr_t alignment,
    uintptr_t* next_tail
) {
    uintptr_t aligned_tail;
    uintptr_t candidate;
    AcgcAddressStatus status;

    if (range == NULL || next_tail == NULL) {
        return ACGC_ADDRESS_INVALID_ARGUMENT;
    }

    status = validate_range(range);
    if (status != ACGC_ADDRESS_OK) {
        return status;
    }
    status = acgc_address_range_contains(range, tail, 0);
    if (status != ACGC_ADDRESS_OK) {
        return status;
    }
    status = acgc_address_align_down(tail, alignment, &aligned_tail);
    if (status != ACGC_ADDRESS_OK) {
        return status;
    }
    if ((uintmax_t)size > (uintmax_t)aligned_tail) {
        return ACGC_ADDRESS_UNDERFLOW;
    }

    candidate = aligned_tail - (uintptr_t)size;
    status = acgc_address_align_down(candidate, alignment, &candidate);
    if (status != ACGC_ADDRESS_OK) {
        return status;
    }
    status = acgc_address_range_contains(range, candidate, 0);
    if (status != ACGC_ADDRESS_OK) {
        return status;
    }

    *next_tail = candidate;
    return ACGC_ADDRESS_OK;
}

AcgcAddressStatus acgc_address_tail_free(
    const AcgcAddressRange* range,
    uintptr_t head,
    uintptr_t tail,
    uintptr_t alignment,
    size_t* free_bytes
) {
    uintptr_t aligned_head;
    uintptr_t delta;
    AcgcAddressStatus status;

    if (range == NULL || free_bytes == NULL) {
        return ACGC_ADDRESS_INVALID_ARGUMENT;
    }

    status = validate_range(range);
    if (status != ACGC_ADDRESS_OK) {
        return status;
    }
    status = acgc_address_range_contains(range, head, 0);
    if (status != ACGC_ADDRESS_OK) {
        return status;
    }
    status = acgc_address_range_contains(range, tail, 0);
    if (status != ACGC_ADDRESS_OK) {
        return status;
    }
    status = acgc_address_align_up(head, alignment, &aligned_head);
    if (status != ACGC_ADDRESS_OK) {
        return status;
    }
    if (tail < aligned_head) {
        return ACGC_ADDRESS_UNDERFLOW;
    }

    delta = tail - aligned_head;
    if ((uintmax_t)delta > (uintmax_t)SIZE_MAX) {
        return ACGC_ADDRESS_OVERFLOW;
    }

    *free_bytes = (size_t)delta;
    return ACGC_ADDRESS_OK;
}
