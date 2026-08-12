#ifdef F3DEX_GBI_2
#undef F3DEX_GBI_2
#endif

#include <PR/mbi.h>
#include <libforest/gbi_extensions.h>

#include <stdint.h>

static uSprite static_legacy_sprite;
static const Gfx static_legacy_commands[] = {
    gsSPSprite2DBase(&static_legacy_sprite),
    gsSPDisplayList(SEGMENT_ADDR(G_MWO_SEGMENT_A, 0)),
    gsSPEndDisplayList(),
};

_Static_assert(sizeof(Gfx) == 8, "Gfx must remain exactly 8 bytes");

int acgc_static_gbi_legacy_c_probe(void) {
    return static_legacy_commands[0].words.w0 == 0;
}
