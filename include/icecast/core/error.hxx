#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <icecast/core/export.hxx>

namespace icecast {

enum struct error_category {
    configuration,
    cancelled,
    timeout,
    name_resolution,
    connection,
    tls,
    authentication,
    http,
    protocol,
    server_rejected,
    unsupported,
    browser_policy,
    backpressure,
    internal,
};

struct error {
    error_category category = error_category::internal;
    std::string message;
    std::string operation;
    bool retryable = false;
    std::optional<int> http_status;
    std::string backend;
    std::optional<std::int64_t> backend_code;
};

[[nodiscard]] ICECAST_CXX_CORE_API std::string_view to_string(error_category category) noexcept;

} // namespace icecast
