#include "expp/ui/components.hpp"
#include "expp/ui/theme.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace expp::ui {

namespace {

constexpr std::string_view kKittyDeleteStream{"\x1b_Ga=d,d=I,i=1,q=2\x1b\\"};

class TerminalControlNode final : public ftxui::Node {
public:
    explicit TerminalControlNode(std::string_view control_stream)
        : controlStream_(control_stream) {}

    void ComputeRequirement() override {}

    void Render(ftxui::Screen& screen) override {
        auto& pixel = screen.PixelAt(box_.x_min, box_.y_min);
        pixel.character = std::string{controlStream_} + pixel.character;
    }

private:
    std::string_view controlStream_;
};

class TerminalImageNode final : public ftxui::Node {
public:
    TerminalImageNode(std::string_view escape_stream, int columns, int rows, bool emit_stream)
        : escapeStream_(escape_stream)
        , columns_(std::max(1, columns))
        , rows_(std::max(1, rows))
        , emitStream_(emit_stream) {}

    void ComputeRequirement() override {
        requirement_.min_x = columns_;
        requirement_.min_y = rows_;
    }

    void Render(ftxui::Screen& screen) override {
        if (!emitStream_) {
            return;
        }
        // The terminal is at this cell while FTXUI serializes the screen. Restore
        // it afterwards for protocols, such as iTerm2, that advance the cursor.
        auto& pixel = screen.PixelAt(box_.x_min, box_.y_min);
        pixel.character =
            std::format("{}\x1b[{};{}H", escapeStream_, box_.y_min + 1, box_.x_min + 1);
    }

private:
    std::string_view escapeStream_;
    int columns_;
    int rows_;
    bool emitStream_;
};

[[nodiscard]] ftxui::Element terminal_control(std::string_view control_stream) {
    return std::make_shared<TerminalControlNode>(control_stream);
}

[[nodiscard]] ftxui::Element terminal_image(std::string_view escape_stream,
                                            int columns,
                                            int rows,
                                            bool emit_stream) {
    return std::make_shared<TerminalImageNode>(escape_stream, columns, rows, emit_stream);
}

}  // namespace

struct PreviewComponent::Impl {
    explicit Impl(PreviewRenderConfig in_config) : config(std::move(in_config)) {}

    PreviewRenderConfig config;
    std::string activeKittyImage_;

    [[nodiscard]] const Theme& theme() const noexcept {
        return config.theme != nullptr ? *config.theme : global_theme();
    }

    [[nodiscard]] int resolveMaxLines() const {
        int max_lines = config.maxRenderLines;
        if (max_lines < 0) {
            const auto terminal_size = ftxui::Terminal::Size();
            if (terminal_size.dimy > 0) {
                max_lines = terminal_size.dimy;
            }
        }
        return std::max(1, max_lines);
    }

    [[nodiscard]] ftxui::Element renderLines(const std::vector<std::string>& lines,
                                             int max_lines = -1) const {
        using namespace ftxui;
        if (lines.empty()) {
            return text(config.emptyMessage) | dim | center;
        }
        if (max_lines < 0) {
            max_lines = static_cast<int>(lines.size());
        }

        Elements elements;
        const int line_count = std::min(static_cast<int>(lines.size()), max_lines);
        for (int index = 0; index < line_count; ++index) {
            elements.push_back(text(lines[static_cast<std::size_t>(index)]));
        }
        if (lines.size() > static_cast<std::size_t>(max_lines)) {
            elements.push_back(
                text("... (" + std::to_string(lines.size() - static_cast<std::size_t>(max_lines)) +
                     " more lines)") |
                dim);
        }
        return vbox(std::move(elements));
    }

    [[nodiscard]] ftxui::Element renderFragment(const app::RichTextFragment& fragment) const {
        using namespace ftxui;
        auto element = text(fragment.text) | color(theme().getPreviewTextColor(fragment.role));
        switch (fragment.role) {
            case app::PreviewTextRole::Keyword:
                return element | bold;
            case app::PreviewTextRole::Comment:
                return element | dim;
            case app::PreviewTextRole::String:
            case app::PreviewTextRole::Number:
            case app::PreviewTextRole::Type:
            case app::PreviewTextRole::Diagnostic:
            case app::PreviewTextRole::Normal:
            default:
                return element;
        }
    }

    [[nodiscard]] ftxui::Element renderRichText(const app::RichTextPreview& preview,
                                                int max_lines) const {
        using namespace ftxui;
        Elements rendered;
        const auto line_count = std::min(preview.lines.size(), static_cast<std::size_t>(max_lines));
        rendered.reserve(line_count);
        for (std::size_t index = 0; index < line_count; ++index) {
            Elements fragments;
            fragments.reserve(preview.lines[index].size());
            for (const auto& fragment : preview.lines[index]) {
                fragments.push_back(renderFragment(fragment));
            }
            rendered.push_back(hbox(std::move(fragments)));
        }
        return rendered.empty() ? text(config.emptyMessage) | dim | center
                                : vbox(std::move(rendered));
    }

