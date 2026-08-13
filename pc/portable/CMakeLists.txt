cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(acgc_portable LANGUAGES C CXX)
    include(CTest)
endif()

add_library(acgc_portable STATIC
    src/address.c
    src/dvd_host_state.c
    src/gbi_reference_registry.c
    src/disc.c
    src/boot_source.c
    src/yaz0.c
)

add_library(acgc_gx_semantic_packet STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/gx_semantic_packet.c
)
target_compile_features(acgc_gx_semantic_packet PUBLIC c_std_11)
target_include_directories(acgc_gx_semantic_packet
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_executable(acgc_gx_semantic_packet_tests
    tests/test_gx_semantic_packet.c
)
target_compile_features(acgc_gx_semantic_packet_tests PRIVATE c_std_11)
target_include_directories(acgc_gx_semantic_packet_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_link_libraries(acgc_gx_semantic_packet_tests PRIVATE acgc_gx_semantic_packet)

add_executable(acgc_gx_semantic_packet_cpp_tests
    tests/test_gx_semantic_packet_cpp.cpp
)
target_compile_features(acgc_gx_semantic_packet_cpp_tests PRIVATE cxx_std_11)
target_include_directories(acgc_gx_semantic_packet_cpp_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_link_libraries(acgc_gx_semantic_packet_cpp_tests PRIVATE acgc_gx_semantic_packet)

if(MSVC)
    target_compile_options(acgc_gx_semantic_packet PRIVATE /W4)
    target_compile_options(acgc_gx_semantic_packet_tests PRIVATE /W4)
    target_compile_options(acgc_gx_semantic_packet_cpp_tests PRIVATE /W4)
else()
    target_compile_options(acgc_gx_semantic_packet PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(acgc_gx_semantic_packet_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(acgc_gx_semantic_packet_cpp_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_test(
        NAME acgc_gx_semantic_packet_tests
        COMMAND acgc_gx_semantic_packet_tests
    )
    add_test(
        NAME acgc_gx_semantic_packet_cpp_tests
        COMMAND acgc_gx_semantic_packet_cpp_tests
    )
endif()

add_executable(acgc_pc_abi_probe
    tests/pc_abi_probe.c
)
target_compile_definitions(acgc_pc_abi_probe PRIVATE TARGET_PC)
target_include_directories(acgc_pc_abi_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(acgc_npc_actor_slot_probe
    tests/npc_actor_slot_probe.c
)
target_compile_definitions(acgc_npc_actor_slot_probe PRIVATE
    TARGET_PC
    VERSION=0
    F3DEX_GBI_2
    _LANGUAGE_C
)
target_compile_features(acgc_npc_actor_slot_probe PRIVATE c_std_11)
target_include_directories(acgc_npc_actor_slot_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(acgc_card_public_abi_c_probe OBJECT
    tests/card_public_abi_c_probe.c
)
target_compile_definitions(acgc_card_public_abi_c_probe PRIVATE TARGET_PC)
target_include_directories(acgc_card_public_abi_c_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/static
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_library(acgc_card_public_abi_cpp_probe OBJECT
    tests/card_public_abi_cpp_probe.cpp
)
target_compile_definitions(acgc_card_public_abi_cpp_probe PRIVATE TARGET_PC)
target_compile_features(acgc_card_public_abi_cpp_probe PRIVATE cxx_std_17)
target_include_directories(acgc_card_public_abi_cpp_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/static
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_library(acgc_static_gbi_compile_c_probe OBJECT
    tests/static_gbi_compile_c_probe.c
)
target_compile_definitions(acgc_static_gbi_compile_c_probe PRIVATE
    TARGET_PC
    VERSION=0
    F3DEX_GBI_2
    _LANGUAGE_C
)
target_include_directories(acgc_static_gbi_compile_c_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/static
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(MSVC)
    target_compile_options(acgc_static_gbi_compile_c_probe PRIVATE /W4)
else()
    target_compile_options(acgc_static_gbi_compile_c_probe PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(acgc_static_gbi_compile_cpp_probe OBJECT
    tests/static_gbi_compile_cpp_probe.cpp
)
target_compile_definitions(acgc_static_gbi_compile_cpp_probe PRIVATE
    TARGET_PC
    VERSION=0
    F3DEX_GBI_2
    _LANGUAGE_C_PLUS_PLUS
)
target_compile_features(acgc_static_gbi_compile_cpp_probe PRIVATE cxx_std_17)
target_include_directories(acgc_static_gbi_compile_cpp_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/static
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(MSVC)
    target_compile_options(acgc_static_gbi_compile_cpp_probe PRIVATE /W4)
else()
    target_compile_options(acgc_static_gbi_compile_cpp_probe PRIVATE -Wall -Wextra)
endif()

add_library(acgc_static_gbi_legacy_c_probe OBJECT
    tests/static_gbi_legacy_c_probe.c
)
target_compile_definitions(acgc_static_gbi_legacy_c_probe PRIVATE
    TARGET_PC
    VERSION=0
    _LANGUAGE_C
)
target_include_directories(acgc_static_gbi_legacy_c_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/static
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(MSVC)
    target_compile_options(acgc_static_gbi_legacy_c_probe PRIVATE /W4)
else()
    target_compile_options(acgc_static_gbi_legacy_c_probe PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_compile_features(acgc_portable PUBLIC c_std_11)
target_include_directories(acgc_portable
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(acgc_portable PRIVATE /W4)
else()
    target_compile_options(acgc_portable PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND BUILD_TESTING)
    add_executable(acgc_portable_tests
        tests/test_portable.c
    )
    target_link_libraries(acgc_portable_tests PRIVATE acgc_portable)

    if(MSVC)
        target_compile_options(acgc_portable_tests PRIVATE /W4)
    else()
        target_compile_options(acgc_portable_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME acgc_portable_tests COMMAND acgc_portable_tests)

    add_executable(acgc_boot_source_tests
        tests/test_boot_source.c
    )
    target_link_libraries(acgc_boot_source_tests PRIVATE acgc_portable)
    if(MSVC)
        target_compile_options(acgc_boot_source_tests PRIVATE /W4)
    else()
        target_compile_options(acgc_boot_source_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME acgc_boot_source_tests COMMAND acgc_boot_source_tests)

    add_executable(acgc_boot_source_cpp_header_tests
        tests/test_boot_source_cpp.cpp
    )
    target_link_libraries(acgc_boot_source_cpp_header_tests PRIVATE acgc_portable)
    target_compile_features(acgc_boot_source_cpp_header_tests PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(acgc_boot_source_cpp_header_tests PRIVATE /W4)
    else()
        target_compile_options(acgc_boot_source_cpp_header_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(
        NAME acgc_boot_source_cpp_header_tests
        COMMAND acgc_boot_source_cpp_header_tests
    )

    add_executable(acgc_gbi_runtime_tests
        tests/test_gbi_runtime.c
        ../src/pc_gbi_runtime.c
    )
    target_compile_definitions(acgc_gbi_runtime_tests PRIVATE
        TARGET_PC
        VERSION=0
        F3DEX_GBI_2
        _LANGUAGE_C
        NDEBUG
    )
    target_include_directories(acgc_gbi_runtime_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )
    target_link_libraries(acgc_gbi_runtime_tests PRIVATE acgc_portable)
    if(MSVC)
        target_compile_options(acgc_gbi_runtime_tests PRIVATE /W4)
    else()
        target_compile_options(acgc_gbi_runtime_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME acgc_gbi_runtime_tests COMMAND acgc_gbi_runtime_tests)

    add_executable(acgc_emu64_seg2k0_tests
        tests/test_emu64_seg2k0.cpp
        ../src/pc_gbi_runtime.c
        ../../src/static/libforest/emu64/emu64_utility.c
    )
    target_compile_definitions(acgc_emu64_seg2k0_tests PRIVATE
        TARGET_PC
        VERSION=0
        F3DEX_GBI_2
        _LANGUAGE_C
        NDEBUG
    )
    target_compile_features(acgc_emu64_seg2k0_tests PRIVATE cxx_std_17)
    target_include_directories(acgc_emu64_seg2k0_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
    )
    set_source_files_properties(
        ../../src/static/libforest/emu64/emu64_utility.c
        PROPERTIES
            LANGUAGE CXX
            COMPILE_OPTIONS "-ffunction-sections"
    )
    if(MSVC)
        target_compile_options(acgc_emu64_seg2k0_tests PRIVATE /W4)
    else()
        target_compile_options(acgc_emu64_seg2k0_tests PRIVATE -Wall -Wextra -Wpedantic -Wno-register)
        if(APPLE)
            target_link_options(acgc_emu64_seg2k0_tests PRIVATE -Wl,-dead_strip)
        else()
            target_link_options(acgc_emu64_seg2k0_tests PRIVATE -Wl,--gc-sections)
        endif()
    endif()
    target_link_libraries(acgc_emu64_seg2k0_tests PRIVATE acgc_portable)
    add_test(NAME acgc_emu64_seg2k0_tests COMMAND acgc_emu64_seg2k0_tests)

    if(APPLE)
        add_executable(acgc_emu64_gbi_traversal_tests
            tests/test_emu64_gbi_traversal.cpp
            tests/emu64_test_stubs.c
            ../src/pc_gbi_runtime.c
            ../../src/static/libforest/emu64/emu64.c
        )
        target_compile_definitions(acgc_emu64_gbi_traversal_tests PRIVATE
            TARGET_PC
            PC_DARWIN_COMPILE_AUDIT
            PC_ENHANCEMENTS
            VERSION=0
            F3DEX_GBI_2
            _LANGUAGE_C
            NDEBUG
        )
        target_compile_features(acgc_emu64_gbi_traversal_tests PRIVATE cxx_std_17)
        find_path(ACGC_SDL2_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2)
        target_include_directories(acgc_emu64_gbi_traversal_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../../include
            ${CMAKE_CURRENT_SOURCE_DIR}/../../src
            ${CMAKE_CURRENT_SOURCE_DIR}/../include
            ${CMAKE_CURRENT_SOURCE_DIR}/../lib/glad/include
            ${ACGC_SDL2_INCLUDE_DIR}
        )
        set_source_files_properties(
            ../../src/static/libforest/emu64/emu64.c
            PROPERTIES
                LANGUAGE CXX
        )
        if(MSVC)
            target_compile_options(acgc_emu64_gbi_traversal_tests PRIVATE /W4)
        else()
            target_compile_options(acgc_emu64_gbi_traversal_tests PRIVATE
                -Wall -Wextra -Wpedantic -fms-extensions -Wno-register
                -ffunction-sections -fdata-sections
            )
            target_link_options(acgc_emu64_gbi_traversal_tests PRIVATE -Wl,-dead_strip)
        endif()
        target_link_libraries(acgc_emu64_gbi_traversal_tests PRIVATE acgc_portable)
        add_test(NAME acgc_emu64_gbi_traversal_tests COMMAND acgc_emu64_gbi_traversal_tests)
    endif()

    add_executable(acgc_dvd_host_state_tests
        tests/test_dvd_host_state.c
    )
    target_link_libraries(acgc_dvd_host_state_tests PRIVATE acgc_portable)
    if(MSVC)
        target_compile_options(acgc_dvd_host_state_tests PRIVATE /W4)
    else()
        target_compile_options(acgc_dvd_host_state_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME acgc_dvd_host_state_tests COMMAND acgc_dvd_host_state_tests)

    add_executable(acgc_dvd_public_abi_c_tests
        tests/test_dvd_public_abi.c
    )
    target_include_directories(acgc_dvd_public_abi_c_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    )
    target_link_libraries(acgc_dvd_public_abi_c_tests PRIVATE acgc_portable)
    if(MSVC)
        target_compile_options(acgc_dvd_public_abi_c_tests PRIVATE /W4)
    else()
        target_compile_options(acgc_dvd_public_abi_c_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME acgc_dvd_public_abi_c_tests COMMAND acgc_dvd_public_abi_c_tests)

    add_executable(acgc_dvd_public_abi_cpp_tests
        tests/test_dvd_public_abi.cpp
    )
    target_include_directories(acgc_dvd_public_abi_cpp_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    )
    target_compile_features(acgc_dvd_public_abi_cpp_tests PRIVATE cxx_std_17)
    target_link_libraries(acgc_dvd_public_abi_cpp_tests PRIVATE acgc_portable)
    if(MSVC)
        target_compile_options(acgc_dvd_public_abi_cpp_tests PRIVATE /W4)
    else()
        target_compile_options(acgc_dvd_public_abi_cpp_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME acgc_dvd_public_abi_cpp_tests COMMAND acgc_dvd_public_abi_cpp_tests)
endif()

add_executable(acgc_pc_libc_memory_probe_c
    tests/pc_libc_memory_probe.c
)
target_compile_definitions(acgc_pc_libc_memory_probe_c PRIVATE
    TARGET_PC
    VERSION=0
    F3DEX_GBI_2
    _LANGUAGE_C
)
target_include_directories(acgc_pc_libc_memory_probe_c PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(acgc_pc_libc_memory_probe_cpp
    tests/pc_libc_memory_probe.cpp
)
target_compile_definitions(acgc_pc_libc_memory_probe_cpp PRIVATE
    TARGET_PC
    VERSION=0
    F3DEX_GBI_2
    _LANGUAGE_C_PLUS_PLUS
)
target_compile_features(acgc_pc_libc_memory_probe_cpp PRIVATE cxx_std_17)
target_include_directories(acgc_pc_libc_memory_probe_cpp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(acgc_jkr_aram_stream_abi_probe
    tests/jkr_aram_stream_abi_probe.cpp
)
target_compile_definitions(acgc_jkr_aram_stream_abi_probe PRIVATE TARGET_PC)
target_compile_features(acgc_jkr_aram_stream_abi_probe PRIVATE cxx_std_17)
target_include_directories(acgc_jkr_aram_stream_abi_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(acgc_jkr_exp_heap_probe
    tests/jkr_exp_heap_probe.cpp
    ../../src/static/JSystem/JKernel/JKRDisposer.cpp
    ../../src/static/JSystem/JKernel/JKRExpHeap.cpp
    ../../src/static/JSystem/JKernel/JKRHeap.cpp
    ../../src/static/JSystem/JSupport/JSUList.cpp
)
target_compile_definitions(acgc_jkr_exp_heap_probe PRIVATE TARGET_PC VERSION=0)
target_compile_features(acgc_jkr_exp_heap_probe PRIVATE cxx_std_17)
target_include_directories(acgc_jkr_exp_heap_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/static
)
if(MSVC)
    target_compile_options(acgc_jkr_exp_heap_probe PRIVATE /W4)
else()
    target_compile_options(acgc_jkr_exp_heap_probe PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

add_executable(acgc_jsu_stream_enum_probe
    tests/jsu_stream_enum_probe.cpp
)
target_compile_definitions(acgc_jsu_stream_enum_probe PRIVATE TARGET_PC)
target_compile_features(acgc_jsu_stream_enum_probe PRIVATE cxx_std_17)
target_include_directories(acgc_jsu_stream_enum_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND BUILD_TESTING)
    add_test(NAME acgc_npc_actor_slot_probe COMMAND acgc_npc_actor_slot_probe)
    add_test(NAME acgc_pc_libc_memory_probe_c COMMAND acgc_pc_libc_memory_probe_c)
    add_test(NAME acgc_pc_libc_memory_probe_cpp COMMAND acgc_pc_libc_memory_probe_cpp)
    add_test(NAME acgc_jkr_aram_stream_abi_probe COMMAND acgc_jkr_aram_stream_abi_probe)
    add_test(NAME acgc_jkr_exp_heap_probe COMMAND acgc_jkr_exp_heap_probe)
    add_test(NAME acgc_jsu_stream_enum_probe COMMAND acgc_jsu_stream_enum_probe)
endif()
