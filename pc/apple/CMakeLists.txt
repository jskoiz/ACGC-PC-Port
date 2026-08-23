cmake_minimum_required(VERSION 3.16)

project(acgc_macos_native_host LANGUAGES C CXX OBJC)

if(NOT APPLE)
    message(FATAL_ERROR "pc/apple is a macOS-only AppKit host")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_OBJC_STANDARD 11)
set(CMAKE_OBJC_STANDARD_REQUIRED ON)

include(CTest)
find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(METAL_FRAMEWORK Metal REQUIRED)
find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)

# Keep the legacy pc/CMakeLists.txt independent. This entrypoint consumes only
# the existing dependency-free portable target.
add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../portable"
    "${CMAKE_CURRENT_BINARY_DIR}/acgc_portable"
)

add_library(acgc_renderer_geometry STATIC
    src/renderer_geometry.c
)
target_compile_features(acgc_renderer_geometry PUBLIC c_std_11)
target_include_directories(acgc_renderer_geometry
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(acgc_renderer_geometry PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(acgc_metal_state_fixture STATIC
    src/metal_state_fixture.c
)
target_compile_features(acgc_metal_state_fixture PUBLIC c_std_11)
target_include_directories(acgc_metal_state_fixture
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(acgc_metal_state_fixture PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(acgc_renderer_fixtures STATIC
    src/renderer_fixtures.c
)
target_compile_features(acgc_renderer_fixtures PUBLIC c_std_11)
target_include_directories(acgc_renderer_fixtures
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(acgc_renderer_fixtures PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(acgc_metal_packet_consumer STATIC
    src/metal_packet_consumer.c
)
target_compile_features(acgc_metal_packet_consumer PUBLIC c_std_11)
target_include_directories(acgc_metal_packet_consumer
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)
target_link_libraries(acgc_metal_packet_consumer
    PUBLIC
        acgc_gx_semantic_packet
        acgc_apple_canonical_plan
        acgc_metal_state_fixture
        acgc_renderer_fixtures
        acgc_renderer_geometry
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(acgc_metal_packet_consumer PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(acgc_metal_sink STATIC
    src/metal_sink.m
)
target_compile_options(acgc_metal_sink PRIVATE -fobjc-arc)
target_include_directories(acgc_metal_sink
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)
target_link_libraries(acgc_metal_sink
    PUBLIC
        acgc_metal_state_fixture
        acgc_renderer_geometry
        ${FOUNDATION_FRAMEWORK}
        ${METAL_FRAMEWORK}
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(acgc_metal_sink PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(acgc_game_runtime STATIC
    src/game_runtime.c
)
target_compile_features(acgc_game_runtime PUBLIC c_std_11)
target_include_directories(acgc_game_runtime
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(acgc_game_runtime PUBLIC acgc_portable)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(acgc_game_runtime PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(acgc_macos_host_core STATIC
    src/host.c
)
target_compile_features(acgc_macos_host_core PUBLIC c_std_11)
target_include_directories(acgc_macos_host_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(acgc_macos_host_core PUBLIC acgc_portable acgc_renderer_geometry)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(acgc_macos_host_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(ACGC_MACOS_HOST_FRAMEWORKS
    ${APPKIT_FRAMEWORK}
    ${FOUNDATION_FRAMEWORK}
    ${METAL_FRAMEWORK}
    ${QUARTZCORE_FRAMEWORK}
)

add_executable(acgc_macos_native_host MACOSX_BUNDLE
    src/main.m
)
add_executable(acgc_macos_native_host_cli
    src/main.m
)
foreach(host_target IN ITEMS acgc_macos_native_host acgc_macos_native_host_cli)
    target_compile_options(${host_target} PRIVATE -fobjc-arc)
    target_link_libraries(${host_target}
        PRIVATE
            acgc_macos_host_core
            acgc_game_runtime
            ${ACGC_MACOS_HOST_FRAMEWORKS}
    )
    set_target_properties(${host_target} PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.acgc.modernport.host"
        MACOSX_BUNDLE_BUNDLE_NAME "ACGC Modern macOS Native Host"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
        MACOSX_BUNDLE_LONG_VERSION_STRING "0.1"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
    )
endforeach()

add_executable(acgc_macos_host_core_tests
    tests/test_host_core.c
)
target_link_libraries(acgc_macos_host_core_tests PRIVATE acgc_macos_host_core)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(acgc_macos_host_core_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(acgc_game_runtime_probe
    tests/test_game_runtime.c
)
target_link_libraries(acgc_game_runtime_probe PRIVATE acgc_game_runtime)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(acgc_game_runtime_probe PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(acgc_legacy_seam_tests
    tests/test_legacy_seams.c
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/graph_submission.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/boot_hot_start.c"
)
target_include_directories(acgc_legacy_seam_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(acgc_legacy_seam_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(acgc_renderer_geometry_tests
    tests/test_renderer_geometry.c
)
target_link_libraries(acgc_renderer_geometry_tests PRIVATE acgc_renderer_geometry)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(acgc_renderer_geometry_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(acgc_renderer_geometry_cpp_tests
    tests/test_renderer_geometry_cpp.cpp
)
target_compile_features(acgc_renderer_geometry_cpp_tests PRIVATE cxx_std_11)
target_link_libraries(acgc_renderer_geometry_cpp_tests PRIVATE acgc_renderer_geometry)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(acgc_renderer_geometry_cpp_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(acgc_metal_state_fixture_tests
    tests/test_metal_state_fixture.m
)
target_compile_options(acgc_metal_state_fixture_tests PRIVATE -fobjc-arc)
target_link_libraries(acgc_metal_state_fixture_tests
    PRIVATE
        acgc_metal_state_fixture
        acgc_renderer_geometry
        ${FOUNDATION_FRAMEWORK}
        ${METAL_FRAMEWORK}
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(acgc_metal_state_fixture_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(acgc_apple_canonical_envelope_parser STATIC
    src/apple_canonical_envelope_parser.c
)
target_compile_features(acgc_apple_canonical_envelope_parser PUBLIC c_std_11)
target_include_directories(acgc_apple_canonical_envelope_parser
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(
        acgc_apple_canonical_envelope_parser
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()

add_executable(acgc_apple_canonical_envelope_parser_fixture
    tests/test_apple_canonical_envelope_parser.c
)
target_link_libraries(
    acgc_apple_canonical_envelope_parser_fixture
    PRIVATE
        acgc_apple_canonical_envelope_parser
)
target_compile_features(acgc_apple_canonical_envelope_parser_fixture PRIVATE c_std_11)
target_include_directories(acgc_apple_canonical_envelope_parser_fixture PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(
        acgc_apple_canonical_envelope_parser_fixture
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()

add_library(acgc_apple_canonical_plan STATIC
    src/apple_canonical_plan.c
)
target_compile_features(acgc_apple_canonical_plan PUBLIC c_std_11)
target_include_directories(acgc_apple_canonical_plan
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)
target_link_libraries(acgc_apple_canonical_plan
    PUBLIC
        acgc_apple_canonical_envelope_parser
        acgc_gx_canonical_state
        acgc_gx_canonical_geometry
        acgc_gx_canonical_transform_state
        acgc_gx_canonical_channel_state
        acgc_gx_canonical_texgen_state
        acgc_gx_canonical_texture_state
        acgc_gx_canonical_tev_state
        acgc_gx_canonical_lighting_state
        acgc_gx_canonical_blend_state
        acgc_gx_canonical_alpha_state
        acgc_gx_canonical_depth_state
        acgc_gx_canonical_raster_state
        acgc_gx_canonical_indirect_state
        acgc_gx_canonical_dynamic_state
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(
        acgc_apple_canonical_plan
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()

add_library(acgc_apple_canonical_plan_handoff STATIC
    src/apple_canonical_plan_handoff.c
)
target_compile_features(acgc_apple_canonical_plan_handoff PUBLIC c_std_11)
target_include_directories(acgc_apple_canonical_plan_handoff
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)
target_link_libraries(acgc_apple_canonical_plan_handoff
    PUBLIC
        acgc_apple_canonical_plan
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(
        acgc_apple_canonical_plan_handoff
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()

add_executable(acgc_apple_canonical_plan_fixture
    tests/test_apple_canonical_plan.c
    "${CMAKE_CURRENT_SOURCE_DIR}/../../pc/src/pc_gx_cumulative_snapshot.c"
)
target_compile_features(acgc_apple_canonical_plan_fixture PRIVATE c_std_11)
target_include_directories(acgc_apple_canonical_plan_fixture PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../pc/include"
)
target_link_libraries(
    acgc_apple_canonical_plan_fixture
    PRIVATE
        acgc_apple_canonical_plan
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(
        acgc_apple_canonical_plan_fixture
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()

add_executable(acgc_apple_canonical_plan_handoff_fixture
    tests/test_apple_canonical_plan_handoff.c
    src/apple_canonical_plan_handoff.c
)
target_compile_features(acgc_apple_canonical_plan_handoff_fixture PRIVATE c_std_11)
target_include_directories(acgc_apple_canonical_plan_handoff_fixture PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(
        acgc_apple_canonical_plan_handoff_fixture
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()

add_executable(acgc_apple_canonical_plan_consumer_fixture
    tests/test_apple_canonical_plan_consumer.c
)
target_compile_features(acgc_apple_canonical_plan_consumer_fixture PRIVATE c_std_11)
target_include_directories(acgc_apple_canonical_plan_consumer_fixture PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)
target_link_libraries(
    acgc_apple_canonical_plan_consumer_fixture
    PRIVATE
        acgc_metal_packet_consumer
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(
        acgc_apple_canonical_plan_consumer_fixture
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()

add_executable(acgc_metal_packet_consumer_tests
    tests/test_metal_packet_consumer.m
)
target_compile_options(acgc_metal_packet_consumer_tests PRIVATE -fobjc-arc)
target_link_libraries(acgc_metal_packet_consumer_tests
    PRIVATE
        acgc_metal_packet_consumer
        ${FOUNDATION_FRAMEWORK}
        ${METAL_FRAMEWORK}
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(acgc_metal_packet_consumer_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(acgc_metal_sink_tests
    tests/test_metal_sink.m
)
target_compile_options(acgc_metal_sink_tests PRIVATE -fobjc-arc)
target_link_libraries(acgc_metal_sink_tests
    PRIVATE
        acgc_metal_sink
        acgc_metal_packet_consumer
        ${FOUNDATION_FRAMEWORK}
        ${METAL_FRAMEWORK}
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(acgc_metal_sink_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(acgc_renderer_fixture_tests
    tests/test_renderer_fixtures.c
)
target_link_libraries(acgc_renderer_fixture_tests PRIVATE acgc_renderer_fixtures)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(acgc_renderer_fixture_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(acgc_metal_packet_consumer_v2_texture_tev_tests
    tests/test_metal_packet_consumer_v2_texture_tev.c
)
target_link_libraries(
    acgc_metal_packet_consumer_v2_texture_tev_tests
    PRIVATE
        acgc_metal_packet_consumer
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(
        acgc_metal_packet_consumer_v2_texture_tev_tests
        PRIVATE
            -Wall -Wextra -Wpedantic
    )
endif()

add_executable(acgc_metal_packet_consumer_v2_runtime_sideband_tests
    tests/test_metal_packet_consumer_v2_runtime_sideband.c
    src/pc_metal_runtime.c
)
target_compile_definitions(
    acgc_metal_packet_consumer_v2_runtime_sideband_tests
    PRIVATE
        ACGC_PC_METAL_RUNTIME_SINK_POLICY_FIXTURE
)
target_link_libraries(
    acgc_metal_packet_consumer_v2_runtime_sideband_tests
    PRIVATE
        acgc_metal_packet_consumer
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(
        acgc_metal_packet_consumer_v2_runtime_sideband_tests
        PRIVATE
            -Wall -Wextra -Wpedantic
    )
endif()

add_executable(acgc_metal_packet_consumer_v2_channel_source_tests
    tests/test_metal_packet_consumer_v2_channel_source.c
)
target_link_libraries(
    acgc_metal_packet_consumer_v2_channel_source_tests
    PRIVATE
        acgc_metal_packet_consumer
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(
        acgc_metal_packet_consumer_v2_channel_source_tests
        PRIVATE
            -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_TESTING)
    add_test(
        NAME acgc_apple_canonical_envelope_parser_fixture
        COMMAND acgc_apple_canonical_envelope_parser_fixture
    )
    add_test(
        NAME acgc_apple_canonical_plan_fixture
        COMMAND acgc_apple_canonical_plan_fixture
    )
    add_test(
        NAME acgc_apple_canonical_plan_handoff_fixture
        COMMAND acgc_apple_canonical_plan_handoff_fixture
    )
    add_test(
        NAME acgc_apple_canonical_plan_consumer_fixture
        COMMAND acgc_apple_canonical_plan_consumer_fixture
    )
    add_test(NAME acgc_macos_host_core_tests COMMAND acgc_macos_host_core_tests)
    add_test(NAME acgc_game_runtime_probe COMMAND acgc_game_runtime_probe)
    add_test(NAME acgc_legacy_seam_tests COMMAND acgc_legacy_seam_tests)
    add_test(NAME acgc_renderer_geometry_tests COMMAND acgc_renderer_geometry_tests)
    add_test(NAME acgc_renderer_geometry_cpp_tests COMMAND acgc_renderer_geometry_cpp_tests)
    add_test(NAME acgc_metal_state_fixture_tests COMMAND acgc_metal_state_fixture_tests)
    set_tests_properties(
        acgc_metal_state_fixture_tests
        PROPERTIES
            SKIP_RETURN_CODE 77
    )
    add_test(NAME acgc_metal_packet_consumer_tests COMMAND acgc_metal_packet_consumer_tests)
    set_tests_properties(
        acgc_metal_packet_consumer_tests
        PROPERTIES
            SKIP_RETURN_CODE 77
    )
    add_test(NAME acgc_metal_sink_tests COMMAND acgc_metal_sink_tests)
    set_tests_properties(
        acgc_metal_sink_tests
        PROPERTIES
            SKIP_RETURN_CODE 77
    )
    add_test(NAME acgc_renderer_fixture_tests COMMAND acgc_renderer_fixture_tests)
    add_test(
        NAME acgc_metal_packet_consumer_v2_texture_tev_tests
        COMMAND acgc_metal_packet_consumer_v2_texture_tev_tests
    )
    add_test(
        NAME acgc_metal_packet_consumer_v2_runtime_sideband_tests
        COMMAND acgc_metal_packet_consumer_v2_runtime_sideband_tests
    )
    add_test(
        NAME acgc_metal_packet_consumer_v2_channel_source_tests
        COMMAND acgc_metal_packet_consumer_v2_channel_source_tests
    )
    add_test(
        NAME acgc_macos_host_self_test
        COMMAND acgc_macos_native_host_cli --self-test
    )
endif()
