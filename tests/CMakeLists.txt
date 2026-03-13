# Include Catch2 helpers
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)

# ==============================================================================
# Test executable
# ==============================================================================
add_executable(expp_tests
    test_main.cpp
    core/test_filesystem.cpp
    core/test_error.cpp
)

target_link_libraries(expp_tests
    PRIVATE
        expp_core
        expp_ui
        Catch2::Catch2WithMain
)

target_include_directories(expp_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Discover tests for CTest
catch_discover_tests(expp_tests)

# ==============================================================================
# Custom test target
# ==============================================================================
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS expp_tests
    COMMENT "Running Expp test suite"
)