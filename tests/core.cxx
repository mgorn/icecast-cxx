#include <icecast/core.hxx>

#include <iostream>
#include <string>
#include <utility>

namespace {

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (condition) {
        return;
    }

    std::cerr << "line " << line << ": expected " << expression << '\n';
    ++failures;
}

#define EXPECT(expression) expect(static_cast<bool>(expression), #expression, __LINE__)

void test_result_and_error() {
    icecast::result<int> success{42};
    EXPECT(success);
    EXPECT(success.value() == 42);
    EXPECT(success.error_if() == nullptr);

    icecast::error failure_error;
    failure_error.category = icecast::error_category::timeout;
    failure_error.message = "timed out";
    failure_error.operation = "test";
    failure_error.retryable = true;
    icecast::result<int> failure{std::move(failure_error)};
    EXPECT(not failure);
    EXPECT(failure.error().category == icecast::error_category::timeout);
    EXPECT(failure.error().retryable);
    EXPECT(icecast::to_string(failure.error().category) == "timeout");

    auto void_success = icecast::result<void>::success();
    EXPECT(void_success);
    EXPECT(void_success.error_if() == nullptr);
}

void test_endpoints() {
    auto endpoint = icecast::parse_server_endpoint("https://radio.example.com:8443/icecast/");
    EXPECT(endpoint);
    if (endpoint) {
        EXPECT(endpoint.value().scheme == icecast::endpoint_scheme::https);
        EXPECT(endpoint.value().host == "radio.example.com");
        EXPECT(endpoint.value().port == 8443);
        EXPECT(endpoint.value().base_path == "/icecast");
        EXPECT(icecast::effective_port(endpoint.value()) == 8443);
        EXPECT(icecast::format_server_endpoint(endpoint.value()) == "https://radio.example.com:8443/icecast");

        auto mount = icecast::parse_mountpoint("/live.ogg");
        EXPECT(mount);
        if (mount) {
            auto url = icecast::resolve_mount_url(endpoint.value(), mount.value());
            EXPECT(url);
            if (url) {
                EXPECT(url.value() == "https://radio.example.com:8443/icecast/live.ogg");
            }
        }
    }

    auto ipv6 = icecast::parse_server_endpoint("http://[::1]:8000/");
    EXPECT(ipv6);
    if (ipv6) {
        EXPECT(ipv6.value().host == "::1");
        EXPECT(icecast::format_server_endpoint(ipv6.value()) == "http://[::1]:8000");
    }

    auto default_https = icecast::parse_server_endpoint("https://radio.example.com");
    EXPECT(default_https);
    if (default_https) {
        EXPECT(icecast::effective_port(default_https.value()) == 443);
    }

    EXPECT(not icecast::parse_server_endpoint("radio.example.com:8000"));
    EXPECT(not icecast::parse_server_endpoint("ftp://radio.example.com"));
    EXPECT(not icecast::parse_server_endpoint("http://source:secret@radio.example.com"));
    EXPECT(not icecast::parse_server_endpoint("http://radio.example.com:70000"));
    EXPECT(not icecast::parse_server_endpoint("http://radio.example.com?x=1"));
    EXPECT(not icecast::parse_mountpoint("live.ogg"));
    EXPECT(not icecast::parse_mountpoint("/live.ogg?token=secret"));
}

void test_credentials() {
    icecast::basic_credentials credentials{
        .username = "source",
        .password = "secret",
    };
    EXPECT(icecast::validate_basic_credentials(credentials));
    EXPECT(icecast::format_redacted_credentials(credentials) == "source:<redacted>");
    EXPECT(not icecast::validate_basic_credentials({.username = "bad:user", .password = "secret"}));
    EXPECT(not icecast::validate_basic_credentials({.username = "source", .password = "bad\r\nvalue"}));
    EXPECT(icecast::format_redacted_credentials({.username = "bad\nuser", .password = "secret"}) == "bad?user:<redacted>");
}

void test_headers() {
    icecast::headers values;
    EXPECT(values.append("Icy-MetaInt", "16000"));
    EXPECT(values.append("Set-Cookie", "a=1"));
    EXPECT(values.append("set-cookie", "b=2"));
    EXPECT(values.contains("icy-metaint"));
    EXPECT(values.first("ICY-METAINT").value_or("") == "16000");
    EXPECT(values.all("SET-cookie").size() == 2);

    EXPECT(values.set("Set-Cookie", "c=3"));
    EXPECT(values.all("set-cookie").size() == 1);
    EXPECT(values.first("set-cookie").value_or("") == "c=3");

    EXPECT(not values.set("Bad Header", "value"));
    EXPECT(values.first("set-cookie").value_or("") == "c=3");
    EXPECT(not values.append("Bad Header", "value"));
    EXPECT(not values.append("X-Test", "bad\r\nvalue"));
}

void test_capabilities() {
    icecast::platform_capabilities platform;
    platform.stream_listening.support = icecast::capability_support::supported;
    platform.stream_publishing.support = icecast::capability_support::conditional;
    platform.stream_publishing.detail = "browser request streaming is conditional";
    platform.stream_publishing.requirements = {"streaming_request_body", "cors_permission"};
    platform.admin_operations.support = icecast::capability_support::supported;

    icecast::server_capabilities server;
    server.stream_listening.support = icecast::capability_support::supported;
    server.stream_publishing.support = icecast::capability_support::supported;
    server.stream_publishing.requirements = {"cors_permission"};
    server.admin_operations.support = icecast::capability_support::unsupported;
    server.admin_operations.detail = "admin disabled";

    const auto effective = icecast::combine_capabilities(platform, server);
    EXPECT(effective.stream_listening.support == icecast::capability_support::supported);
    EXPECT(effective.stream_publishing.support == icecast::capability_support::conditional);
    EXPECT(effective.stream_publishing.requirements.size() == 2);
    EXPECT(effective.admin_operations.support == icecast::capability_support::unsupported);
}

} // namespace

int main() {
    test_result_and_error();
    test_endpoints();
    test_credentials();
    test_headers();
    test_capabilities();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return 1;
    }

    return 0;
}
