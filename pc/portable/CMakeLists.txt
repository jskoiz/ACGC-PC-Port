cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(acgc_portable LANGUAGES C)
    include(CTest)
endif()

add_library(acgc_portable STATIC
    src/address.c
    src/gbi_reference_registry.c
    src/disc.c
    src/yaz0.c
)

add_executable(acgc_pc_abi_probe
    tests/pc_abi_probe.c
)
target_compile_definitions(acgc_pc_abi_probe PRIVATE TARGET_PC)
target_include_directories(acgc_pc_abi_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

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

    add_executable(acgc_gbi_runtime_tests
        tests/test_gbi_runtime.c
        ../src/pc_gbi_runtime.c
    )
    target_link_libraries(acgc_gbi_runtime_tests PRIVATE acgc_portable)
    if(MSVC)
        target_compile_options(acgc_gbi_runtime_tests PRIVATE /W4)
    else()
        target_compile_options(acgc_gbi_runtime_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME acgc_gbi_runtime_tests COMMAND acgc_gbi_runtime_tests)
endif()
