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

#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace expp::app {

/// Viewport available to the preview renderer.
struct PreviewViewport {
    int width{0};
    int height{0};

    [[nodiscard]] constexpr bool operator==(const PreviewViewport&) const = default;
};

/// Logical preview scroll/read offset.
struct PreviewOffset {
    std::uint64_t row{0};
    std::uint64_t byte{0};

    [[nodiscard]] constexpr bool operator==(const PreviewOffset&) const = default;
};

/// Styled text role for rich preview fragments.
enum class PreviewTextRole : std::uint8_t {
    Normal,
    Keyword,
    String,
    Comment,
    Number,
    Type,
    Diagnostic,
};

/// A styled piece of text that can be consumed by the UI layer.
struct RichTextFragment {
    std::string text;
    PreviewTextRole role{PreviewTextRole::Normal};
};

/// Plain line-oriented text preview.
struct PlainTextPreview {
    std::vector<std::string> lines;
};

/// Rich line-oriented text preview.
struct RichTextPreview {
    std::vector<std::vector<RichTextFragment>> lines;
};

/// Native terminal image stream preview.
struct ImagePreview {
    std::string protocol;
    std::string escapeStream;
    int width{0};
    int height{0};
    bool renderedInline{false};
};

/// File listing/tree preview, used for directories and archives.
struct ListingPreview {
    std::vector<std::string> entries;
    bool tree{false};
};

/// Hex dump preview, rendered as preformatted lines.
struct HexDumpPreview {
    std::vector<std::string> lines;
    std::uint64_t baseOffset{0};
};

/// Last-resort metadata preview.
struct MetadataPreview {
    std::vector<std::string> lines;
};

/// Typed preview payload bus for all preview providers.
using PreviewContent = std::variant<PlainTextPreview,
                                    RichTextPreview,
                                    ImagePreview,
                                    ListingPreview,
                                    HexDumpPreview,
                                    MetadataPreview>;

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
    PreviewContent content{PlainTextPreview{}};
    std::string mimeType;
    bool truncated{false};
    bool canScrollUp{false};
    bool canScrollDown{false};
    std::string diagnostic;
};

/// Preview state after a loading error.
struct PreviewErrorState {
    std::filesystem::path target;
    std::string message;
};

/// Discriminated preview model — the single source of truth for preview UI state.
using PreviewModel =
    std::variant<PreviewIdleState, PreviewLoadingState, PreviewReadyState, PreviewErrorState>;

}  // namespace expp::app

#endif  // EXPP_APP_PREVIEW_MODEL_HPP
