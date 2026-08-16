include_guard(GLOBAL)

set(EXPP_TREE_SITTER_COMPILED_IN 0)
if(NOT EXPP_ENABLE_TREE_SITTER)
    return()
endif()

include(FetchContent)

function(expp_declare_tree_sitter_grammar content_name repository tag)
    FetchContent_Declare(${content_name}
        GIT_REPOSITORY "https://github.com/tree-sitter/${repository}.git"
        GIT_TAG "${tag}"
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR expp-no-add-subdirectory
    )
endfunction()

# SOURCE_SUBDIR prevents upstream grammar projects from adding their own tools
# and targets. Expp builds only the runtime and generated parsers it consumes.
FetchContent_Declare(expp_tree_sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    GIT_TAG v0.26.11
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR expp-no-add-subdirectory
)
expp_declare_tree_sitter_grammar(expp_tree_sitter_cpp tree-sitter-cpp v0.23.4)
expp_declare_tree_sitter_grammar(expp_tree_sitter_python tree-sitter-python v0.25.0)
expp_declare_tree_sitter_grammar(expp_tree_sitter_rust tree-sitter-rust v0.24.2)
expp_declare_tree_sitter_grammar(expp_tree_sitter_javascript tree-sitter-javascript v0.25.0)
expp_declare_tree_sitter_grammar(expp_tree_sitter_go tree-sitter-go v0.25.0)
expp_declare_tree_sitter_grammar(expp_tree_sitter_java tree-sitter-java v0.23.5)
expp_declare_tree_sitter_grammar(expp_tree_sitter_c_sharp tree-sitter-c-sharp v0.23.5)
expp_declare_tree_sitter_grammar(expp_tree_sitter_ruby tree-sitter-ruby v0.23.1)
expp_declare_tree_sitter_grammar(expp_tree_sitter_bash tree-sitter-bash v0.25.1)
expp_declare_tree_sitter_grammar(expp_tree_sitter_typescript tree-sitter-typescript v0.23.2)
expp_declare_tree_sitter_grammar(expp_tree_sitter_json tree-sitter-json v0.24.8)

FetchContent_MakeAvailable(
    expp_tree_sitter
    expp_tree_sitter_cpp
    expp_tree_sitter_python
    expp_tree_sitter_rust
    expp_tree_sitter_javascript
    expp_tree_sitter_go
    expp_tree_sitter_java
    expp_tree_sitter_c_sharp
    expp_tree_sitter_ruby
    expp_tree_sitter_bash
    expp_tree_sitter_typescript
    expp_tree_sitter_json
)

add_library(expp_tree_sitter_runtime STATIC
    "${expp_tree_sitter_SOURCE_DIR}/lib/src/lib.c"
)
target_include_directories(expp_tree_sitter_runtime
    PUBLIC "${expp_tree_sitter_SOURCE_DIR}/lib/include"
    PRIVATE "${expp_tree_sitter_SOURCE_DIR}/lib/src"
)
target_compile_definitions(expp_tree_sitter_runtime PRIVATE
    _POSIX_C_SOURCE=200112L
    _DEFAULT_SOURCE
    _BSD_SOURCE
    _DARWIN_C_SOURCE
)
set_target_properties(expp_tree_sitter_runtime PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

function(expp_add_tree_sitter_grammar target source_directory)
    set(grammar_sources "${source_directory}/src/parser.c")
    if(EXISTS "${source_directory}/src/scanner.c")
        list(APPEND grammar_sources "${source_directory}/src/scanner.c")
    elseif(EXISTS "${source_directory}/src/scanner.cc")
        list(APPEND grammar_sources "${source_directory}/src/scanner.cc")
    endif()

    add_library(${target} STATIC ${grammar_sources})
    target_include_directories(${target} PRIVATE "${source_directory}/src")
    target_link_libraries(${target} PUBLIC expp_tree_sitter_runtime)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON
    )
endfunction()

expp_add_tree_sitter_grammar(expp_tree_sitter_cpp "${expp_tree_sitter_cpp_SOURCE_DIR}")
expp_add_tree_sitter_grammar(expp_tree_sitter_python "${expp_tree_sitter_python_SOURCE_DIR}")
expp_add_tree_sitter_grammar(expp_tree_sitter_rust "${expp_tree_sitter_rust_SOURCE_DIR}")
expp_add_tree_sitter_grammar(expp_tree_sitter_javascript "${expp_tree_sitter_javascript_SOURCE_DIR}")
expp_add_tree_sitter_grammar(expp_tree_sitter_go "${expp_tree_sitter_go_SOURCE_DIR}")
expp_add_tree_sitter_grammar(expp_tree_sitter_java "${expp_tree_sitter_java_SOURCE_DIR}")
expp_add_tree_sitter_grammar(expp_tree_sitter_c_sharp "${expp_tree_sitter_c_sharp_SOURCE_DIR}")
expp_add_tree_sitter_grammar(expp_tree_sitter_ruby "${expp_tree_sitter_ruby_SOURCE_DIR}")
expp_add_tree_sitter_grammar(expp_tree_sitter_bash "${expp_tree_sitter_bash_SOURCE_DIR}")
expp_add_tree_sitter_grammar(
    expp_tree_sitter_typescript_language
    "${expp_tree_sitter_typescript_SOURCE_DIR}/typescript"
)
expp_add_tree_sitter_grammar(
    expp_tree_sitter_tsx
    "${expp_tree_sitter_typescript_SOURCE_DIR}/tsx"
)
expp_add_tree_sitter_grammar(expp_tree_sitter_json "${expp_tree_sitter_json_SOURCE_DIR}")

target_link_libraries(expp_preview_features INTERFACE
    expp_tree_sitter_cpp
    expp_tree_sitter_python
    expp_tree_sitter_rust
    expp_tree_sitter_javascript
    expp_tree_sitter_go
    expp_tree_sitter_java
    expp_tree_sitter_c_sharp
    expp_tree_sitter_ruby
    expp_tree_sitter_bash
    expp_tree_sitter_typescript_language
    expp_tree_sitter_tsx
    expp_tree_sitter_json
)
set(EXPP_TREE_SITTER_COMPILED_IN 1)
