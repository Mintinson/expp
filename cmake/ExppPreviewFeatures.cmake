include_guard(GLOBAL)

add_library(expp_magic INTERFACE)
set(EXPP_LIBMAGIC_COMPILED_IN 0)
if(EXPP_ENABLE_LIBMAGIC)
    find_path(LIBMAGIC_INCLUDE_DIR NAMES magic.h)
    find_library(LIBMAGIC_LIBRARY NAMES magic libmagic)
    if(LIBMAGIC_INCLUDE_DIR AND LIBMAGIC_LIBRARY)
        target_include_directories(expp_magic INTERFACE "${LIBMAGIC_INCLUDE_DIR}")
        target_link_libraries(expp_magic INTERFACE "${LIBMAGIC_LIBRARY}")
        set(EXPP_LIBMAGIC_COMPILED_IN 1)
    endif()
endif()
target_compile_definitions(expp_magic INTERFACE
    EXPP_HAS_LIBMAGIC=${EXPP_LIBMAGIC_COMPILED_IN}
)

add_library(expp_preview_features INTERFACE)

set(EXPP_LIBARCHIVE_COMPILED_IN 0)
if(EXPP_ENABLE_LIBARCHIVE)
    find_package(LibArchive QUIET)
    if(TARGET LibArchive::LibArchive)
        target_link_libraries(expp_preview_features INTERFACE LibArchive::LibArchive)
        set(EXPP_LIBARCHIVE_COMPILED_IN 1)
    endif()
endif()

include(ExppTreeSitter)

target_compile_definitions(expp_preview_features INTERFACE
    EXPP_HAS_LIBARCHIVE=${EXPP_LIBARCHIVE_COMPILED_IN}
    EXPP_HAS_TREE_SITTER=${EXPP_TREE_SITTER_COMPILED_IN}
    EXPP_HAS_TERMINAL_IMAGES=$<BOOL:${EXPP_ENABLE_TERMINAL_IMAGES}>
)
