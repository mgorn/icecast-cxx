#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <icecast/core/credentials.hxx>
#include <icecast/core/endpoint.hxx>
#include <icecast/core/headers.hxx>
#include <icecast/core/result.hxx>
#include <icecast/stream/export.hxx>
#include <icecast/stream/reconnect.hxx>

namespace icecast {

enum struct stream_action {
    continue_stream,
    pause,
    stop,
};

struct stream_metadata {
    std::optional<std::string> name;
    std::optional<std::string> description;
    std::optional<std::string> genre;
    std::optional<std::string> url;
};

struct stream_info {
    std::string content_type;
    headers response_headers;
    std::optional<std::size_t> icy_metadata_interval;
};

struct listener_config {
    server_endpoint endpoint;
    mountpoint mount;
    std::optional<basic_credentials> credentials;
    headers request_headers;
    bool request_icy_metadata = true;
    reconnect_policy reconnect;
};

struct publisher_config {
    server_endpoint endpoint;
    mountpoint mount;
    basic_credentials credentials{"source", {}};
    std::string content_type;
    headers request_headers;
    stream_metadata metadata;
    bool public_stream = false;
    std::size_t max_buffered_bytes = 256 * 1024;
};

[[nodiscard]] ICECAST_CXX_STREAM_API result<void> validate_listener_config(const listener_config& config);
[[nodiscard]] ICECAST_CXX_STREAM_API result<void> validate_publisher_config(const publisher_config& config);

} // namespace icecast
