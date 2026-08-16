#ifndef EXPP_APP_TREE_SITTER_HIGHLIGHTER_HPP
#define EXPP_APP_TREE_SITTER_HIGHLIGHTER_HPP

#include "expp/app/preview_model.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace expp::app {

/**
 * @brief Highlights a bounded preview with a bundled Tree-sitter grammar.
 *
 * @return Rich text for supported languages, or `std::nullopt` when Tree-sitter
 *         is unavailable, the language is unsupported, or parsing cannot start.
 */
[[nodiscard]] std::optional<RichTextPreview> highlight_with_tree_sitter(
    const std::filesystem::path& path,
    std::string_view mime_type,
    std::span<const std::string> lines);

}  // namespace expp::app

#endif  // EXPP_APP_TREE_SITTER_HIGHLIGHTER_HPP
