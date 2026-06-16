list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

add_executable(cli_bench cli_bench.cpp)
target_link_libraries(cli_bench PRIVATE Catch2::Catch2WithMain)