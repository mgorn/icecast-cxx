#pragma once

#include <chrono>
#include <cstddef>
#include <optional>

#include <icecast/core/result.hxx>
#include <icecast/stream/export.hxx>

namespace icecast {

struct reconnect_policy {
    bool enabled = true;
    std::optional<std::size_t> max_attempts;
    std::chrono::milliseconds initial_delay{500};
    std::chrono::milliseconds max_delay{30'000};
    double multiplier = 2.0;
    double jitter = 0.2;
};

[[nodiscard]] ICECAST_CXX_STREAM_API result<void> validate_reconnect_policy(const reconnect_policy& policy);
[[nodiscard]] ICECAST_CXX_STREAM_API bool allows_reconnect(const reconnect_policy& policy, std::size_t attempts_made) noexcept;
[[nodiscard]] ICECAST_CXX_STREAM_API std::chrono::milliseconds reconnect_base_delay(const reconnect_policy& policy, std::size_t attempt_index) noexcept;

} // namespace icecast
