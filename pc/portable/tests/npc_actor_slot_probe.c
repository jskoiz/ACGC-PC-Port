#include "ac_npc.h"
#include "acgc/npc_actor_slot.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    u64 align;
    u8 buf[ACGC_NPC_ACTOR_SLOT_BACKING_SIZE(NPC_ACTOR)];
} NpcActorSlotProbe;

#if !defined(TARGET_PC)
#error "the NPC actor slot probe covers TARGET_PC contracts"
#elif ACGC_NPC_ACTOR_SLOT_HOST_LP64
_Static_assert(
    ACGC_NPC_ACTOR_SLOT_BACKING_SIZE(NPC_ACTOR) >=
        sizeof(NPC_ACTOR) + ACGC_NPC_ACTOR_SLOT_ALIGNMENT - 1u,
    "NPC actor backing must include alignment slack"
);
#else
_Static_assert(
    ACGC_NPC_ACTOR_SLOT_SIZE(NPC_ACTOR) == 0xA50u,
    "legacy TARGET_PC actor slots must retain their 0xA50 capacity"
);
_Static_assert(
    ACGC_NPC_ACTOR_SLOT_BACKING_SIZE(NPC_ACTOR) == 0xA50u,
    "legacy TARGET_PC actor slots must retain their 0xA50 backing"
);
_Static_assert(
    ACGC_NPC_ACTOR_SLOT_SIZE(NPC_ACTOR) >=
        sizeof(NPC_ACTOR) + ACGC_NPC_ACTOR_SLOT_ALIGNMENT - 1u,
    "legacy TARGET_PC actor slots must retain the backing sentinel contract"
);
#endif
_Static_assert(
    sizeof(NpcActorSlotProbe) >=
        offsetof(NpcActorSlotProbe, buf) +
            ACGC_NPC_ACTOR_SLOT_BACKING_SIZE(NPC_ACTOR),
    "NPC actor probe slot must contain its backing buffer"
);

static int check(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "npc actor slot probe: %s\n", message);
        return 1;
    }

    return 0;
}

int main(void) {
    NpcActorSlotProbe slot;
    uintptr_t storage_begin = (uintptr_t)slot.buf;
    uintptr_t storage_end =
        storage_begin + ACGC_NPC_ACTOR_SLOT_BACKING_SIZE(NPC_ACTOR);
    uintptr_t actor_address =
        (uintptr_t)acgc_npc_actor_slot_aligned(slot.buf);
    size_t slot_size = ACGC_NPC_ACTOR_SLOT_SIZE(NPC_ACTOR);
    int failures = 0;

    printf(
        "npc actor slot: sizeof=0x%zx align=%zu capacity=0x%zx backing=0x%zx pointer_align=%u\n",
        sizeof(NPC_ACTOR),
        _Alignof(NPC_ACTOR),
        slot_size,
        ACGC_NPC_ACTOR_SLOT_BACKING_SIZE(NPC_ACTOR),
        ACGC_NPC_ACTOR_SLOT_ALIGNMENT
    );

#if ACGC_NPC_ACTOR_SLOT_HOST_LP64
    failures += check(
        sizeof(NPC_ACTOR) > 0xA50u,
        "wide-host probe no longer covers the original undersized host slot"
    );
#else
    failures += check(
        slot_size == 0xA50u &&
            ACGC_NPC_ACTOR_SLOT_BACKING_SIZE(NPC_ACTOR) == 0xA50u &&
            slot_size >= sizeof(NPC_ACTOR) + ACGC_NPC_ACTOR_SLOT_ALIGNMENT - 1u,
        "legacy TARGET_PC probe must retain the historical 0xA50 contract"
    );
#endif
    failures += check(
        ACGC_NPC_ACTOR_SLOT_ACCEPTS(slot_size, slot_size),
        "the exact slot boundary must be accepted"
    );
    failures += check(
        !ACGC_NPC_ACTOR_SLOT_ACCEPTS(slot_size + 1u, slot_size),
        "the first byte beyond the slot boundary must be rejected"
    );
    failures += check(
        (actor_address & (ACGC_NPC_ACTOR_SLOT_ALIGNMENT - 1u)) == 0,
        "the returned actor address must have the target alignment"
    );
    failures += check(
        actor_address >= storage_begin && actor_address <= storage_end &&
            sizeof(NPC_ACTOR) <= storage_end - actor_address,
        "an aligned NPC actor must fit inside its static backing slot"
    );
    failures += check(
        actor_address + sizeof(NPC_ACTOR) <= storage_end,
        "the NPC actor end must stay within the backing allocation"
    );

    return failures == 0 ? 0 : 1;
}
