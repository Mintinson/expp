/**
 * @file explorer_view.hpp
 * @brief TUI view for the file explorer
 *
 * @copyright Copyright (c) 2026
 */

#ifndef EXPP_APP_EXPLORER_VIEW_HPP
#define EXPP_APP_EXPLORER_VIEW_HPP

#include "expp/app/explorer.hpp"

#include <memory>



namespace expp::app {
class ExplorerView {
public:
    /**
     * @brief Construct a new Explorer View
     * @param explorer The explorer controller
     * @param config The application configuration
     */
    explicit ExplorerView(std::shared_ptr<Explorer> explorer
                          // const core::ConfigManager& config
    );

    ~ExplorerView();

    // Non-copyable
    ExplorerView(const ExplorerView&) = delete;
    ExplorerView& operator=(const ExplorerView&) = delete;

    // Movable
    ExplorerView(ExplorerView&&) noexcept;
    ExplorerView& operator=(ExplorerView&&) noexcept;

    /**
     * @brief Run the main event loop
     * @return Exit code (0 on success)
     */
    int run();

    /**
     * @brief Request the view to exit
     */
    void requestExit();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace expp::app

#endif  // EXPP_APP_EXPLORER_VIEW_HPP