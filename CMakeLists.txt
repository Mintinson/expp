cmake_minimum_required(VERSION 3.28)

project(Expp
    VERSION 0.11.0
    DESCRIPTION "Cross-platform TUI file manager built with C++23"
    HOMEPAGE_URL "https://github.com/yourusername/Expp"
    LANGUAGES CXX
)

# ==============================================================================
# Global settings
# ==============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Default to Release build if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# ==============================================================================
# Options
# ==============================================================================
option(EXPP_BUILD_TESTS "Build test suite" ON)
option(EXPP_BUILD_EXAMPLES "Build example programs" ON)
option(EXPP_ENABLE_SANITIZERS "Enable sanitizers in Debug builds" ON)
option(EXPP_ENABLE_CLANG_TIDY "Enable clang-tidy checks" OFF)
option(EXPP_ENABLE_CPPCHECK "Enable cppcheck static analysis" OFF)

# ==============================================================================
# Compiler warnings (strict by default)
# ==============================================================================
add_library(expp_warnings INTERFACE)
target_compile_options(expp_warnings INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
    >
    $<$<CXX_COMPILER_ID:GNU>:
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Woverloaded-virtual
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
    >
    $<$<CXX_COMPILER_ID:Clang>:
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Woverloaded-virtual
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
    >
)

# ==============================================================================
# Sanitizers (Debug builds only)
# ==============================================================================
add_library(expp_sanitizers INTERFACE)
if(EXPP_ENABLE_SANITIZERS)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(expp_sanitizers INTERFACE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
            )
            target_link_options(expp_sanitizers INTERFACE
                -fsanitize=address,undefined
            )
            message(STATUS "AddressSanitizer and UBSan enabled for Debug build")
        elseif(MSVC)
            target_compile_options(expp_sanitizers INTERFACE /fsanitize=address)
            message(STATUS "AddressSanitizer enabled for Debug build (MSVC)")
        endif()
    endif()
endif()


# ==============================================================================
# Dependencies via FetchContent
# ==============================================================================
include(FetchContent)

# FTXUI - TUI library
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v6.1.9
)

# toml++ - Configuration parsing (header-only)
FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus
    GIT_TAG v3.4.0
)

# Catch2 - Testing framework
if(EXPP_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2
        GIT_TAG v3.5.2
    )
endif()


FetchContent_MakeAvailable(ftxui tomlplusplus)
if(EXPP_BUILD_TESTS)
    FetchContent_MakeAvailable(Catch2)
endif()

# --- FIX: Synchronize MSVC ABI flags for FTXUI ---
if(MSVC)
    # Apply strict standard conformance flags to match expp_warnings
    foreach(ftxui_target screen dom component)
        target_compile_options(${ftxui_target} PRIVATE /permissive- /Zc:__cplusplus /Zc:preprocessor)
        
        # Apply AddressSanitizer if enabled to match expp_sanitizers
        if(EXPP_ENABLE_SANITIZERS AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
            target_compile_options(${ftxui_target} PRIVATE /fsanitize=address)
        endif()
    endforeach()
endif()

# ==============================================================================
# Core library
# ==============================================================================
add_library(expp_core STATIC
    src/core/filesystem.cpp
    src/core/config.cpp
)


target_include_directories(expp_core
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(expp_core
    PUBLIC
        tomlplusplus::tomlplusplus
    PRIVATE
        expp_warnings
        expp_sanitizers
)
set_target_properties(expp_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# Platform-specific libraries for trash functionality
if(WIN32)
    target_link_libraries(expp_core PRIVATE shell32)
endif()


# ==============================================================================
# UI library
# ==============================================================================
add_library(expp_ui STATIC
    src/ui/key_handler.cpp
    src/ui/theme.cpp
    src/ui/components.cpp
)
target_include_directories(expp_ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(expp_ui
    PUBLIC
        expp_core
        ftxui::screen
        ftxui::dom
        ftxui::component
    PRIVATE
        expp_warnings
        expp_sanitizers
)

# ==============================================================================
# Application library
# ==============================================================================
add_library(expp_app STATIC
    src/app/explorer.cpp
    src/app/explorer_view.cpp
)
target_include_directories(expp_app
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(expp_app
    PUBLIC
        expp_ui
    PRIVATE
        expp_warnings
        expp_sanitizers
)

# ==============================================================================
# Main executable (refactored)
# ==============================================================================
add_executable(expp_exe src/main.cpp)
target_link_libraries(expp_exe
    PRIVATE
        expp_app
        expp_warnings
        expp_sanitizers
)
set_target_properties(expp_exe PROPERTIES
    OUTPUT_NAME "explorer"
)

# ==============================================================================
# Tests
# ==============================================================================
if(EXPP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ==============================================================================
# Examples
# ==============================================================================
if(EXPP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ==============================================================================
# clang-tidy integration
# ==============================================================================
if(EXPP_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_EXE})
        message(STATUS "clang-tidy enabled: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING "clang-tidy requested but not found")
    endif()
endif()


# ==============================================================================
# Installation
# ==============================================================================
include(GNUInstallDirs)

install(TARGETS expp_exe
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/expp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# ==============================================================================
# CPack configuration
# ==============================================================================
set(CPACK_PACKAGE_VENDOR "Expp")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
include(CPack)

# ==============================================================================
# Summary
# ==============================================================================
message(STATUS "")
message(STATUS "Expp Configuration Summary")
message(STATUS "================================")
message(STATUS "Version:           ${PROJECT_VERSION}")
message(STATUS "Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Compiler:      ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Build tests:       ${EXPP_BUILD_TESTS}")
message(STATUS "Build examples:    ${EXPP_BUILD_EXAMPLES}")
message(STATUS "Sanitizers:        ${EXPP_ENABLE_SANITIZERS}")
message(STATUS "clang-tidy:        ${EXPP_ENABLE_CLANG_TIDY}")
message(STATUS "")


add_executable(draft test.cpp)