#pragma once

#include <cstdint>

#include <icecast/core/result.hxx>
#include <icecast/stream/export.hxx>

namespace icecast {

enum struct listener_state {
    idle,
    connecting,
    streaming,
    reconnect_wait,
    stopping,
    stopped,
    failed,
};

struct listener_status {
    listener_state state = listener_state::idle;
    std::uint64_t connection_generation = 0;
};

enum struct publisher_state {
    idle,
    connecting,
    publishing,
    stopping,
    stopped,
    interrupted,
    failed,
};

[[nodiscard]] ICECAST_CXX_STREAM_API bool is_valid_listener_transition(listener_state from, listener_state to) noexcept;
[[nodiscard]] ICECAST_CXX_STREAM_API result<void> transition_listener(listener_status& status, listener_state next);

} // namespace icecast
