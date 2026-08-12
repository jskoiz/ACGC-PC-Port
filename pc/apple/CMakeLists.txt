cmake_minimum_required(VERSION 3.16)

project(acgc_macos_native_host LANGUAGES C OBJC)

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

# Keep the legacy pc/CMakeLists.txt independent. This entrypoint consumes only
# the existing dependency-free portable target.
add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../portable"
    "${CMAKE_CURRENT_BINARY_DIR}/acgc_portable"
)

add_library(acgc_macos_host_core STATIC
    src/host.c
)
target_compile_features(acgc_macos_host_core PUBLIC c_std_11)
target_include_directories(acgc_macos_host_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(acgc_macos_host_core PUBLIC acgc_portable)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(acgc_macos_host_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(ACGC_MACOS_HOST_FRAMEWORKS
    ${APPKIT_FRAMEWORK}
    ${FOUNDATION_FRAMEWORK}
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

if(BUILD_TESTING)
    add_test(NAME acgc_macos_host_core_tests COMMAND acgc_macos_host_core_tests)
    add_test(
        NAME acgc_macos_host_self_test
        COMMAND acgc_macos_native_host_cli --self-test
    )
endif()
