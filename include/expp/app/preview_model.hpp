/**
 * @file preview_model.hpp
 * @brief Preview state types shared between App controllers and UI rendering.
 *
 * These types are intentionally pure data (no logic, no service dependencies)
 * so both the App layer (which produces them) and the UI layer (which consumes
 * them) can include this header without creating a circular dependency.
 */

#ifndef EXPP_APP_PREVIEW_MODEL_HPP
#define EXPP_APP_PREVIEW_MODEL_HPP

#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace expp::app {

/// Preview state when there is no active preview target.
struct PreviewIdleState {};

/// Preview state while content is being loaded.
struct PreviewLoadingState {
    std::filesystem::path target;
};

/// Preview state after successful content loading.
struct PreviewReadyState {
    std::filesystem::path target;
    std::vector<std::string> lines;
};

/// Preview state after a loading error.
struct PreviewErrorState {
    std::filesystem::path target;
    std::string message;
};

/// Discriminated preview model — the single source of truth for preview UI state.
using PreviewModel = std::variant<PreviewIdleState, PreviewLoadingState, PreviewReadyState, PreviewErrorState>;

}  // namespace expp::app

#endif  // EXPP_APP_PREVIEW_MODEL_HPP
