#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <icecast/core/export.hxx>
#include <icecast/core/result.hxx>

namespace icecast {

enum struct endpoint_scheme {
    http,
    https,
};

struct server_endpoint {
    endpoint_scheme scheme = endpoint_scheme::http;
    std::string host;
    std::optional<std::uint16_t> port;
    std::string base_path;
};

struct mountpoint {
    std::string path = "/";
};

[[nodiscard]] ICECAST_CXX_CORE_API result<server_endpoint> parse_server_endpoint(std::string_view value);
[[nodiscard]] ICECAST_CXX_CORE_API result<mountpoint> parse_mountpoint(std::string_view value);
[[nodiscard]] ICECAST_CXX_CORE_API result<void> validate_server_endpoint(const server_endpoint& endpoint);
[[nodiscard]] ICECAST_CXX_CORE_API result<void> validate_mountpoint(const mountpoint& value);
[[nodiscard]] ICECAST_CXX_CORE_API std::uint16_t effective_port(const server_endpoint& endpoint) noexcept;
[[nodiscard]] ICECAST_CXX_CORE_API std::string_view to_string(endpoint_scheme scheme) noexcept;
[[nodiscard]] ICECAST_CXX_CORE_API std::string format_server_endpoint(const server_endpoint& endpoint);
[[nodiscard]] ICECAST_CXX_CORE_API result<std::string> resolve_mount_url(const server_endpoint& endpoint, const mountpoint& value);

} // namespace icecast
