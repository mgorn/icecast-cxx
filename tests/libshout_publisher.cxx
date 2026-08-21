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

#include <icecast/publish_libshout.hxx>

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

void send_all(socket_handle socket, std::string_view value) {
    std::size_t offset = 0;
    while (offset < value.size()) {
        const auto count = send_some(socket, value.substr(offset));
        assert(count != 0);
        offset += count;
    }
}

struct source_server {
    socket_runtime runtime;
    socket_handle listening_socket = invalid_socket_handle;
    std::uint16_t port = 0;
    std::size_t expected_body_bytes = 0;
    std::thread worker;
    std::vector<std::string> requests;
    std::string body;

    explicit source_server(std::size_t expected_body_bytes_value) : expected_body_bytes(expected_body_bytes_value) {
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
        assert(::listen(listening_socket, 4) == 0);

        worker = std::thread([this] {
            for (int connection_index = 0; connection_index < 4; ++connection_index) {
                const auto client = accept(listening_socket, nullptr, nullptr);
                assert(socket_error(client) == 0);

                std::array<char, 4096> buffer{};
                std::string request;
                while (request.find("\r\n\r\n") == std::string::npos) {
                    const auto count = receive_some(client, buffer);
                    if (count == 0) {
                        break;
                    }
                    request.append(buffer.data(), count);
                }
                requests.push_back(request);

                const auto authenticated = request.find("Authorization: Basic ") != std::string::npos;
                if (not authenticated) {
                    send_all(client, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    shutdown_socket(client);
                    close_socket(client);
                    continue;
                }

                send_all(client, "HTTP/1.0 200 OK\r\n\r\n");
                while (body.size() < expected_body_bytes) {
                    const auto count = receive_some(client, buffer);
                    if (count == 0) {
                        break;
                    }
                    body.append(buffer.data(), count);
                }
                shutdown_socket(client);
                close_socket(client);
                break;
            }
        });
    }

    source_server(const source_server&) = delete;
    source_server& operator=(const source_server&) = delete;

    ~source_server() {
        shutdown_socket(listening_socket);
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

std::vector<std::byte> bytes(std::string_view value) {
    std::vector<std::byte> output;
    output.reserve(value.size());
    for (const unsigned char character : value) {
        output.push_back(static_cast<std::byte>(character));
    }
    return output;
}

void test_publisher_backpressure_and_finish() {
    const auto payload = bytes("ABCDEFGHIJKL");
    source_server server{payload.size()};

    auto context_result = icecast::libshout_context::create();
    assert(context_result);
    auto context = std::move(context_result).value();

    icecast::publisher_config config;
    config.endpoint.host = "127.0.0.1";
    config.endpoint.port = server.port;
    config.endpoint.base_path = "/radio";
    config.mount.path = "/live.mp3";
    config.credentials = {"source", "secret"};
    config.content_type = "audio/mpeg";
    config.metadata.name = "Publisher test";
    config.max_buffered_bytes = 8;

    std::vector<icecast::publisher_state> states;
    std::vector<icecast::error> errors;
    icecast::libshout_publisher_callbacks callbacks;
    callbacks.on_state = [&](icecast::publisher_state state) {
        states.push_back(state);
    };
    callbacks.on_error = [&](const icecast::error& error) {
        errors.push_back(error);
    };

    auto publisher_result = context.publish(std::move(config), std::move(callbacks));
    assert(publisher_result);
    auto publisher = std::move(publisher_result).value();

    auto first_write = publisher.write(payload);
    assert(first_write);
    assert(first_write.value().accepted == 8);
    assert(first_write.value().status == icecast::publisher_write_status::would_block);

    for (int index = 0; (index < 400) and (publisher.state() != icecast::publisher_state::publishing); ++index) {
        assert(context.poll());
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    assert(publisher.state() == icecast::publisher_state::publishing);

    for (int index = 0; (index < 400) and (publisher.buffered_bytes() != 0); ++index) {
        assert(context.poll());
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    assert(publisher.buffered_bytes() == 0);

    auto second_write = publisher.write(std::span<const std::byte>{payload}.subspan(first_write.value().accepted));
    assert(second_write);
    assert(second_write.value().accepted == 4);
    assert(second_write.value().status == icecast::publisher_write_status::accepted);
    assert(publisher.finish());

    for (int index = 0; (index < 400) and (not publisher.finished()); ++index) {
        assert(context.poll());
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    assert(publisher.state() == icecast::publisher_state::stopped);
    assert(context.active_publisher_count() == 0);
    assert(errors.empty());

    server.join();
    assert(server.body == "ABCDEFGHIJKL");

    bool saw_authenticated_source = false;
    for (const auto& request : server.requests) {
        if (request.find("Authorization: Basic c291cmNlOnNlY3JldA==") != std::string::npos) {
            saw_authenticated_source = true;
            assert(request.find("SOURCE /radio/live.mp3 HTTP/") != std::string::npos);
            assert(request.find("Content-Type: audio/mpeg\r\n") != std::string::npos);
            assert(request.find("ice-name: Publisher test\r\n") != std::string::npos);
        }
    }
    assert(saw_authenticated_source);
    assert(std::find(states.begin(), states.end(), icecast::publisher_state::publishing) != states.end());
    assert(states.back() == icecast::publisher_state::stopped);
}

void test_unsupported_content_type() {
    auto context_result = icecast::libshout_context::create();
    assert(context_result);
    auto context = std::move(context_result).value();

    icecast::publisher_config config;
    config.endpoint.host = "127.0.0.1";
    config.endpoint.port = 1;
    config.mount.path = "/live.aac";
    config.credentials = {"source", "secret"};
    config.content_type = "audio/aac";

    auto publisher = context.publish(std::move(config));
    assert(not publisher);
    assert(publisher.error().category == icecast::error_category::unsupported);
}

void test_empty_source_password_is_rejected() {
    auto context_result = icecast::libshout_context::create();
    assert(context_result);
    auto context = std::move(context_result).value();

    icecast::publisher_config config;
    config.endpoint.host = "127.0.0.1";
    config.endpoint.port = 1;
    config.mount.path = "/live.mp3";
    config.content_type = "audio/mpeg";

    auto publisher = context.publish(std::move(config));
    assert(not publisher);
    assert(publisher.error().category == icecast::error_category::configuration);
}

void test_arbitrary_headers_are_explicitly_unsupported() {
    auto context_result = icecast::libshout_context::create();
    assert(context_result);
    auto context = std::move(context_result).value();

    icecast::publisher_config config;
    config.endpoint.host = "127.0.0.1";
    config.endpoint.port = 1;
    config.mount.path = "/live.mp3";
    config.credentials = {"source", "secret"};
    config.content_type = "audio/mpeg";
    config.request_headers.fields.push_back({"X-Test", "value"});

    auto publisher = context.publish(std::move(config));
    assert(not publisher);
    assert(publisher.error().category == icecast::error_category::unsupported);
}

} // namespace

int main() {
    test_publisher_backpressure_and_finish();
    test_unsupported_content_type();
    test_empty_source_password_is_rejected();
    test_arbitrary_headers_are_explicitly_unsupported();
    return 0;
}
