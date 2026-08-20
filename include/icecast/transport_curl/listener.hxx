#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>

#include <icecast/core/error.hxx>
#include <icecast/core/result.hxx>
#include <icecast/stream/icy.hxx>
#include <icecast/stream/models.hxx>
#include <icecast/stream/state.hxx>
#include <icecast/transport_curl/export.hxx>

namespace icecast {

namespace detail {
struct curl_listener_impl;
struct curl_context_impl;
}

struct curl_listener_callbacks {
    std::function<stream_action(std::span<const std::byte>)> on_media;
    std::function<stream_action(const icy_metadata_event_view&)> on_metadata;
    std::function<void(const stream_info&)> on_stream_info;
    std::function<void(const listener_status&)> on_state;
    std::function<void(const error&)> on_error;
};

struct curl_listener_options {
    std::chrono::milliseconds connect_timeout{10'000};
    long max_redirects = 5;
};

struct curl_context;

struct curl_listener {
    curl_listener(const curl_listener&) = delete;
    curl_listener& operator=(const curl_listener&) = delete;
    ICECAST_CXX_TRANSPORT_CURL_API curl_listener(curl_listener&& other) noexcept;
    ICECAST_CXX_TRANSPORT_CURL_API curl_listener& operator=(curl_listener&& other) noexcept;
    ICECAST_CXX_TRANSPORT_CURL_API ~curl_listener();

    [[nodiscard]] ICECAST_CXX_TRANSPORT_CURL_API listener_status status() const noexcept;
    [[nodiscard]] ICECAST_CXX_TRANSPORT_CURL_API bool paused() const noexcept;
    [[nodiscard]] ICECAST_CXX_TRANSPORT_CURL_API bool finished() const noexcept;
    [[nodiscard]] ICECAST_CXX_TRANSPORT_CURL_API std::optional<stream_info> info() const;
    [[nodiscard]] ICECAST_CXX_TRANSPORT_CURL_API std::optional<error> last_error() const;
    ICECAST_CXX_TRANSPORT_CURL_API void resume() noexcept;
    ICECAST_CXX_TRANSPORT_CURL_API void stop() noexcept;

private:
    explicit curl_listener(std::shared_ptr<detail::curl_listener_impl> implementation) noexcept;
    std::shared_ptr<detail::curl_listener_impl> implementation_;
    friend struct curl_context;
};

struct curl_context {
    curl_context(const curl_context&) = delete;
    curl_context& operator=(const curl_context&) = delete;
    ICECAST_CXX_TRANSPORT_CURL_API curl_context(curl_context&& other) noexcept;
    ICECAST_CXX_TRANSPORT_CURL_API curl_context& operator=(curl_context&& other) noexcept;
    ICECAST_CXX_TRANSPORT_CURL_API ~curl_context();

    [[nodiscard]] static ICECAST_CXX_TRANSPORT_CURL_API result<curl_context> create();
    [[nodiscard]] ICECAST_CXX_TRANSPORT_CURL_API result<curl_listener> listen(listener_config config, curl_listener_callbacks callbacks, curl_listener_options options = {});
    [[nodiscard]] ICECAST_CXX_TRANSPORT_CURL_API result<void> poll(std::chrono::milliseconds max_wait = std::chrono::milliseconds{100});
    [[nodiscard]] ICECAST_CXX_TRANSPORT_CURL_API std::size_t active_listener_count() const noexcept;
    ICECAST_CXX_TRANSPORT_CURL_API void request_stop_all() noexcept;

private:
    explicit curl_context(std::unique_ptr<detail::curl_context_impl> implementation) noexcept;
    std::unique_ptr<detail::curl_context_impl> implementation_;
};

} // namespace icecast