    [[nodiscard]] ftxui::Element renderContent(const app::PreviewReadyState& state,
                                               int max_lines,
                                               bool emit_image) const {
        return std::visit(
            [&](const auto& content) -> ftxui::Element {
                using Content = std::decay_t<decltype(content)>;
                if constexpr (std::is_same_v<Content, app::PlainTextPreview>) {
                    return renderLines(content.lines.empty() ? state.lines : content.lines,
                                       max_lines);
                } else if constexpr (std::is_same_v<Content, app::RichTextPreview>) {
                    return renderRichText(content, max_lines);
                } else if constexpr (std::is_same_v<Content, app::ListingPreview>) {
                    return renderLines(content.entries.empty() ? state.lines : content.entries,
                                       max_lines);
                } else if constexpr (std::is_same_v<Content, app::HexDumpPreview>) {
                    return renderLines(content.lines.empty() ? state.lines : content.lines,
                                       max_lines);
                } else if constexpr (std::is_same_v<Content, app::MetadataPreview>) {
                    return renderLines(content.lines.empty() ? state.lines : content.lines,
                                       max_lines);
                } else {
                    if (content.renderedInline && !content.escapeStream.empty()) {
                        return ftxui::vbox(
                                   {renderLines(state.lines, max_lines), ftxui::separator(),
                                    ftxui::filler(),
                                    ftxui::hbox({ftxui::filler(),
                                                 terminal_image(content.escapeStream,
                                                                content.displayColumns,
                                                                content.displayRows, emit_image),
                                                 ftxui::filler()}),
                                    ftxui::filler()}) |
                               ftxui::flex;
                    }
                    return renderLines(state.lines, max_lines);
                }
            },
            state.content);
    }

    [[nodiscard]] ftxui::Element render(const app::PreviewModel& model) {
        using namespace ftxui;
        const int max_lines = resolveMaxLines();
        bool emit_image = true;
        bool clear_kitty_image = false;

        if (const auto* ready = std::get_if<app::PreviewReadyState>(&model)) {
            if (const auto* image = std::get_if<app::ImagePreview>(&ready->content);
                image != nullptr && image->renderedInline && image->protocol == "kitty" &&
                !image->escapeStream.empty()) {
                emit_image = activeKittyImage_ != image->escapeStream;
                activeKittyImage_ = image->escapeStream;
            } else if (!activeKittyImage_.empty()) {
                activeKittyImage_.clear();
                clear_kitty_image = true;
            }
        } else if (!activeKittyImage_.empty()) {
            activeKittyImage_.clear();
            clear_kitty_image = true;
        }

        auto content = std::visit(
            [&](const auto& state) -> Element {
                using State = std::decay_t<decltype(state)>;
                if constexpr (std::is_same_v<State, app::PreviewIdleState>) {
                    return text(config.emptyMessage) | dim | center;
                } else if constexpr (std::is_same_v<State, app::PreviewLoadingState>) {
                    return text("[Loading...]") | dim | center;
                } else if constexpr (std::is_same_v<State, app::PreviewReadyState>) {
                    auto rendered = renderContent(state, max_lines, emit_image);
                    if (state.diagnostic.empty()) {
                        return rendered;
                    }
                    return vbox({std::move(rendered), text("[" + state.diagnostic + "]") |
                                                          color(theme().getPreviewTextColor(
                                                              app::PreviewTextRole::Diagnostic)) |
                                                          dim});
                } else {
                    return text(config.errorPrefix + state.message + "]") |
                           color(theme().getPreviewTextColor(app::PreviewTextRole::Diagnostic)) |
                           dim;
                }
            },
            model);
        return clear_kitty_image ? dbox({std::move(content), terminal_control(kKittyDeleteStream)})
                                 : content;
    }
};

PreviewComponent::PreviewComponent(const PreviewRenderConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

PreviewComponent::~PreviewComponent() = default;
PreviewComponent::PreviewComponent(PreviewComponent&&) noexcept = default;
PreviewComponent& PreviewComponent::operator=(PreviewComponent&&) noexcept = default;

ftxui::Element PreviewComponent::render(const app::PreviewModel& model) const {
    return impl_->render(model);
}

ftxui::Element PreviewComponent::renderLines(const std::vector<std::string>& lines) const {
    return impl_->renderLines(lines, impl_->resolveMaxLines());
}

void PreviewComponent::setConfig(const PreviewRenderConfig& config) {
    impl_->config = config;
}

}  // namespace expp::ui
