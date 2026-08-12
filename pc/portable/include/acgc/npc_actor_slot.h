#ifndef ACGC_NPC_ACTOR_SLOT_H
#define ACGC_NPC_ACTOR_SLOT_H

#include <stddef.h>
#include <stdint.h>

#define ACGC_NPC_ACTOR_SLOT_ALIGNMENT 16u

/*
 * The ordinary NPC clip keeps fixed static actor slots.  Any 64-bit TARGET_PC
 * host grows NPC_ACTOR beyond the historical PC slot; keep the logical
 * capacity tied to the actual host type instead of guessing a replacement
 * constant.  ILP32 TARGET_PC keeps its existing slot contract.
 */
#if defined(TARGET_PC) && (UINTPTR_MAX > UINT32_MAX)
#define ACGC_NPC_ACTOR_SLOT_HOST_LP64 1
#define ACGC_NPC_ACTOR_SLOT_SIZE(actor_type) (sizeof(actor_type))
#define ACGC_NPC_ACTOR_SLOT_BACKING_SIZE(actor_type) \
    (ACGC_NPC_ACTOR_SLOT_SIZE(actor_type) + (ACGC_NPC_ACTOR_SLOT_ALIGNMENT - 1u))
#elif defined(TARGET_PC)
#define ACGC_NPC_ACTOR_SLOT_HOST_LP64 0
#define ACGC_NPC_ACTOR_SLOT_SIZE(actor_type) ((size_t)0xA50u)
#define ACGC_NPC_ACTOR_SLOT_BACKING_SIZE(actor_type) \
    ACGC_NPC_ACTOR_SLOT_SIZE(actor_type)
#else
#define ACGC_NPC_ACTOR_SLOT_HOST_LP64 0
#define ACGC_NPC_ACTOR_SLOT_SIZE(actor_type) ((size_t)0x9D0u)
#define ACGC_NPC_ACTOR_SLOT_BACKING_SIZE(actor_type) \
    ACGC_NPC_ACTOR_SLOT_SIZE(actor_type)
#endif

#define ACGC_NPC_ACTOR_SLOT_ACCEPTS(size, slot_size) \
    ((size_t)(size) <= (size_t)(slot_size))

#ifdef TARGET_PC
static inline void* acgc_npc_actor_slot_aligned(void* storage) {
    uintptr_t address = (uintptr_t)storage;
    const uintptr_t mask = (uintptr_t)(ACGC_NPC_ACTOR_SLOT_ALIGNMENT - 1u);

    return (void*)((address + mask) & ~mask);
}
#endif

#endif /* ACGC_NPC_ACTOR_SLOT_H */
