#pragma once

#include <icecast/core/error.hxx>
#include <icecast/core/result.hxx>
#include <icecast/publish_libshout/export.hxx>
#include <icecast/stream/models.hxx>
#include <icecast/stream/state.hxx>

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace icecast {

namespace detail {
struct libshout_publisher_impl;
struct libshout_context_impl;
}

struct libshout_publisher_callbacks {
    std::function<void(publisher_state)> on_state;
    std::function<void(const error&)> on_error;
};

struct libshout_publisher_options {
    std::chrono::milliseconds connect_timeout{10'000};
    std::size_t send_chunk_bytes = 16 * 1024;
};

struct libshout_context;

struct libshout_publisher {
    libshout_publisher(const libshout_publisher&) = delete;
    libshout_publisher& operator=(const libshout_publisher&) = delete;
    ICECAST_CXX_PUBLISH_LIBSHOUT_API libshout_publisher(libshout_publisher&& other) noexcept;
    ICECAST_CXX_PUBLISH_LIBSHOUT_API libshout_publisher& operator=(libshout_publisher&& other) noexcept;
    ICECAST_CXX_PUBLISH_LIBSHOUT_API ~libshout_publisher();

    [[nodiscard]] ICECAST_CXX_PUBLISH_LIBSHOUT_API publisher_state state() const noexcept;
    [[nodiscard]] ICECAST_CXX_PUBLISH_LIBSHOUT_API bool finished() const noexcept;
    [[nodiscard]] ICECAST_CXX_PUBLISH_LIBSHOUT_API std::size_t buffered_bytes() const noexcept;
    [[nodiscard]] ICECAST_CXX_PUBLISH_LIBSHOUT_API std::optional<error> last_error() const;
    [[nodiscard]] ICECAST_CXX_PUBLISH_LIBSHOUT_API result<publisher_write_result> write(std::span<const std::byte> encoded_bytes);
    [[nodiscard]] ICECAST_CXX_PUBLISH_LIBSHOUT_API result<void> finish();
    ICECAST_CXX_PUBLISH_LIBSHOUT_API void abort() noexcept;

private:
    explicit libshout_publisher(std::shared_ptr<detail::libshout_publisher_impl> implementation) noexcept;
    std::shared_ptr<detail::libshout_publisher_impl> m_implementation;
    friend struct libshout_context;
};

struct libshout_context {
    libshout_context(const libshout_context&) = delete;
    libshout_context& operator=(const libshout_context&) = delete;
    ICECAST_CXX_PUBLISH_LIBSHOUT_API libshout_context(libshout_context&& other) noexcept;
    ICECAST_CXX_PUBLISH_LIBSHOUT_API libshout_context& operator=(libshout_context&& other) noexcept;
    ICECAST_CXX_PUBLISH_LIBSHOUT_API ~libshout_context();

    [[nodiscard]] static ICECAST_CXX_PUBLISH_LIBSHOUT_API result<libshout_context> create();
    [[nodiscard]] ICECAST_CXX_PUBLISH_LIBSHOUT_API result<libshout_publisher> publish(publisher_config config, libshout_publisher_callbacks callbacks = {}, libshout_publisher_options options = {});
    [[nodiscard]] ICECAST_CXX_PUBLISH_LIBSHOUT_API result<void> poll();
    [[nodiscard]] ICECAST_CXX_PUBLISH_LIBSHOUT_API std::size_t active_publisher_count() const noexcept;
    ICECAST_CXX_PUBLISH_LIBSHOUT_API void request_stop_all() noexcept;

private:
    explicit libshout_context(std::unique_ptr<detail::libshout_context_impl> implementation) noexcept;
    std::unique_ptr<detail::libshout_context_impl> m_implementation;
};

} // namespace icecast
