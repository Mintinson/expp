#include "expp/app/preview_provider.hpp"

#include <limits>
#include <string_view>

namespace expp::app {

bool preview_mime_matches(std::string_view pattern, std::string_view mime_type) noexcept {
    if (pattern == "*/*" || pattern == "*") {
        return true;
    }
    if (pattern == mime_type) {
        return true;
    }

    constexpr std::string_view kWildcardSuffix{"/*"};
    if (pattern.ends_with(kWildcardSuffix)) {
        const auto prefix = pattern.substr(0, pattern.size() - 1);
        return mime_type.starts_with(prefix);
    }
    return false;
}

void PreviewProviderRegistry::registerProvider(std::shared_ptr<PreviewProvider> provider) {
    if (!provider) {
        return;
    }
    providers_.push_back(std::move(provider));
}

std::shared_ptr<PreviewProvider> PreviewProviderRegistry::findProvider(
    std::string_view mime_type) const noexcept {
    std::shared_ptr<PreviewProvider> best_provider;
    int best_priority = std::numeric_limits<int>::min();

    for (const auto& provider : providers_) {
        for (const auto& capability : provider->capabilities()) {
            if (!preview_mime_matches(capability.mimePattern, mime_type)) {
                continue;
            }
            if (capability.priority > best_priority) {
                best_priority = capability.priority;
                best_provider = provider;
            }
        }
    }

    return best_provider;
}

}  // namespace expp::app
