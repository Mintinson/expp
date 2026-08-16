#include "expp/app/tree_sitter_highlighter.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#if EXPP_HAS_TREE_SITTER
    #include <tree_sitter/api.h>
#endif

namespace expp::app {

#if EXPP_HAS_TREE_SITTER

using namespace std::literals;

extern "C" {
    const TSLanguage* tree_sitter_cpp();
    const TSLanguage* tree_sitter_python();
    const TSLanguage* tree_sitter_rust();
    const TSLanguage* tree_sitter_javascript();
    const TSLanguage* tree_sitter_go();
    const TSLanguage* tree_sitter_java();
    const TSLanguage* tree_sitter_c_sharp();
    const TSLanguage* tree_sitter_ruby();
    const TSLanguage* tree_sitter_bash();
    const TSLanguage* tree_sitter_typescript();
    const TSLanguage* tree_sitter_tsx();
    const TSLanguage* tree_sitter_json();
}

namespace {

using LanguageFactory = const TSLanguage* (*)();

struct HighlightSpan {
    std::uint32_t begin{0};
    std::uint32_t end{0};
    PreviewTextRole role{PreviewTextRole::Normal};
};

[[nodiscard]] std::string lower_ascii(std::string value) {
    std::ranges::transform(value, value.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

[[nodiscard]] bool matches_any(std::string_view value,
                               std::span<const std::string_view> candidates) noexcept {
    return std::ranges::binary_search(candidates, value);
}

[[nodiscard]] LanguageFactory find_language(const std::filesystem::path& path,
                                            std::string_view mime_type) noexcept {
    const auto extension = lower_ascii(path.extension().string());
    const auto filename = lower_ascii(path.filename().string());
    static constexpr std::array cpp_extensions{
        ".c"sv,   ".cc"sv,  ".cpp"sv, ".cxx"sv, ".h"sv,   ".hh"sv,
        ".hpp"sv, ".hxx"sv, ".ipp"sv, ".ixx"sv, ".mpp"sv,
    };
    static constexpr std::array python_extensions{".py"sv, ".pyw"sv};
    static constexpr std::array javascript_extensions{
        ".cjs"sv,
        ".js"sv,
        ".jsx"sv,
        ".mjs"sv,
    };
    static constexpr std::array ruby_extensions{".rake"sv, ".rb"sv};
    static constexpr std::array ruby_filenames{"gemfile"sv, "rakefile"sv};
    static constexpr std::array bash_extensions{".bash"sv, ".sh"sv};
    static constexpr std::array bash_filenames{".bash_profile"sv, ".bashrc"sv, ".profile"sv};

    if (matches_any(extension, cpp_extensions)) {
        return &tree_sitter_cpp;
    }
    if (matches_any(extension, python_extensions)) {
        return &tree_sitter_python;
    }
    if (extension == ".rs") {
        return &tree_sitter_rust;
    }
    if (matches_any(extension, javascript_extensions) || mime_type == "application/javascript") {
        return &tree_sitter_javascript;
    }
    if (extension == ".go") {
        return &tree_sitter_go;
    }
    if (extension == ".java") {
        return &tree_sitter_java;
    }
    if (extension == ".cs") {
        return &tree_sitter_c_sharp;
    }
    if (matches_any(extension, ruby_extensions) || matches_any(filename, ruby_filenames)) {
        return &tree_sitter_ruby;
    }
    if (matches_any(extension, bash_extensions) || matches_any(filename, bash_filenames)) {
        return &tree_sitter_bash;
    }
    if (extension == ".ts") {
        return &tree_sitter_typescript;
    }
    if (extension == ".tsx") {
        return &tree_sitter_tsx;
    }
    if (extension == ".json" || mime_type == "application/json" ||
        mime_type == "application/x-ndjson") {
        return &tree_sitter_json;
    }
    return nullptr;
}

[[nodiscard]] bool is_keyword(std::string_view type) noexcept {
    static constexpr std::array keywords{
        "abstract"sv,     "alignas"sv,
        "alignof"sv,      "and"sv,
        "as"sv,           "asm"sv,
        "assert"sv,       "async"sv,
        "auto"sv,         "await"sv,
        "begin"sv,        "break"sv,
        "case"sv,         "catch"sv,
        "chan"sv,         "class"sv,
        "co_await"sv,     "co_return"sv,
        "co_yield"sv,     "concept"sv,
        "const"sv,        "consteval"sv,
        "constexpr"sv,    "constinit"sv,
        "continue"sv,     "crate"sv,
        "debugger"sv,     "declare"sv,
        "decltype"sv,     "def"sv,
        "default"sv,      "defer"sv,
        "del"sv,          "delete"sv,
        "do"sv,           "done"sv,
        "dyn"sv,          "elif"sv,
        "else"sv,         "end"sv,
        "ensure"sv,       "enum"sv,
        "esac"sv,         "except"sv,
        "explicit"sv,     "export"sv,
        "extends"sv,      "extern"sv,
        "fallthrough"sv,  "false"sv,
        "fi"sv,           "final"sv,
        "finally"sv,      "fn"sv,
        "for"sv,          "friend"sv,
        "from"sv,         "func"sv,
        "function"sv,     "global"sv,
        "go"sv,           "goto"sv,
        "if"sv,           "impl"sv,
        "implements"sv,   "import"sv,
        "in"sv,           "infer"sv,
        "inline"sv,       "instanceof"sv,
        "interface"sv,    "is"sv,
        "keyof"sv,        "lambda"sv,
        "let"sv,          "loop"sv,
        "map"sv,          "match"sv,
        "mod"sv,          "module"sv,
        "move"sv,         "mut"sv,
        "mutable"sv,      "namespace"sv,
        "native"sv,       "never"sv,
        "new"sv,          "noexcept"sv,
        "nonlocal"sv,     "not"sv,
        "null"sv,         "nullptr"sv,
        "operator"sv,     "or"sv,
        "override"sv,     "package"sv,
        "pass"sv,         "private"sv,
        "protected"sv,    "pub"sv,
        "public"sv,       "raise"sv,
        "range"sv,        "readonly"sv,
        "redo"sv,         "ref"sv,
        "register"sv,     "reinterpret_cast"sv,
        "requires"sv,     "rescue"sv,
        "retry"sv,        "return"sv,
        "satisfies"sv,    "select"sv,
        "self"sv,         "sizeof"sv,
        "static"sv,       "static_assert"sv,
        "strictfp"sv,     "struct"sv,
        "super"sv,        "switch"sv,
        "synchronized"sv, "template"sv,
        "then"sv,         "this"sv,
        "throw"sv,        "throws"sv,
        "trait"sv,        "transient"sv,
        "true"sv,         "try"sv,
        "type"sv,         "typedef"sv,
        "typeid"sv,       "typename"sv,
        "typeof"sv,       "union"sv,
        "unknown"sv,      "unless"sv,
        "unsafe"sv,       "until"sv,
        "use"sv,          "using"sv,
        "var"sv,          "virtual"sv,
        "volatile"sv,     "where"sv,
        "while"sv,        "with"sv,
        "yield"sv,
    };
    return matches_any(type, keywords);
}

[[nodiscard]] PreviewTextRole role_for_node_type(std::string_view type, bool leaf) noexcept {
    if (type.contains("comment")) {
        return PreviewTextRole::Comment;
    }
    if (type.contains("string") || type == "char_literal" || type == "character_literal") {
        return PreviewTextRole::String;
    }
    static constexpr std::array type_nodes{
        "boolean_type"sv,   "floating_point_type"sv, "integral_type"sv,   "predefined_type"sv,
        "primitive_type"sv, "sized_type"sv,          "type_identifier"sv, "void_type"sv,
    };
    if (matches_any(type, type_nodes)) {
        return PreviewTextRole::Type;
    }
    if (type.contains("number") || type.contains("integer") || type.contains("float") ||
        type == "int_literal") {
        return PreviewTextRole::Number;
    }
    if (leaf && is_keyword(type)) {
        return PreviewTextRole::Keyword;
    }
    return PreviewTextRole::Normal;
}

void collect_highlight_spans(TSNode root, std::vector<HighlightSpan>& spans) {
    std::vector<TSNode> pending{root};
    while (!pending.empty()) {
        const auto node = pending.back();
        pending.pop_back();

        const auto child_count = ts_node_child_count(node);
        const auto role = role_for_node_type(ts_node_type(node), child_count == 0);
        const auto begin = ts_node_start_byte(node);
        const auto end = ts_node_end_byte(node);
        if (role != PreviewTextRole::Normal && begin < end) {
            spans.push_back(HighlightSpan{.begin = begin, .end = end, .role = role});
            continue;
        }

        for (auto index = child_count; index > 0; --index) {
            pending.push_back(ts_node_child(node, index - 1));
        }
    }
    std::ranges::sort(spans, {}, &HighlightSpan::begin);
}

void append_fragment(std::vector<RichTextFragment>& fragments,
                     std::string_view text,
                     PreviewTextRole role) {
    if (text.empty()) {
        return;
    }
    if (!fragments.empty() && fragments.back().role == role) {
        fragments.back().text += text;
        return;
    }
    fragments.push_back(RichTextFragment{.text = std::string{text}, .role = role});
}

[[nodiscard]] RichTextPreview make_rich_preview(std::span<const std::string> lines,
                                                std::span<const HighlightSpan> spans) {
    RichTextPreview preview;
    preview.lines.reserve(lines.size());

    std::size_t line_begin = 0;
    std::size_t first_span = 0;
    for (const auto& line : lines) {
        const auto line_end = line_begin + line.size();
        while (first_span < spans.size() && spans[first_span].end <= line_begin) {
            ++first_span;
        }

        std::vector<RichTextFragment> fragments;
        std::size_t cursor = 0;
        for (auto index = first_span; index < spans.size() && spans[index].begin < line_end;
             ++index) {
            const auto span_begin = std::max<std::size_t>(spans[index].begin, line_begin);
            const auto span_end = std::min<std::size_t>(spans[index].end, line_end);
            const auto local_begin = span_begin - line_begin;
            const auto local_end = span_end - line_begin;
            if (local_end <= cursor) {
                continue;
            }
            append_fragment(fragments, std::string_view{line}.substr(cursor, local_begin - cursor),
                            PreviewTextRole::Normal);
            append_fragment(fragments,
                            std::string_view{line}.substr(local_begin, local_end - local_begin),
                            spans[index].role);
            cursor = local_end;
        }
        append_fragment(fragments, std::string_view{line}.substr(cursor), PreviewTextRole::Normal);
        preview.lines.push_back(std::move(fragments));
        line_begin = line_end + 1;
    }
    return preview;
}

}  // namespace

#endif

std::optional<RichTextPreview> highlight_with_tree_sitter(const std::filesystem::path& path,
                                                          std::string_view mime_type,
                                                          std::span<const std::string> lines) {
#if EXPP_HAS_TREE_SITTER
    const auto language_factory = find_language(path, mime_type);
    if (language_factory == nullptr) {
        return std::nullopt;
    }

    std::string source;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index > 0) {
            source.push_back('\n');
        }
        source += lines[index];
    }
    if (source.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }

    using Parser = std::unique_ptr<TSParser, decltype(&ts_parser_delete)>;
    Parser parser{ts_parser_new(), &ts_parser_delete};
    if (parser == nullptr || !ts_parser_set_language(parser.get(), language_factory())) {
        return std::nullopt;
    }

    using Tree = std::unique_ptr<TSTree, decltype(&ts_tree_delete)>;
    Tree tree{ts_parser_parse_string(parser.get(), nullptr, source.data(),
                                     static_cast<std::uint32_t>(source.size())),
              &ts_tree_delete};
    if (tree == nullptr) {
        return std::nullopt;
    }

    std::vector<HighlightSpan> spans;
    collect_highlight_spans(ts_tree_root_node(tree.get()), spans);
    return make_rich_preview(lines, spans);
#else
    (void)path;
    (void)mime_type;
    (void)lines;
    return std::nullopt;
#endif
}

}  // namespace expp::app
