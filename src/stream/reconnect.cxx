#include <algorithm>
#include <cmath>
#include <cstdint>

#include <icecast/stream/reconnect.hxx>

namespace icecast {

namespace {

[[nodiscard]] error reconnect_error(std::string message) {
    return {
        .category = error_category::configuration,
        .message = std::move(message),
        .operation = "validate reconnect policy",
        .retryable = false,
        .http_status = std::nullopt,
        .backend = {},
        .backend_code = std::nullopt,
    };
}

} // namespace

result<void> validate_reconnect_policy(const reconnect_policy& policy) {
    if (policy.initial_delay.count() < 0) {
        return result<void>::failure(reconnect_error("initial reconnect delay cannot be negative"));
    }
    if (policy.max_delay.count() < 0) {
        return result<void>::failure(reconnect_error("maximum reconnect delay cannot be negative"));
    }
    if (policy.max_delay < policy.initial_delay) {
        return result<void>::failure(reconnect_error("maximum reconnect delay cannot be less than the initial delay"));
    }
    if ((not std::isfinite(policy.multiplier)) or (policy.multiplier < 1.0)) {
        return result<void>::failure(reconnect_error("reconnect multiplier must be finite and at least 1.0"));
    }
    if ((not std::isfinite(policy.jitter)) or (policy.jitter < 0.0) or (policy.jitter > 1.0)) {
        return result<void>::failure(reconnect_error("reconnect jitter must be finite and between 0.0 and 1.0"));
    }
    return result<void>::success();
}

bool allows_reconnect(const reconnect_policy& policy, std::size_t attempts_made) noexcept {
    if (not policy.enabled) {
        return false;
    }
    return (not policy.max_attempts.has_value()) or (attempts_made < policy.max_attempts.value());
}

std::chrono::milliseconds reconnect_base_delay(const reconnect_policy& policy, std::size_t attempt_index) noexcept {
    const auto maximum_count = std::max<std::int64_t>(policy.max_delay.count(), 0);
    if (maximum_count == 0) {
        return std::chrono::milliseconds::zero();
    }

    const auto initial_count = std::clamp<std::int64_t>(policy.initial_delay.count(), 0, maximum_count);
    long double delay = static_cast<long double>(initial_count);
    const auto maximum = static_cast<long double>(maximum_count);
    const auto multiplier = (std::isfinite(policy.multiplier) and (policy.multiplier >= 1.0)) ? policy.multiplier : 1.0;

    for (std::size_t index = 0; (index < attempt_index) and (delay < maximum); ++index) {
        delay *= static_cast<long double>(multiplier);
        if ((not std::isfinite(static_cast<double>(delay))) or (delay >= maximum)) {
            delay = maximum;
            break;
        }
    }

    const auto rounded = std::llround(std::clamp(delay, 0.0L, maximum));
    return std::chrono::milliseconds{rounded};
}

} // namespace icecast
