---
description: C++ TUI File Manager – Global Instructions for Copilot
applyTo: '**/*'
# applyTo: 'Describe when these instructions should be loaded' # when provided, instructions will automatically be added to the request context when the pattern matches an attached file
---
# C++ TUI File Manager – Copilot Global Instructions

## Project Vision  
A cross-platform TUI file manager built with C++23, aiming to rival **Yazi** in speed, safety, aesthetics, and high customizability.

## Mandatory Tech Stack  
- **Standard**: C++23 (GCC 15+ / Clang 20+ / MSVC 17.12+)  
- **Build System**: CMake 3.28+ (Modern, target-based CMake)  
- **Error Handling**: `std::expected` or `tl::expected`; exceptions strictly prohibited  
- **Architecture**: Prefer composition over inheritance; adhere to SOLID principles; zero-overhead abstractions  

## Code Style (`.clang-format` already configured)  
- You can read the `.clang-format` and `.clang-tidy` files in the current project root directory to understand the correct code style.

## Key Design Patterns  
1. **RAII**: All resource management must be automated  
2. **Type Safety**: Wrap raw handles/IDs in strong-typed wrappers  
3. **Error Propagation**: Use `expected<T, Error>`; errors must carry contextual information  
4. **Async**: C++20 coroutines + custom executor; avoid callback hell  

## Prohibited Practices  
- Raw pointers owning resources (use `std::unique_ptr` / `std::shared_ptr`)  
- Macro-defined constants (use `constexpr`)  
- Dynamic exception specifications (use `noexcept` only when truly non-throwing)  
- Globally mutable state (prefer dependency injection; use singletons only after careful evaluation)  

## Documentation Requirements  
- All public APIs must include Doxygen comments  
- Complex algorithms must include complexity analysis  
- Extension points must be marked with `// EXTENSION POINT: <description>`  

## Refactoring Priorities  
1. Separate concerns (Core / UI / App layers)  
2. Externalize configuration (replace hard-coded values with a config system)  
3. Migrate error handling (from exceptions to `expected`)  
4. Increase test coverage (unit tests for core APIs)  

## Recommended Libraries  
- **TUI**: `ftxui` (preferred) or a custom ncurses wrapper  
- **Async**: Standalone `asio` or `libuv`  
- **Config**: `toml++` (header-only)  
- **CLI Parsing**: `lyra` or `cxxopts`  
- **Testing**: `Catch2 v3` or `GTest`