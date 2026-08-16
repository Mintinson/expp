include_guard(GLOBAL)

add_library(expp_git2 INTERFACE)
set(EXPP_LIBGIT2_COMPILED_IN 0)

if(EXPP_ENABLE_LIBGIT2)
    find_package(libgit2 CONFIG QUIET)

    if(TARGET libgit2::libgit2)
        target_link_libraries(expp_git2 INTERFACE libgit2::libgit2)
        set(EXPP_LIBGIT2_COMPILED_IN 1)
    elseif(TARGET libgit2::git2)
        target_link_libraries(expp_git2 INTERFACE libgit2::git2)
        set(EXPP_LIBGIT2_COMPILED_IN 1)
    elseif(EXPP_FETCH_LIBGIT2)
        include(FetchContent)
        FetchContent_Declare(libgit2
            GIT_REPOSITORY https://github.com/libgit2/libgit2
            GIT_TAG v1.9.1
            GIT_SUBMODULES ""
        )

        set(BUILD_CLAR OFF CACHE BOOL "Build libgit2 tests" FORCE)
        set(BUILD_TESTS OFF CACHE BOOL "Build libgit2 tests" FORCE)
        set(BUILD_CLI OFF CACHE BOOL "Build libgit2 command-line tools" FORCE)
        set(BUILD_EXAMPLES OFF CACHE BOOL "Build libgit2 examples" FORCE)
        set(BUILD_FUZZERS OFF CACHE BOOL "Build libgit2 fuzzers" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
        set(USE_HTTPS OFF CACHE STRING "libgit2 HTTPS backend" FORCE)
        set(USE_SSH OFF CACHE STRING "libgit2 SSH backend" FORCE)

        if(UNIX AND NOT APPLE)
            set(GIT_USE_STAT_MTIM 1 CACHE INTERNAL "Force libgit2 stat mtim on Linux")
        elseif(APPLE)
            set(GIT_USE_STAT_MTIMESPEC 1 CACHE INTERNAL "Force libgit2 stat mtimespec on macOS")
        elseif(MSVC)
            set(STATIC_CRT OFF CACHE BOOL "Do not link against static CRT" FORCE)
        endif()

        set(expp_saved_c_flags "${CMAKE_C_FLAGS}")
        set(expp_saved_cxx_flags "${CMAKE_CXX_FLAGS}")
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
            string(APPEND CMAKE_C_FLAGS " -w")
            string(APPEND CMAKE_CXX_FLAGS " -w")
        elseif(MSVC)
            string(APPEND CMAKE_C_FLAGS " /w")
            string(APPEND CMAKE_CXX_FLAGS " /w")
        endif()

        FetchContent_MakeAvailable(libgit2)

        set(CMAKE_C_FLAGS "${expp_saved_c_flags}")
        set(CMAKE_CXX_FLAGS "${expp_saved_cxx_flags}")

        if(TARGET libgit2package)
            target_link_libraries(expp_git2 INTERFACE libgit2package)
            target_include_directories(expp_git2 SYSTEM INTERFACE
                "${libgit2_SOURCE_DIR}/include"
                "${libgit2_BINARY_DIR}/include"
            )
            set(EXPP_LIBGIT2_COMPILED_IN 1)
        elseif(TARGET git2)
            target_link_libraries(expp_git2 INTERFACE git2)
            set(EXPP_LIBGIT2_COMPILED_IN 1)
        elseif(TARGET libgit2)
            target_link_libraries(expp_git2 INTERFACE libgit2)
            set(EXPP_LIBGIT2_COMPILED_IN 1)
        endif()
    endif()
endif()

target_compile_definitions(expp_git2 INTERFACE
    EXPP_HAS_LIBGIT2=${EXPP_LIBGIT2_COMPILED_IN}
)
