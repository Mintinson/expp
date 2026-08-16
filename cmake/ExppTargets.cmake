include_guard(GLOBAL)

add_library(expp_core STATIC
    src/core/filesystem.cpp
    src/core/config.cpp
    src/core/async_runtime.cpp
    src/core/version_control.cpp
)
expp_apply_target_defaults(expp_core)
target_link_libraries(expp_core
    PUBLIC
        expp_asio
        expp_magic
        expp_git2
        tomlplusplus::tomlplusplus
)
if(WIN32)
    target_link_libraries(expp_core PRIVATE shell32)
endif()

add_library(expp_ui STATIC
    src/ui/key_handler.cpp
    src/ui/theme.cpp
    src/ui/file_list_component.cpp
    src/ui/status_bar_component.cpp
    src/ui/toast_component.cpp
    src/ui/help_menu_component.cpp
    src/ui/dialog_component.cpp
    src/ui/panel_component.cpp
    src/ui/preview_component.cpp
    src/ui/help_menu_model.cpp
)
expp_apply_target_defaults(expp_ui)
target_link_libraries(expp_ui
    PUBLIC
        expp_core
        ftxui::screen
        ftxui::dom
        ftxui::component
)

add_library(expp_app STATIC
    src/app/notification_center.cpp
    src/app/navigation_utils.cpp
    src/app/explorer.cpp
    src/app/explorer_view.cpp
    src/app/explorer_services.cpp
    src/app/explorer_directory_controller.cpp
    src/app/explorer_mutation_controller.cpp
    src/app/explorer_commands.cpp
    src/app/explorer_presenter.cpp
    src/app/explorer_sort.cpp
    src/app/explorer_state_helpers.cpp
    src/app/explorer_command_dispatcher.cpp
    src/app/explorer_overlay_controller.cpp
    src/app/explorer_render_composer.cpp
    src/app/explorer_preview_controller.cpp
    src/app/preview_provider.cpp
    src/app/tree_sitter_highlighter.cpp
)
expp_apply_target_defaults(expp_app)
target_link_libraries(expp_app
    PUBLIC expp_ui
    PRIVATE expp_preview_features
)

add_executable(expp_exe src/main.cpp)
target_link_libraries(expp_exe PRIVATE
    expp_app
    expp_project_options
    expp_warnings
    expp_sanitizers
)
set_target_properties(expp_exe PROPERTIES OUTPUT_NAME explorer)
expp_apply_analysis(expp_exe)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(expp_app PRIVATE -Wno-null-dereference)
    target_compile_options(expp_exe PRIVATE -Wno-null-dereference)
endif()
