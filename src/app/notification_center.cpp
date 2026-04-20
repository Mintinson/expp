#include "expp/app/notification_center.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace expp::app {

/**
 * @brief Creates notification options from a configuration object
 * @param config The notification configuration
 * @return The corresponding notification options
 */
NotificationOptions make_notification_options(const core::NotificationConfig& config) noexcept {
    return NotificationOptions{
        .durationMs = std::max(0, config.durationMs),
        .showSuccess = config.showSuccess,
        .showInfo = config.showInfo,
    };
}

/**
 * @brief Determines the severity of a notification based on the error category
 * @param error The error for which to determine severity
 * @return The appropriate toast severity
 */
ui::ToastSeverity severity_for_error(const core::Error& error) noexcept {
    switch (error.category()) {
        case core::ErrorCategory::InvalidArgument:
        case core::ErrorCategory::InvalidState:
        case core::ErrorCategory::Permission:
        case core::ErrorCategory::NotFound:
        case core::ErrorCategory::NoSupport:
            return ui::ToastSeverity::Warning;
        case core::ErrorCategory::None:
        case core::ErrorCategory::FileSystem:
        case core::ErrorCategory::IO:
        case core::ErrorCategory::Config:
        case core::ErrorCategory::UI:
        case core::ErrorCategory::System:
        case core::ErrorCategory::Unknown:
        default:
            return ui::ToastSeverity::Error;
    }
}

bool publish_if_error(NotificationCenter& notifications,
                      core::VoidResult result,
                      std::string success_message,
                      const ui::ToastSeverity success_severity) {
    if (!result) {
        notifications.publish(severity_for_error(result.error()), result.error().message());
        return false;
    }

    if (!success_message.empty()) {
        notifications.publish(success_severity, std::move(success_message));
    }
    return true;
}

NotificationCenter::NotificationCenter(NotificationOptions options) : options_(options) {}

void NotificationCenter::publish(ui::ToastSeverity severity, std::string message, Clock::time_point now) {
    if (!isEnabled(severity) || message.empty()) {
        return;
    }

    activeToast_ = ui::ToastInfo{
        .severity = severity,
        .message = std::move(message),
    };
    expiresAt_ = now + std::chrono::milliseconds(options_.durationMs);
    if (publishObserver_) {
        publishObserver_(expiresAt_);
    }
}

void NotificationCenter::expire(Clock::time_point now) {
    if (!activeToast_.has_value()) {
        return;
    }
    if (now >= expiresAt_) {
        activeToast_.reset();
    }
}

void NotificationCenter::clear() {
    activeToast_.reset();
}

void NotificationCenter::setPublishObserver(PublishObserver observer) {
    publishObserver_ = std::move(observer);
}

const std::optional<ui::ToastInfo>& NotificationCenter::current() const noexcept {
    return activeToast_;
}

bool NotificationCenter::isEnabled(ui::ToastSeverity severity) const noexcept {
    switch (severity) {
        case ui::ToastSeverity::Info:
            return options_.showInfo;
        case ui::ToastSeverity::Success:
            return options_.showSuccess;
        case ui::ToastSeverity::Warning:
        case ui::ToastSeverity::Error:
            return true;
        default:
            return true;
    }
}

}  // namespace expp::app
