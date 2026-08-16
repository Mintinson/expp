include_guard(GLOBAL)

include(FetchContent)
find_package(Threads REQUIRED)

FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v6.1.9
)
FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus
    GIT_TAG v3.4.0
)
FetchContent_Declare(asio_src
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio
    GIT_TAG asio-1-38-0
)

FetchContent_MakeAvailable(ftxui tomlplusplus asio_src)

if(EXPP_BUILD_TESTS OR EXPP_ENABLE_BENCHMARKS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2
        GIT_TAG v3.5.2
    )
    FetchContent_MakeAvailable(Catch2)
endif()

add_library(expp_asio INTERFACE)
target_include_directories(expp_asio SYSTEM INTERFACE "${asio_src_SOURCE_DIR}/include")
target_compile_definitions(expp_asio INTERFACE
    ASIO_STANDALONE
    ASIO_NO_DEPRECATED
    $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=0x0601>
    $<$<BOOL:${EXPP_DISABLE_EXCEPTIONS}>:ASIO_NO_EXCEPTIONS>
)
target_compile_options(expp_asio INTERFACE
    $<$<CXX_COMPILER_ID:GNU>:-Wno-null-dereference>
    $<$<CXX_COMPILER_ID:MSVC>:/wd4702>
)
target_link_libraries(expp_asio INTERFACE Threads::Threads)

# Keep third-party targets compatible with the flags used by first-party code
# without leaking Expp's warning-as-error policy into external sources.
if(MSVC)
    foreach(ftxui_target IN ITEMS screen dom component)
        target_compile_options(${ftxui_target} PRIVATE
            /permissive-
            /Zc:__cplusplus
            /EHsc
            $<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>
            $<$<AND:$<BOOL:${EXPP_ENABLE_SANITIZERS}>,$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>>:/fsanitize=address>
        )
    endforeach()

    if(EXPP_ENABLE_SANITIZERS)
        foreach(catch_target IN ITEMS Catch2 Catch2WithMain)
            if(TARGET ${catch_target})
                target_compile_options(${catch_target} PRIVATE
                    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/fsanitize=address>
                )
            endif()
        endforeach()
    endif()
endif()
