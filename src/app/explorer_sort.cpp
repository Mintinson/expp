#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>
#include <vector>

#include <expp/app/explorer.hpp>
#include <expp/core/filesystem.hpp>

namespace expp::app {

namespace rng = std::ranges;

namespace {

[[nodiscard]] char to_lower_ascii(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

[[nodiscard]] int compare_lexicographic_insensitive(std::string_view lhs, std::string_view rhs) {
    const std::size_t common = std::min(lhs.size(), rhs.size());
    for (std::size_t index = 0; index < common; ++index) {
        const char lhs_char = to_lower_ascii(lhs[index]);
        const char rhs_char = to_lower_ascii(rhs[index]);
        if (lhs_char < rhs_char) {
            return -1;
        }
        if (lhs_char > rhs_char) {
            return 1;
        }
    }

    if (lhs.size() < rhs.size()) {
        return -1;
    }
    if (lhs.size() > rhs.size()) {
        return 1;
    }
    return 0;
}

[[nodiscard]] inline bool is_ascii_digit(char c) noexcept {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

/**
 * @brief Extracts a numeric chunk from a string view, skipping leading zeros.
 * @param s The string view to extract the numeric chunk from.
 * @param start_idx The starting index within the string view where the numeric chunk begins.
 * @return A pair containing the numeric chunk as a string view (with leading zeros removed) and the index immediately
 * after the last digit of the numeric chunk.
 */
[[nodiscard]] inline std::pair<std::string_view, std::size_t> extract_numeric_chunk(std::string_view s,
                                                                                    std::size_t start_idx) noexcept {
    std::size_t end_idx = start_idx;
    while (end_idx < s.size() && is_ascii_digit(s[end_idx])) {
        ++end_idx;
    }

    std::size_t sig_start = start_idx;
    while (sig_start < end_idx && s[sig_start] == '0') {
        ++sig_start;
    }

    return {s.substr(sig_start, end_idx - sig_start), end_idx};
}

/**
 * @brief Compares two strings using natural (human-friendly) ordering with case-insensitive comparison.
 * @param lhs The left-hand side string to compare.
 * @param rhs The right-hand side string to compare.
 * @return A negative value if lhs comes before rhs, a positive value if lhs comes after rhs, or zero if they are
 * equivalent. Numeric sequences within the strings are compared numerically rather than lexicographically.
 */
[[nodiscard]] int compare_natural_insensitive(  // NOLINT(readability-function-cognitive-complexity)
    std::string_view lhs,
    std::string_view rhs) {
    std::size_t lhs_index = 0;
    std::size_t rhs_index = 0;

    while (lhs_index < lhs.size() && rhs_index < rhs.size()) {
        // for contiguous digit sequences, compare as numbers
        if (is_ascii_digit(lhs[lhs_index]) && is_ascii_digit(rhs[rhs_index])) {
            const auto [lhs_sig, lhs_next] = extract_numeric_chunk(lhs, lhs_index);
            const auto [rhs_sig, rhs_next] = extract_numeric_chunk(rhs, rhs_index);

            // first compare the lengths of the significant digit sequences (i.e. ignore leading zeros)
            if (lhs_sig.size() != rhs_sig.size()) {
                return lhs_sig.size() < rhs_sig.size() ? -1 : 1;
            }

            // if the significant digit sequences are the same length, compare them lexicographically
            const int num_cmp = lhs_sig.compare(rhs_sig);
            if (num_cmp != 0) {
                return num_cmp < 0 ? -1 : 1;
            }

            // if digit sequences are identical, go ahead the comparison.
            lhs_index = lhs_next;
            rhs_index = rhs_next;
            continue;
        }

        // for non-digit characters, compare case-insensitively
        const char lhs_char = to_lower_ascii(lhs[lhs_index]);
        const char rhs_char = to_lower_ascii(rhs[rhs_index]);
        if (lhs_char != rhs_char) {
            return lhs_char < rhs_char ? -1 : 1;
        }

        ++lhs_index;
        ++rhs_index;
    }

    // the prefix is the same, so the shorter string should come first
    if (lhs_index < lhs.size()) {
        return 1;
    }
    if (rhs_index < rhs.size()) {
        return -1;
    }

    return 0;
}

template <typename T>
[[nodiscard]] int compare_value(const T& lhs, const T& rhs) {
    if (lhs < rhs) {
        return -1;
    }
    if (rhs < lhs) {
        return 1;
    }
    return 0;
}

}  // namespace

int compare_by_sort_field(const core::filesystem::FileEntry& lhs,
                          const core::filesystem::FileEntry& rhs,
                          const SortOrder& order) {
    switch (order.field) {
        case SortOrder::Field::ModifiedTime:
            return compare_value(lhs.lastModified, rhs.lastModified);
        case SortOrder::Field::BirthTime:
            return compare_value(lhs.birthTime, rhs.birthTime);
        case SortOrder::Field::Extension: {
            const int extension_compare = compare_lexicographic_insensitive(lhs.extension(), rhs.extension());
            if (extension_compare != 0) {
                return extension_compare;
            }
            // fall back to filename comparison when extensions are the same or both entries have no extension
            return compare_lexicographic_insensitive(lhs.filename(), rhs.filename());
        }
        case SortOrder::Field::Alphabetical:
            return compare_lexicographic_insensitive(lhs.filename(), rhs.filename());
        case SortOrder::Field::Natural:
            return compare_natural_insensitive(lhs.filename(), rhs.filename());
        case SortOrder::Field::Size:
            return compare_value(lhs.size, rhs.size);
        default:
            return 0;
    }
}

void sort_entries(std::vector<core::filesystem::FileEntry>& entries, const SortOrder& order) {
    rng::stable_sort(entries, [&](const auto& lhs, const auto& rhs) {
        // directories are always sorted before files, regardless of the selected sort field
        if (lhs.isDirectory() != rhs.isDirectory()) {
            return lhs.isDirectory();
        }

        const int field_compare = compare_by_sort_field(lhs, rhs, order);
        if (field_compare != 0) {
            return order.direction == SortOrder::Direction::Ascending ? field_compare < 0 : field_compare > 0;
        }

        // if the selected sort field considers the entries equivalent
        // fall back to a natural, case-insensitive ascending filename comparison
        const int natural_compare = compare_natural_insensitive(lhs.filename(), rhs.filename());
        if (natural_compare != 0) {
            return natural_compare < 0;
        }

        // the last fallback is to sort by full path to ensure a deterministic order
        // even for entries with identical filenames (e.g. hard links)
        return lhs.path.generic_u8string() < rhs.path.generic_u8string();
    });
}

}  // namespace expp::app
