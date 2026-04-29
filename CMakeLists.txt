cmake_minimum_required(VERSION 3.28)

# Version selection is intentionally CI-aware so release packaging can inject
# a tag without forcing local developers to edit the source tree.
set(EXPP_DEFAULT_VERSION "0.0.1")

if(DEFINED CI_VERSION)
    set(expp_raw_version "${CI_VERSION}")
    string(REGEX REPLACE "^v" "" expp_raw_version "${expp_raw_version}")
    string(REGEX MATCH "([0-9][0-9]*\\.[0-9][0-9]*\\.[0-9][0-9]*)" expp_semver "${expp_raw_version}")
    if(expp_semver STREQUAL "")
        set(APP_VERSION ${EXPP_DEFAULT_VERSION})
    else()
        set(APP_VERSION "${expp_semver}")
    endif()
else()
    set(APP_VERSION ${EXPP_DEFAULT_VERSION})
endif()

if(NOT APP_VERSION MATCHES "^[0-9][0-9]*\\.[0-9][0-9]*\\.[0-9][0-9]*$")
    set(APP_VERSION ${EXPP_DEFAULT_VERSION})
endif()

project(Expp
    VERSION ${APP_VERSION}
        DESCRIPTION "Cross-platform TUI file manager built with C++23"
        HOMEPAGE_URL "https://github.com/Mintinson/expp"
        LANGUAGES CXX C
)

# ==============================================================================
# Strip default MSVC Exception Handling flags injected by CMake
# ==============================================================================
if(MSVC)
    # iterate over common CMake compiler flag variables
    foreach(flag_var
            CMAKE_CXX_FLAGS
            CMAKE_CXX_FLAGS_DEBUG
            CMAKE_CXX_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_MINSIZEREL
            CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        if(${flag_var} MATCHES "/EHsc")
            string(REPLACE "/EHsc" "" ${flag_var} "${${flag_var}}")
        endif()
    endforeach()
endif()

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
option(EXPP_BUILD_EXAMPLES "Build example programs" OFF)
option(EXPP_ENABLE_SANITIZERS "Enable sanitizers in Debug builds" ON)
option(EXPP_ENABLE_CLANG_TIDY "Enable clang-tidy checks" OFF)
option(EXPP_ENABLE_CPPCHECK "Enable cppcheck static analysis" OFF)
option(EXPP_ENABLE_LIBMAGIC "Use libmagic for MIME sniffing when available" OFF)
option(EXPP_PREPARE_IO_URING_BACKEND "Enable Linux io_uring extension hooks" ON)


set(EXPP_IO_URING_COMPILED_IN 0)
if(UNIX AND NOT APPLE AND EXPP_PREPARE_IO_URING_BACKEND)
    # The macro is normalized to 0/1 once here so C++ code can make simple
    # compile-time decisions without repeating platform conditionals.
    set(EXPP_IO_URING_COMPILED_IN 1)
endif()

# ==============================================================================
# Project-wide ABI and Language policy
# ==============================================================================
# expp_project_options carries project-wide ABI and language policy.
# It intentionally owns the exception mode flags so every first-party target
# shares one consistent contract.
add_library(expp_project_options INTERFACE)
target_compile_features(expp_project_options INTERFACE cxx_std_23)
target_compile_definitions(expp_project_options INTERFACE
    # $<$<BOOL:${EXPP_DISABLE_EXCEPTIONS}>:TOML_EXCEPTIONS=0>
    TOML_EXCEPTIONS=0       # toml++ is configured to never throw, so this is a global definition rather than a conditional one.
    EXPP_PREPARE_IO_URING_BACKEND=${EXPP_IO_URING_COMPILED_IN}
    $<$<AND:$<BOOL:${EXPP_DISABLE_EXCEPTIONS}>,$<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},MSVC>>:_HAS_EXCEPTIONS=0>
)
target_compile_options(expp_project_options INTERFACE
    # Frontend-variant checks keep clang-cl aligned with cl.exe while still
    # allowing GCC/Clang-on-Unix to use their native flags.
    $<$<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},MSVC>:
        /permissive-
        /Zc:__cplusplus
        /utf-8
        $<$<BOOL:${EXPP_DISABLE_EXCEPTIONS}>:/EHs-c->
        $<$<NOT:$<BOOL:${EXPP_DISABLE_EXCEPTIONS}>>:/EHsc>
    >
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>
    $<$<AND:$<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},GNU>,$<BOOL:${EXPP_DISABLE_EXCEPTIONS}>>:-fno-exceptions>
)

# ==============================================================================
# Compiler warnings (strict by default)
# ==============================================================================
add_library(expp_warnings INTERFACE)

