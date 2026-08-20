#include <limits>
#include <string>

#include <icecast/stream/state.hxx>

namespace icecast {

namespace {

[[nodiscard]] error transition_error(std::string message) {
    return {
        .category = error_category::internal,
        .message = std::move(message),
        .operation = "transition listener state",
        .retryable = false,
        .http_status = std::nullopt,
        .backend = {},
        .backend_code = std::nullopt,
    };
}

} // namespace

bool is_valid_listener_transition(listener_state from, listener_state to) noexcept {
    if (from == to) {
        return false;
    }

    switch (from) {
        case listener_state::idle:
            return (to == listener_state::connecting) or (to == listener_state::stopped);
        case listener_state::connecting:
            return (to == listener_state::streaming) or (to == listener_state::reconnect_wait) or (to == listener_state::stopping) or (to == listener_state::failed);
        case listener_state::streaming:
            return (to == listener_state::reconnect_wait) or (to == listener_state::stopping) or (to == listener_state::stopped) or (to == listener_state::failed);
        case listener_state::reconnect_wait:
            return (to == listener_state::connecting) or (to == listener_state::stopping) or (to == listener_state::stopped) or (to == listener_state::failed);
        case listener_state::stopping:
            return (to == listener_state::stopped) or (to == listener_state::failed);
        case listener_state::stopped:
            return to == listener_state::connecting;
        case listener_state::failed:
            return (to == listener_state::connecting) or (to == listener_state::stopped);
    }
    return false;
}

result<void> transition_listener(listener_status& status, listener_state next) {
    if (not is_valid_listener_transition(status.state, next)) {
        return result<void>::failure(transition_error("invalid listener state transition"));
    }

    if (next == listener_state::streaming) {
        if (status.connection_generation == std::numeric_limits<std::uint64_t>::max()) {
            return result<void>::failure(transition_error("listener connection generation overflow"));
        }
        ++status.connection_generation;
    }

    status.state = next;
    return result<void>::success();
}

} // namespace icecast
