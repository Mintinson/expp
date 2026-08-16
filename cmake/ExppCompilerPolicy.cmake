include_guard(GLOBAL)

add_library(expp_project_options INTERFACE)
target_compile_features(expp_project_options INTERFACE cxx_std_23)
target_compile_definitions(expp_project_options INTERFACE
    TOML_EXCEPTIONS=0
    EXPP_PREPARE_IO_URING_BACKEND=${EXPP_IO_URING_COMPILED_IN}
    $<$<AND:$<BOOL:${EXPP_DISABLE_EXCEPTIONS}>,$<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},MSVC>>:_HAS_EXCEPTIONS=0>
)
target_compile_options(expp_project_options INTERFACE
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
if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(expp_project_options INTERFACE -Xclang -std=c++23)
endif()

add_library(expp_warnings INTERFACE)
target_compile_options(expp_warnings INTERFACE
    $<$<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},MSVC>:/W4 /WX>
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>
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

add_library(expp_sanitizers INTERFACE)
if(EXPP_ENABLE_SANITIZERS)
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

set(EXPP_CLANG_TIDY_COMMAND "")
if(EXPP_ENABLE_CLANG_TIDY)
    find_program(EXPP_CLANG_TIDY_COMMAND NAMES clang-tidy)
    if(NOT EXPP_CLANG_TIDY_COMMAND)
        message(WARNING "clang-tidy requested but not found")
    endif()
endif()

set(EXPP_CPPCHECK_COMMAND "")
if(EXPP_ENABLE_CPPCHECK)
    find_program(EXPP_CPPCHECK_COMMAND NAMES cppcheck)
    if(NOT EXPP_CPPCHECK_COMMAND)
        message(WARNING "cppcheck requested but not found")
    endif()
endif()

function(expp_apply_analysis target)
    if(EXPP_CLANG_TIDY_COMMAND)
        set_property(TARGET ${target} PROPERTY CXX_CLANG_TIDY "${EXPP_CLANG_TIDY_COMMAND}")
    endif()
    if(EXPP_CPPCHECK_COMMAND)
        set_property(TARGET ${target} PROPERTY CXX_CPPCHECK "${EXPP_CPPCHECK_COMMAND}")
    endif()
endfunction()

function(expp_apply_target_defaults target)
    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(${target}
        PRIVATE
            expp_project_options
            expp_warnings
            expp_sanitizers
    )
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    expp_apply_analysis(${target})
endfunction()
