#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <icecast/core/export.hxx>
#include <icecast/core/result.hxx>

namespace icecast {

struct header_field {
    std::string name;
    std::string value;
};

struct headers {
    [[nodiscard]] ICECAST_CXX_CORE_API result<void> append(std::string name, std::string value);
    [[nodiscard]] ICECAST_CXX_CORE_API result<void> set(std::string name, std::string value);
    [[nodiscard]] ICECAST_CXX_CORE_API std::size_t erase(std::string_view name) noexcept;
    [[nodiscard]] ICECAST_CXX_CORE_API bool contains(std::string_view name) const noexcept;
    [[nodiscard]] ICECAST_CXX_CORE_API std::optional<std::string_view> first(std::string_view name) const noexcept;
    [[nodiscard]] ICECAST_CXX_CORE_API std::vector<std::string_view> all(std::string_view name) const;

    std::vector<header_field> fields;
};

[[nodiscard]] ICECAST_CXX_CORE_API bool header_name_equal(std::string_view left, std::string_view right) noexcept;
[[nodiscard]] ICECAST_CXX_CORE_API result<void> validate_header_field(const header_field& field);

} // namespace icecast
