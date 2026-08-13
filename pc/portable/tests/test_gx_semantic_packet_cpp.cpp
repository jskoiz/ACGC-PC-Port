#include "acgc/gx_semantic_packet.h"
#include "acgc/gx_semantic_packet_adapter.h"

#include <iostream>
#include <type_traits>

static_assert(
    std::is_standard_layout<AcgcGxSemanticPacket>::value,
    "GX semantic packet must remain standard-layout"
);
static_assert(
    std::is_trivially_copyable<AcgcGxSemanticPacket>::value,
    "GX semantic packet must remain trivially copyable"
);
static_assert(
    std::is_standard_layout<AcgcGxSemanticPacketAdapterResult>::value,
    "GX semantic packet adapter result must remain standard-layout"
);
static_assert(
    std::is_trivially_copyable<AcgcGxSemanticPacketAdapterResult>::value,
    "GX semantic packet adapter result must remain trivially copyable"
);

int main() {
    AcgcGxSemanticPacket packet{};

    if (!acgc_gx_semantic_packet_init(&packet)) {
        std::cerr << "GX semantic packet C++ init failed\n";
        return 1;
    }
    packet.vertex_count = 3;
    packet.material.flags = ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR;
    packet.vertices[0].position[0] = 0;
    packet.vertices[1].position[0] = 0;
    packet.vertices[2].position[0] = 0;
    if (!acgc_gx_semantic_packet_validate(&packet)) {
        std::cerr << "GX semantic packet C++ validation failed\n";
        return 1;
    }
    std::cout << "GX semantic packet C++ probe: PASS\n";
    return 0;
}
