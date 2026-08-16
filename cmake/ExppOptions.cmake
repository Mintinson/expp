include_guard(GLOBAL)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release MinSizeRel RelWithDebInfo
    )
endif()

option(EXPP_BUILD_TESTS "Build test suite" ON)
option(EXPP_BUILD_EXAMPLES "Build example programs" ON)
option(EXPP_ENABLE_BENCHMARKS "Build benchmarks" ON)
option(EXPP_DISABLE_EXCEPTIONS "Disable C++ exceptions in first-party targets" OFF)
option(EXPP_ENABLE_SANITIZERS "Enable sanitizers in debug builds" ON)
option(EXPP_ENABLE_CLANG_TIDY "Enable clang-tidy checks" OFF)
option(EXPP_ENABLE_CPPCHECK "Enable cppcheck static analysis" OFF)
option(EXPP_ENABLE_LIBMAGIC "Use libmagic for MIME sniffing when available" OFF)
option(EXPP_ENABLE_LIBARCHIVE "Use libarchive for archive previews when available" OFF)
option(EXPP_ENABLE_TREE_SITTER "Fetch Tree-sitter and bundled grammars for syntax highlighting" ON)
option(EXPP_ENABLE_TERMINAL_IMAGES "Enable terminal image protocol previews" ON)
option(EXPP_ENABLE_LIBGIT2 "Use libgit2 for Git status when available" ON)
option(EXPP_FETCH_LIBGIT2 "Fetch and build libgit2 when no package target is found" ON)
option(EXPP_PREPARE_IO_URING_BACKEND "Enable Linux io_uring extension hooks" ON)

set(EXPP_IO_URING_COMPILED_IN 0)
if(UNIX AND NOT APPLE AND EXPP_PREPARE_IO_URING_BACKEND)
    set(EXPP_IO_URING_COMPILED_IN 1)
endif()

# CMake injects /EHsc by default. The project policy target below owns the
# exception mode so every first-party target receives one consistent flag.
if(MSVC)
    foreach(flag_variable IN ITEMS
        CMAKE_CXX_FLAGS
        CMAKE_CXX_FLAGS_DEBUG
        CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_MINSIZEREL
        CMAKE_CXX_FLAGS_RELWITHDEBINFO
    )
        string(REPLACE "/EHsc" "" ${flag_variable} "${${flag_variable}}")
    endforeach()
endif()
