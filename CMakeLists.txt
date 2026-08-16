cmake_minimum_required(VERSION 3.28)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(ExppVersion)
expp_resolve_version(EXPP_VERSION)

project(Expp
    VERSION ${EXPP_VERSION}
    DESCRIPTION "Cross-platform TUI file manager built with C++23"
    HOMEPAGE_URL "https://github.com/Mintinson/expp"
    LANGUAGES C CXX
)

include(GNUInstallDirs)
include(ExppOptions)
include(ExppCompilerPolicy)
include(ExppDependencies)
include(ExppPreviewFeatures)
include(ExppGit)
include(ExppTargets)

if(EXPP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(EXPP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(EXPP_ENABLE_BENCHMARKS)
    add_subdirectory(bench)
endif()

include(ExppPackaging)
include(ExppSummary)
