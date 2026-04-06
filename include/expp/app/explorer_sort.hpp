#ifndef EXPP_APP_EXPLORER_SORT_HPP
#define EXPP_APP_EXPLORER_SORT_HPP

#include "expp/app/explorer.hpp"
#include "expp/core/filesystem.hpp"

#include <vector>

namespace expp::app {


[[nodiscard]] int compare_by_sort_field(const core::filesystem::FileEntry& lhs,
                                        const core::filesystem::FileEntry& rhs,
                                        const SortOrder& order);

void sort_entries(std::vector<core::filesystem::FileEntry>& entries, const SortOrder& order);

}  // namespace expp::app

#endif  // EXPP_APP_EXPLORER_SORT_HPP
