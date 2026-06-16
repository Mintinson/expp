#ifndef EXPP_CORE_HELPER_HPP
#define EXPP_CORE_HELPER_HPP

#include <string>
#include <string_view>
#include <unordered_map>

namespace expp::core {

struct StringHash {
    using is_transparent = void;

    std::size_t operator()(std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
};

template <typename Value>
using StringHashMap = std::unordered_map<std::string, Value, StringHash, std::equal_to<>>;
}  // namespace expp::core

#endif // EXPP_CORE_HELPER_HPP