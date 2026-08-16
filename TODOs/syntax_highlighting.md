# Extending syntax highlighting

Tree-sitter support is enabled by `EXPP_ENABLE_TREE_SITTER` and currently bundles
C, C++, Python, Rust, JavaScript, Go, Java, C#, Ruby, Bash, TypeScript/TSX, and JSON.
CMake fetches tagged source releases and builds the generated parsers directly, so
contributors do not need a system Tree-sitter package or the Tree-sitter CLI.

## Configure syntax colors

By default, syntax roles reuse semantic colors from the active theme. This keeps a
custom light or dark theme visually consistent without a separate syntax palette.
Add a shared `base` to recolor every syntax role uniformly, then add only the role
overrides that should differ:

```toml
[theme.syntax_colors]
base = "#D0D0D0"
keyword = "#C586C0"
string = "#CE9178"
comment = "#6A9955"
number = "#B5CEA8"
type = "#4EC9B0"
diagnostic = "#F44747"
normal = "#D4D4D4"
```

Resolution order is individual role, shared `base`, then the active theme's semantic
fallback. Remove `base` and any individual values that should track future theme
changes automatically.

## Add another language

1. Choose the official or well-maintained grammar repository and pin a release tag.
   Confirm that its generated `src/parser.c` is compatible with the runtime ABI in
   `CMakeLists.txt`. The bundled runtime reports the supported range in
   `lib/include/tree_sitter/api.h`.
2. Add a `fetchcontent_declare` entry beside the existing grammar declarations. Keep
   `GIT_SHALLOW TRUE` and `SOURCE_SUBDIR expp-no-add-subdirectory`; the latter prevents
   grammar-specific CMake targets and tools from entering the Expp build.
3. Add the dependency to the Tree-sitter `FetchContent_MakeAvailable` call, create its
   parser target with `expp_add_tree_sitter_grammar`, and link that target through
   `expp_preview_features`. The helper automatically includes `scanner.c` or
   `scanner.cc` when a grammar needs an external scanner. Pass the grammar-specific
   subdirectory when a repository contains multiple parsers, as TypeScript does.
4. Declare the grammar's exported `tree_sitter_<language>()` function and map its file
   extensions or MIME types in `src/app/tree_sitter_highlighter.cpp`.
5. Add a representative source file under `tests/resource/syntax_samples/` and a
   fixture entry in `tests/app/test_preview.cpp`. Include syntax that the generic
   fallback cannot recognize, and assert both the expected roles and exact source-text
   reconstruction.
6. Configure and test both paths:

   ```sh
   cmake --preset <debug-preset> -DEXPP_ENABLE_TREE_SITTER=ON
   cmake --build --preset <debug-preset>
   ctest --preset <debug-preset> --output-on-failure

   cmake --preset <debug-preset> -DEXPP_ENABLE_TREE_SITTER=OFF
   cmake --build --preset <debug-preset>
   ```

The shared node classifier already handles common comment, string, numeric, type, and
keyword node names. Extend `role_for_node_type` only when a grammar uses materially
different node names; keep language-specific exceptions narrow. Unsupported files and
parser setup failures intentionally fall back to the existing lightweight highlighters.

## Maintenance notes

- Update the runtime and grammar tags deliberately and together, then run every syntax
  fixture. Do not track moving branches.
- Keep the bundled set focused on languages users preview frequently. Each grammar adds
  generated parser code to binary size and clean-build time.
- Highlighting is limited to the already bounded preview chunk. Avoid reading an entire
  source file or retaining syntax trees between unrelated preview requests without a
  measured performance reason.
