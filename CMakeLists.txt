cmake_minimum_required(VERSION 3.28)

set(EXPP_DEFAULT_VERSION "0.0.1")

# 2. if GitHub Action pass a variable CI_VERSION, use it as the version; otherwise, use the default version
if(DEFINED CI_VERSION)
    set(APP_VERSION ${CI_VERSION})
else()
    set(APP_VERSION ${EXPP_DEFAULT_VERSION})
endif()

project(Expp
        VERSION ${EXPP_DEFAULT_VERSION}
        DESCRIPTION "Cross-platform TUI file manager built with C++23"
        HOMEPAGE_URL "https://github.com/Mintinson/expp"
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
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif ()

# ==============================================================================
# Options
# ==============================================================================
option(EXPP_BUILD_TESTS "Build test suite" ON)
option(EXPP_BUILD_EXAMPLES "Build example programs" ON)
option(EXPP_ENABLE_SANITIZERS "Enable sanitizers in Debug builds" ON)
option(EXPP_ENABLE_CLANG_TIDY "Enable clang-tidy checks" OFF)
option(EXPP_ENABLE_CPPCHECK "Enable cppcheck static analysis" OFF)
option(EXPP_ENABLE_LIBMAGIC "Use libmagic for MIME sniffing when available" ON)
option(EXPP_PREPARE_IO_URING_BACKEND "Enable Linux io_uring extension hooks" ON)

# ==============================================================================
# Compiler warnings (strict by default)
# ==============================================================================
add_library(expp_warnings INTERFACE)
target_compile_options(expp_warnings INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:
        /W4
        /WX
        /EHsc
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
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
        >
)

# ==============================================================================
# Sanitizers (Debug builds only)
# ==============================================================================
add_library(expp_sanitizers INTERFACE)
if (EXPP_ENABLE_SANITIZERS)
    if (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(expp_sanitizers INTERFACE
                    -fsanitize=address,undefined
                    -fno-omit-frame-pointer
            )
            target_link_options(expp_sanitizers INTERFACE
                    -fsanitize=address,undefined
            )
            message(STATUS "AddressSanitizer and UBSan enabled for Debug build")
        elseif (MSVC)
            target_compile_options(expp_sanitizers INTERFACE /fsanitize=address)
            message(STATUS "AddressSanitizer enabled for Debug build (MSVC)")
        endif ()
    endif ()
endif ()


# ==============================================================================
# Dependencies via FetchContent
# ==============================================================================
include(FetchContent)
find_package(Threads REQUIRED)

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
if (EXPP_BUILD_TESTS)
    FetchContent_Declare(Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2
            GIT_TAG v3.5.2
    )
endif ()

# Asio - standalone async runtime
FetchContent_Declare(asio_src
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio
        GIT_TAG asio-1-38-0
)


FetchContent_MakeAvailable(ftxui tomlplusplus asio_src)
if (EXPP_BUILD_TESTS)
    FetchContent_MakeAvailable(Catch2)
endif ()

add_library(expp_asio INTERFACE)
target_include_directories(expp_asio SYSTEM INTERFACE "${asio_src_SOURCE_DIR}/include")
target_compile_definitions(expp_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED _WIN32_WINNT=0x0601)
target_link_libraries(expp_asio INTERFACE Threads::Threads)
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC may report false positives in standalone Asio internals with -Wnull-dereference.
    # Suppress this warning for Asio headers only, while keeping strict warnings for project code.
    target_compile_options(expp_asio INTERFACE -Wno-null-dereference)
endif ()

add_library(expp_magic INTERFACE)
if (EXPP_ENABLE_LIBMAGIC)
    find_path(LIBMAGIC_INCLUDE_DIR NAMES magic.h)
    find_library(LIBMAGIC_LIBRARY NAMES magic libmagic)
    if (LIBMAGIC_INCLUDE_DIR AND LIBMAGIC_LIBRARY)
        target_include_directories(expp_magic INTERFACE "${LIBMAGIC_INCLUDE_DIR}")
        target_link_libraries(expp_magic INTERFACE "${LIBMAGIC_LIBRARY}")
        target_compile_definitions(expp_magic INTERFACE EXPP_HAS_LIBMAGIC=1)
    else ()
        target_compile_definitions(expp_magic INTERFACE EXPP_HAS_LIBMAGIC=0)
    endif ()
else ()
    target_compile_definitions(expp_magic INTERFACE EXPP_HAS_LIBMAGIC=0)
endif ()

if (UNIX AND NOT APPLE AND EXPP_PREPARE_IO_URING_BACKEND)
    target_compile_definitions(expp_asio INTERFACE EXPP_PREPARE_IO_URING_BACKEND=1)
else ()
    target_compile_definitions(expp_asio INTERFACE EXPP_PREPARE_IO_URING_BACKEND=0)
endif ()

# --- FIX: Synchronize MSVC ABI flags for FTXUI ---
if (MSVC)
    # Apply strict standard conformance flags to match expp_warnings
    foreach (ftxui_target screen dom component)
        target_compile_options(${ftxui_target} PRIVATE /permissive- /Zc:__cplusplus /Zc:preprocessor)
        target_compile_options(${ftxui_target} PRIVATE /EHsc)

        # Apply AddressSanitizer if enabled to match expp_sanitizers
        if (EXPP_ENABLE_SANITIZERS AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
            target_compile_options(${ftxui_target} PRIVATE /fsanitize=address)
        endif ()
    endforeach ()
endif ()

# ==============================================================================
# Core library
# ==============================================================================
add_library(expp_core STATIC
        src/core/filesystem.cpp
        src/core/config.cpp
        src/core/async_runtime.cpp
)


target_include_directories(expp_core
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(expp_core
        PUBLIC
        tomlplusplus::tomlplusplus
        expp_asio
        expp_magic
        PRIVATE
        expp_warnings
        expp_sanitizers
)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(expp_core PRIVATE -Wno-null-dereference)
    endif ()
set_target_properties(expp_core PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)

# Platform-specific libraries for trash functionality
if (WIN32)
    target_link_libraries(expp_core PRIVATE shell32)
endif ()


# ==============================================================================
# UI library
# ==============================================================================
add_library(expp_ui STATIC
        src/ui/key_handler.cpp
        src/ui/theme.cpp
        src/ui/components.cpp
        src/ui/help_menu_model.cpp
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
        src/app/notification_center.cpp
        src/app/navigation_utils.cpp
        src/app/explorer.cpp
        src/app/explorer_view.cpp
        src/app/explorer_services.cpp
        src/app/explorer_directory_controller.cpp
        src/app/explorer_mutation_controller.cpp
        src/app/explorer_commands.cpp
        src/app/explorer_presenter.cpp
        src/app/explorer_sort.cpp
        src/app/explorer_state_helpers.cpp
        src/app/explorer_command_dispatcher.cpp
        src/app/explorer_overlay_controller.cpp
        src/app/explorer_render_composer.cpp
        src/app/explorer_preview_controller.cpp
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
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(expp_app PRIVATE -Wno-null-dereference)
endif ()

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
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(expp_exe PRIVATE -Wno-null-dereference)
endif ()
set_target_properties(expp_exe PROPERTIES
        OUTPUT_NAME "explorer"
)

# ==============================================================================
# Tests
# ==============================================================================
if (EXPP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

# ==============================================================================
# Examples
# ==============================================================================
if (EXPP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif ()

# ==============================================================================
# clang-tidy integration
# ==============================================================================
if (EXPP_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if (CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_EXE})
        message(STATUS "clang-tidy enabled: ${CLANG_TIDY_EXE}")
    else ()
        message(WARNING "clang-tidy requested but not found")
    endif ()
endif ()


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
include(InstallRequiredSystemLibraries)
set(CPACK_PACKAGE_VENDOR "Expp")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
set(CPACK_GENERATOR "ZIP") # zip for Windows by default
if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "TGZ") # tar.gz for Linux
endif()
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
