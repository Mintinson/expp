#ifndef EXPP_APP_PREVIEW_PROVIDER_HPP
#define EXPP_APP_PREVIEW_PROVIDER_HPP

/**
 * @file preview_provider.hpp
 * @brief Provider registry for MIME-driven preview dispatch.
 */

#include "expp/app/explorer_services.hpp"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace expp::app {

/**
 * @brief MIME pattern and priority declared by a preview provider.
 *
 * Patterns may be exact MIME types, major-type prefix wildcards, or a global
 * fallback pattern.
 */
struct PreviewProviderCapability {
    std::string mimePattern;
    int priority{0};
};

/**
 * @brief EXTENSION POINT: MIME-routed preview content provider.
 */
class PreviewProvider {
public:
    virtual ~PreviewProvider() = default;

    [[nodiscard]] virtual std::span<const PreviewProviderCapability> capabilities()
        const noexcept = 0;

    [[nodiscard]] virtual core::Task<core::Result<PreviewPayload>> load(
        const PreviewRequest& request, const MimePayload& mime) const = 0;
};

/**
 * @brief Ordered registry that selects the highest-priority provider for a MIME type.
 */
class PreviewProviderRegistry {
public:
    void registerProvider(std::shared_ptr<PreviewProvider> provider);

    [[nodiscard]] std::shared_ptr<PreviewProvider> findProvider(
        std::string_view mime_type) const noexcept;

private:
    std::vector<std::shared_ptr<PreviewProvider>> providers_;
};

[[nodiscard]] bool preview_mime_matches(std::string_view pattern,
                                        std::string_view mime_type) noexcept;

}  // namespace expp::app

#endif  // EXPP_APP_PREVIEW_PROVIDER_HPP
