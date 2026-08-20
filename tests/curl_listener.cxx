#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <icecast/transport_curl.hxx>

namespace {

#ifdef _WIN32
using socket_handle = SOCKET;
inline constexpr socket_handle invalid_socket_handle = INVALID_SOCKET;

struct socket_runtime {
    socket_runtime() {
        WSADATA data{};
        assert(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    }

    ~socket_runtime() {
        WSACleanup();
    }
};

void close_socket(socket_handle value) noexcept {
    if (value != invalid_socket_handle) {
        closesocket(value);
    }
}

void shutdown_socket(socket_handle value) noexcept {
    if (value != invalid_socket_handle) {
        shutdown(value, SD_BOTH);
    }
}
#else
using socket_handle = int;
inline constexpr socket_handle invalid_socket_handle = -1;

struct socket_runtime {};

void close_socket(socket_handle value) noexcept {
    if (value != invalid_socket_handle) {
        close(value);
    }
}

void shutdown_socket(socket_handle value) noexcept {
    if (value != invalid_socket_handle) {
        shutdown(value, SHUT_RDWR);
    }
}
#endif

[[nodiscard]] int socket_error(socket_handle value) noexcept {
#ifdef _WIN32
    return value == invalid_socket_handle ? -1 : 0;
#else
    return value < 0 ? -1 : 0;
#endif
}

[[nodiscard]] std::size_t receive_some(socket_handle socket, std::span<char> buffer) {
#ifdef _WIN32
    const auto capacity = static_cast<int>(std::min<std::size_t>(buffer.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
    const auto count = recv(socket, buffer.data(), capacity, 0);
#else
    const auto count = recv(socket, buffer.data(), buffer.size(), 0);
#endif
    return count > 0 ? static_cast<std::size_t>(count) : 0;
}

[[nodiscard]] std::size_t send_some(socket_handle socket, std::string_view value) {
#ifdef _WIN32
    const auto count = static_cast<int>(std::min<std::size_t>(value.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
    const auto sent = send(socket, value.data(), count, 0);
#else
#ifdef MSG_NOSIGNAL
    const auto sent = send(socket, value.data(), value.size(), MSG_NOSIGNAL);
#else
    const auto sent = send(socket, value.data(), value.size(), 0);
#endif
#endif
    return sent > 0 ? static_cast<std::size_t>(sent) : 0;
}

struct loopback_server {
    socket_runtime runtime;
    socket_handle listening_socket = invalid_socket_handle;
    std::uint16_t port = 0;
    std::thread worker;
    std::string request;

    explicit loopback_server(std::string response) {
        listening_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        assert(socket_error(listening_socket) == 0);

        int enabled = 1;
        assert(setsockopt(listening_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&enabled), sizeof(enabled)) == 0);

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        assert(bind(listening_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);

#ifdef _WIN32
        int address_size = sizeof(address);
#else
        socklen_t address_size = sizeof(address);
#endif
        assert(getsockname(listening_socket, reinterpret_cast<sockaddr*>(&address), &address_size) == 0);
        port = ntohs(address.sin_port);
        assert(::listen(listening_socket, 1) == 0);

        worker = std::thread([this, response = std::move(response)] {
            const auto client = accept(listening_socket, nullptr, nullptr);
            assert(socket_error(client) == 0);

            std::array<char, 4096> buffer{};
            while (request.find("\r\n\r\n") == std::string::npos) {
                const auto count = receive_some(client, buffer);
                if (count == 0) {
                    break;
                }
                request.append(buffer.data(), count);
            }

            std::size_t offset = 0;
            while (offset < response.size()) {
                const auto count = send_some(client, std::string_view{response}.substr(offset));
                if (count == 0) {
                    break;
                }
                offset += count;
            }

            shutdown_socket(client);
            close_socket(client);
        });
    }

    loopback_server(const loopback_server&) = delete;
    loopback_server& operator=(const loopback_server&) = delete;

    ~loopback_server() {
        if (worker.joinable()) {
            worker.join();
        }
        close_socket(listening_socket);
    }

    void join() {
        if (worker.joinable()) {
            worker.join();
        }
    }
};

[[nodiscard]] std::string icy_response() {
    std::string response = "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nicy-metaint: 5\r\nConnection: close\r\n\r\nABCDE";
    response.push_back('\x01');
    const std::string metadata = "StreamTitle='X';";
    assert(metadata.size() == 16);
    response += metadata;
    response += "FGHIJ";
    response.push_back('\x00');
    response += "KLMNO";
    return response;
}

void test_listener_icy_pause_resume_and_stop() {
    loopback_server http{icy_response()};

    auto context_result = icecast::curl_context::create();
    assert(context_result);
    auto context = std::move(context_result).value();

    icecast::listener_config config;
    config.endpoint.host = "127.0.0.1";
    config.endpoint.port = http.port;
    config.mount.path = "/stream";
    config.credentials = icecast::basic_credentials{"listener", "secret"};
    config.reconnect.enabled = false;

    std::vector<std::byte> media;
    std::vector<std::byte> metadata;
    std::vector<icecast::listener_state> states;
    bool saw_info = false;
    bool saw_error = false;
    bool pause_once = true;

    icecast::curl_listener_callbacks callbacks;
    callbacks.on_media = [&](std::span<const std::byte> value) {
        media.insert(media.end(), value.begin(), value.end());
        if (pause_once) {
            pause_once = false;
            return icecast::stream_action::pause;
        }
        if (media.size() >= 15) {
            return icecast::stream_action::stop;
        }
        return icecast::stream_action::continue_stream;
    };
    callbacks.on_metadata = [&](const icecast::icy_metadata_event_view& event) {
        metadata.assign(event.metadata.payload.begin(), event.metadata.payload.end());
        return icecast::stream_action::continue_stream;
    };
    callbacks.on_stream_info = [&](const icecast::stream_info& info) {
        assert(info.content_type == "audio/mpeg");
        assert(info.icy_metadata_interval == 5);
        saw_info = true;
    };
    callbacks.on_state = [&](const icecast::listener_status& status) {
        states.push_back(status.state);
    };
    callbacks.on_error = [&](const icecast::error&) {
        saw_error = true;
    };

    auto listener_result = context.listen(std::move(config), std::move(callbacks));
    assert(listener_result);
    auto listener = std::move(listener_result).value();

    for (int index = 0; (index < 100) and (not listener.paused()) and (not listener.finished()); ++index) {
        assert(context.poll(std::chrono::milliseconds{50}));
    }
    assert(listener.paused());
    assert(media.size() == 5);
    listener.resume();

    for (int index = 0; (index < 100) and (not listener.finished()); ++index) {
        assert(context.poll(std::chrono::milliseconds{50}));
    }
    assert(listener.finished());
    assert(listener.status().state == icecast::listener_state::stopped);
    assert(listener.status().connection_generation == 1);
    assert(saw_info);
    assert(not saw_error);

    const std::string media_text{reinterpret_cast<const char*>(media.data()), media.size()};
    assert(media_text == "ABCDEFGHIJKLMNO");
    const std::string metadata_text{reinterpret_cast<const char*>(metadata.data()), metadata.size()};
    assert(metadata_text == "StreamTitle='X';");

    http.join();
    assert(http.request.find("GET /stream HTTP/") != std::string::npos);
    assert(http.request.find("Icy-MetaData: 1\r\n") != std::string::npos);
    assert(http.request.find("Accept-Encoding: identity\r\n") != std::string::npos);
    assert(http.request.find("Authorization: Basic bGlzdGVuZXI6c2VjcmV0\r\n") != std::string::npos);

    assert(not states.empty());
    assert(states.front() == icecast::listener_state::connecting);
    assert(std::find(states.begin(), states.end(), icecast::listener_state::streaming) != states.end());
    assert(states.back() == icecast::listener_state::stopped);
}

void test_listener_authentication_error() {
    loopback_server http{"HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"};

    auto context_result = icecast::curl_context::create();
    assert(context_result);
    auto context = std::move(context_result).value();

    icecast::listener_config config;
    config.endpoint.host = "127.0.0.1";
    config.endpoint.port = http.port;
    config.mount.path = "/private";
    config.credentials = icecast::basic_credentials{"listener", "wrong"};
    config.reconnect.enabled = false;

    bool received_media = false;
    icecast::curl_listener_callbacks callbacks;
    callbacks.on_media = [&](std::span<const std::byte>) {
        received_media = true;
        return icecast::stream_action::continue_stream;
    };

    auto listener_result = context.listen(std::move(config), std::move(callbacks));
    assert(listener_result);
    auto listener = std::move(listener_result).value();

    for (int index = 0; (index < 100) and (not listener.finished()); ++index) {
        assert(context.poll(std::chrono::milliseconds{50}));
    }

    assert(listener.finished());
    assert(listener.status().state == icecast::listener_state::failed);
    assert(listener.status().connection_generation == 0);
    assert(not received_media);
    assert(listener.last_error().has_value());
    assert(listener.last_error()->category == icecast::error_category::authentication);
    assert(listener.last_error()->http_status == 401);
}

} // namespace

int main() {
    test_listener_icy_pause_resume_and_stop();
    test_listener_authentication_error();
    return 0;
}
