#include "acgc/renderer_geometry.h"

#include <iostream>

int main() {
    AcgcRendererGeometryPacket packet{};

    if (!acgc_renderer_geometry_make_triangle(&packet) ||
        !acgc_renderer_geometry_validate(&packet)) {
        std::cerr << "C++ renderer geometry probe: packet validation failed\n";
        return 1;
    }
    std::cout << "C++ renderer geometry probe: PASS\n";
    return 0;
}
