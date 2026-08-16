# Expp CMake modules

The root `CMakeLists.txt` is intentionally limited to project orchestration. Each
module has one build-system responsibility:

- `ExppVersion.cmake` resolves the project version from CI metadata.
- `ExppOptions.cmake` owns cache options and language-wide defaults.
- `ExppCompilerPolicy.cmake` defines compiler, warning, sanitizer, and analysis policies.
- `ExppDependencies.cmake` fetches required third-party dependencies and configures Asio.
- `ExppPreviewFeatures.cmake` detects optional preview backends.
- `ExppTreeSitter.cmake` fetches and builds the syntax-highlighting runtime and grammars.
- `ExppGit.cmake` finds or fetches libgit2 behind a stable interface target.
- `ExppTargets.cmake` declares first-party libraries and the executable.
- `ExppPackaging.cmake` owns installation and CPack configuration.
- `ExppSummary.cmake` prints the resolved configuration.

Prefer extending the module that already owns a concern. Add a new module only when
the concern has its own inputs, targets, or optional lifecycle.