target_compile_options(expp_warnings INTERFACE
        # only for  MSVC fronter (cl.exe or clang-cl.exe)
        $<$<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},MSVC>:
        /W4
        /WX
        >
        # only for the real cl.exe, clang-cl doesn't need this
        $<$<CXX_COMPILER_ID:MSVC>:
        /Zc:preprocessor
        >
        # match the GNU frontend (gcc, clang on Linux/Mac)
        $<$<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},GNU>:
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
if(EXPP_ENABLE_SANITIZERS)
    # Sanitizers are only meaningful for debug-style builds. Generator
    # expressions keep multi-config and single-config generators aligned.
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(expp_sanitizers INTERFACE
            $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:-fsanitize=address,undefined>
            $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:-fno-omit-frame-pointer>
        )
        target_link_options(expp_sanitizers INTERFACE
            $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:-fsanitize=address,undefined>
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(expp_sanitizers INTERFACE
            $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/fsanitize=address>
        )
        target_link_options(expp_sanitizers INTERFACE
            $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/fsanitize=address>
        )
    endif()
endif()


# ==============================================================================
# A Helper Function to collect common target properties for first-party libraries
# ==============================================================================
function(expp_apply_target_defaults target)
    # Every first-party library gets the same include/install layout and the
    # same compile/link policy. This keeps the target declarations below small
    # and makes future library additions less error-prone.
    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(${target}
        PRIVATE
            expp_project_options
            expp_warnings
            expp_sanitizers
    )
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()


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


# ==============================================================================
# Asio (For Async Runtime)
# ==============================================================================
add_library(expp_asio INTERFACE)
target_include_directories(expp_asio SYSTEM INTERFACE "${asio_src_SOURCE_DIR}/include")
target_compile_definitions(expp_asio INTERFACE 
    ASIO_STANDALONE 
    ASIO_NO_DEPRECATED 
    $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=0x0601>
    # Asio's coroutine and service code must see the same no-exception mode as
    # our runtime; otherwise exception_ptr-based and no-throw code paths diverge.
    $<$<BOOL:${EXPP_DISABLE_EXCEPTIONS}>:ASIO_NO_EXCEPTIONS>
)
target_link_libraries(expp_asio INTERFACE Threads::Threads)
target_compile_options(expp_asio INTERFACE
    # These suppressions are scoped to Asio headers only. Project code still
    # builds under the stricter warning profile above.
    $<$<CXX_COMPILER_ID:GNU>:-Wno-null-dereference>
    $<$<CXX_COMPILER_ID:MSVC>:/wd4702>
)

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

# if (UNIX AND NOT APPLE AND EXPP_PREPARE_IO_URING_BACKEND)
#     target_compile_definitions(expp_asio INTERFACE EXPP_PREPARE_IO_URING_BACKEND=1)
# else ()
#     target_compile_definitions(expp_asio INTERFACE EXPP_PREPARE_IO_URING_BACKEND=0)
# endif ()



# if (EXPP_ENABLE_SANITIZERS)
#     if (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
#         if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
#             target_compile_options(expp_sanitizers INTERFACE
#                     -fsanitize=address,undefined
#                     -fno-omit-frame-pointer
#             )
#             target_link_options(expp_sanitizers INTERFACE
#                     -fsanitize=address,undefined
#             )
#             message(STATUS "AddressSanitizer and UBSan enabled for Debug build")
#             if (TARGET Catch2)
#                 target_compile_options(Catch2 PRIVATE                    
#                     -fsanitize=address,undefined
#                     -fno-omit-frame-pointer
#                 )
#                 target_link_options(Catch2 INTERFACE
#                     -fsanitize=address,undefined
#                 )
#             endif ()
#             if (TARGET Catch2WithMain)
#                   target_compile_options(Catch2WithMain PRIVATE                    
#                     -fsanitize=address,undefined
#                     -fno-omit-frame-pointer
#                 )
#                 target_link_options(Catch2WithMain INTERFACE
#                     -fsanitize=address,undefined
#                 )
#             endif ()
#         elseif (MSVC)
#             target_compile_options(expp_sanitizers INTERFACE /fsanitize=address)
#             message(STATUS "AddressSanitizer enabled for Debug build (MSVC)")
#             if (TARGET Catch2)
#                 target_compile_options(Catch2 PRIVATE /fsanitize=address)
#             endif ()
#             if (TARGET Catch2WithMain)
#                 target_compile_options(Catch2WithMain PRIVATE /fsanitize=address)
#             endif ()
#         endif ()
#         # endif ()
#     endif ()
# endif ()


# --- FIX: Synchronize MSVC ABI flags for FTXUI ---
# if (MSVC)
#     # Apply strict standard conformance flags to match expp_warnings
#     foreach (ftxui_target screen dom component)
#         target_compile_options(${ftxui_target} PRIVATE /permissive- /Zc:__cplusplus /Zc:preprocessor)
#         target_compile_options(${ftxui_target} PRIVATE /EHsc)

#         # Apply AddressSanitizer if enabled to match expp_sanitizers
#         if (EXPP_ENABLE_SANITIZERS AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
#             target_compile_options(${ftxui_target} PRIVATE /fsanitize=address)
#         endif ()
#     endforeach ()
# endif ()
# --- FIX: Synchronize MSVC ABI flags for FTXUI ---
if (MSVC)
    # Apply strict standard conformance flags to match expp_warnings
    foreach (ftxui_target screen dom component)
        target_compile_options(${ftxui_target} PRIVATE /permissive- /Zc:__cplusplus /EHsc)
        
        if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            target_compile_options(${ftxui_target} PRIVATE /Zc:preprocessor)
        endif()

        # Apply AddressSanitizer if enabled to match expp_sanitizers
        if (EXPP_ENABLE_SANITIZERS AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
            target_compile_options(${ftxui_target} PRIVATE /fsanitize=address)
        endif ()
    endforeach ()

    # # Keep Catch2 STL annotation settings aligned with sanitized test/object files.
    # if (EXPP_BUILD_TESTS AND EXPP_ENABLE_SANITIZERS AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
    #     if (TARGET Catch2)
    #         target_compile_options(Catch2 PRIVATE /fsanitize=address)
    #     endif ()
    #     if (TARGET Catch2WithMain)
    #         target_compile_options(Catch2WithMain PRIVATE /fsanitize=address)
    #     endif ()
    # endif ()
endif ()


# ==============================================================================
# Core library
# ==============================================================================
add_library(expp_core STATIC
        src/core/filesystem.cpp
        src/core/config.cpp
        src/core/async_runtime.cpp
)
expp_apply_target_defaults(expp_core)
# target_compile_definitions(tomlplusplus INTERFACE TOML_EXCEPTIONS=0)
target_link_libraries(expp_core
    PUBLIC
        expp_asio
        expp_magic
        tomlplusplus::tomlplusplus
)
# Platform-specific libraries for trash functionality
if (WIN32)
    target_link_libraries(expp_core PRIVATE shell32)
endif ()

# target_include_directories(expp_core
#         PUBLIC
#         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
#         $<INSTALL_INTERFACE:include>
# )

# target_link_libraries(expp_core
#         PUBLIC
#         tomlplusplus::tomlplusplus
#         expp_asio
#         expp_magic
#         PRIVATE
#         expp_warnings
#         expp_sanitizers
# )
#     if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
#         target_compile_options(expp_core PRIVATE -Wno-null-dereference)
#     endif ()
# set_target_properties(expp_core PROPERTIES
#         POSITION_INDEPENDENT_CODE ON
# )


# ==============================================================================
# UI library
# ==============================================================================
add_library(expp_ui STATIC
        src/ui/key_handler.cpp
        src/ui/theme.cpp
        src/ui/components.cpp
        src/ui/help_menu_model.cpp
)
expp_apply_target_defaults(expp_ui)
target_link_libraries(expp_ui
    PUBLIC
        expp_core
        ftxui::screen
        ftxui::dom
        ftxui::component
)
# target_include_directories(expp_ui
#         PUBLIC
#         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
#         $<INSTALL_INTERFACE:include>
# )
# target_link_libraries(expp_ui
#         PUBLIC
#         expp_core
#         ftxui::screen
#         ftxui::dom
#         ftxui::component
#         PRIVATE
#         expp_warnings
#         expp_sanitizers
# )

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
expp_apply_target_defaults(expp_app)
target_link_libraries(expp_app PUBLIC expp_ui)
# target_include_directories(expp_app
#         PUBLIC
#         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
#         $<INSTALL_INTERFACE:include>
# )
# target_link_libraries(expp_app
#         PUBLIC
#         expp_ui
#         PRIVATE
#         expp_warnings
#         expp_sanitizers
# )
# TODO: Is this necessay? (Same as other -Wno-null-dereference)
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
        expp_project_options
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
    # Tests are split into their own subdirectory so they can reuse the same
    # first-party libraries without complicating the main application targets.
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
# Packaging stays intentionally minimal here: the project currently produces one
# executable plus installed headers, and the archive format follows the host
# platform convention.
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
message(STATUS "Version:             ${PROJECT_VERSION}")
message(STATUS "Build type:          ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Compiler:        ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Disable exceptions:  ${EXPP_DISABLE_EXCEPTIONS}")
message(STATUS "Build tests:         ${EXPP_BUILD_TESTS}")
message(STATUS "Build examples:      ${EXPP_BUILD_EXAMPLES}")
message(STATUS "Sanitizers:          ${EXPP_ENABLE_SANITIZERS}")
message(STATUS "clang-tidy:          ${EXPP_ENABLE_CLANG_TIDY}")
message(STATUS "libmagic:            ${EXPP_ENABLE_LIBMAGIC}")
message(STATUS "")
