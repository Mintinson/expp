#ifndef EXPP_APP_NOTIFICATION_CENTER_HPP
#define EXPP_APP_NOTIFICATION_CENTER_HPP

#include "expp/core/config.hpp"
#include "expp/core/error.hpp"
#include "expp/ui/components.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <string>

namespace expp::app {

struct NotificationOptions {
    int durationMs{2500};
    bool showSuccess{true};
    bool showInfo{true};
};

[[nodiscard]] NotificationOptions make_notification_options(const core::NotificationConfig& config) noexcept;

[[nodiscard]] ui::ToastSeverity severity_for_error(const core::Error& error) noexcept;

class NotificationCenter;

[[nodiscard]] bool publish_if_error(NotificationCenter& notifications,
                                    core::VoidResult result,
                                    std::string success_message = {},
                                    ui::ToastSeverity success_severity = ui::ToastSeverity::Success);

class NotificationCenter {
public:
    using Clock = std::chrono::steady_clock;
    using PublishObserver = std::function<void(Clock::time_point)>;

    explicit NotificationCenter(NotificationOptions options = {});

    void publish(ui::ToastSeverity severity, std::string message, Clock::time_point now = Clock::now());

    void expire(Clock::time_point now = Clock::now());

    void clear();

    void setPublishObserver(PublishObserver observer);

    [[nodiscard]] const std::optional<ui::ToastInfo>& current() const noexcept;

private:
    [[nodiscard]] bool isEnabled(ui::ToastSeverity severity) const noexcept;

    NotificationOptions options_;
    std::optional<ui::ToastInfo> activeToast_;
    Clock::time_point expiresAt_{};
    PublishObserver publishObserver_;
};

}  // namespace expp::app

#endif  // EXPP_APP_NOTIFICATION_CENTER_HPP
