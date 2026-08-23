#include <stdint.h>

/* Keep the production texture implementation out of this projection-only target. */
static uint32_t s_pc_gx_texture_mark_image_converted_calls;

void pc_gx_texture_mark_image_converted(unsigned int map) {
    (void)map;
    s_pc_gx_texture_mark_image_converted_calls++;
}

uint32_t pc_gx_texture_mark_image_converted_call_count(void) {
    return s_pc_gx_texture_mark_image_converted_calls;
}

void (*pc_gx_texture_mark_image_converted_boundary(void))(unsigned int) {
    return pc_gx_texture_mark_image_converted;
}
