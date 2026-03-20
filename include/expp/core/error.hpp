/**
 * @file error.hpp
 * @brief Unified error handling system using std::expected
 *
 * This module provides:
 * - Strong-typed error categories
 * - Error context with source location
 * - Result alias for std::expected
 *
 * @copyright Copyright (c) 2026
 */

#ifndef EXPP_CORE_ERROR_HPP
#define EXPP_CORE_ERROR_HPP

#include <cstdint>
#include <expected>
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace expp::core {
enum class ErrorCategory : std::uint8_t {
    None,
    FileSystem,
    Permission,
    NotFound,
    InvalidArgument,
    InvalidState,
    IO,
    Config,
    UI,
    System,
    Unknown
};

/**
 * @brief Converts ErrorCategory to human-readable string
 * @param category The error category
 * @return String representation
 */
[[nodiscard]] constexpr std::string_view category_to_string(ErrorCategory category) noexcept {
    switch (category) {
        case ErrorCategory::None:
            return "None";
        case ErrorCategory::FileSystem:
            return "FileSystem";
        case ErrorCategory::Permission:
            return "Permission";
        case ErrorCategory::NotFound:
            return "NotFound";
        case ErrorCategory::InvalidArgument:
            return "InvalidArgument";
        case ErrorCategory::InvalidState:
            return "InvalidState";
        case ErrorCategory::IO:
            return "IO";
        case ErrorCategory::Config:
            return "Config";
        case ErrorCategory::UI:
            return "UI";
        case ErrorCategory::System:
            return "System";
        case ErrorCategory::Unknown:
            return "Unknown";
    }
    return "Unknown";  // Fallback for invalid enum values
}

/**
 * @brief Error class with context information
 *
 * Provides detailed error information including:
 * - Error category for programmatic handling
 * - Human-readable message
 * - Source location for debugging
 *
 * Thread-safety: This class is thread-safe for read operations.
 */
class Error {
public:
    /**
     * @brief Constructs an Error with full context
     * @param category Error category
     * @param message Human-readable error message
     * @param location Source location (auto-captured by default)
     */
    explicit Error(ErrorCategory category,
                   std::string message,
                   std::source_location location = std::source_location::current()) noexcept
        : category_(category)
        , message_(std::move(message))
        , location_(location) {}
    /**
     * @brief Constructs an Error with just a message (Unknown category)
     * @param message Human-readable error message
     * @param location Source location
     */
    explicit Error(std::string message, std::source_location location = std::source_location::current()) noexcept
        : category_(ErrorCategory::Unknown)
        , message_(std::move(message))
        , location_(location) {}

    [[nodiscard]] ErrorCategory category() const noexcept { return category_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] std::source_location location() const noexcept { return location_; }

    /**
     * @brief Formats the error for display
     * @return Formatted error string
     */
    [[nodiscard]] std::string format() const {
        return std::format("{}: {} [{}:{}]", category_to_string(category_), message_, location_.file_name(),
                           location_.line());
    }

    /**
     * @brief Checks if this error matches a specific category
     * @param cat Category to check
     * @return True if categories match
     */
    [[nodiscard]] bool isCategory(ErrorCategory cat) const noexcept { return category_ == cat; }

private:
    ErrorCategory category_;
    std::string message_;
    std::source_location location_;
};

/**
 * @brief Result type alias using std::expected
 * @tparam T Success value type
 *
 * Usage:
 * @code
 * Result<int> compute() {
 *     if (error_condition)
 *         return std::unexpected(Error{ErrorCategory::InvalidArgument, "bad input"});
 *     return 42;
 * }
 * @endcode
 */
template <typename T>
using Result = std::expected<T, Error>;

/**
 * @brief VoidResult for operations that return nothing on success
 */
using VoidResult = Result<void>;
/**
 * @brief Factory function to create an unexpected error
 * @param category Error category
 * @param message Error message
 * @param location Source location
 * @return std::unexpected containing the Error
 */
[[nodiscard]] inline auto make_error(ErrorCategory category,
                                    std::string message,
                                    std::source_location location = std::source_location::current()) {
    return std::unexpected(Error{category, std::move(message), location});
}
/**
 * @brief Factory function to create an unexpected error with just message
 * @param message Error message
 * @param location Source location
 * @return std::unexpected containing the Error
 */
[[nodiscard]] inline auto make_error(std::string message,
                                    std::source_location location = std::source_location::current()) {
    return std::unexpected(Error{std::move(message), location});
}

}  // namespace expp::core

#endif  // EXPP_CORE_ERROR_HPP