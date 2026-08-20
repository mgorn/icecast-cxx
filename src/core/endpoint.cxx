#include <icecast/core/endpoint.hxx>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <utility>

namespace icecast {
namespace {

[[nodiscard]] error invalid_endpoint(std::string operation, std::string message) {
    error failure;
    failure.category = error_category::configuration;
    failure.message = std::move(message);
    failure.operation = std::move(operation);
    return failure;
}

[[nodiscard]] char ascii_lower(char character) noexcept {
    if ((character >= 'A') and (character <= 'Z')) {
        return static_cast<char>(character - 'A' + 'a');
    }

    return character;
}

[[nodiscard]] bool ascii_equal(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index) {
        if (ascii_lower(left[index]) != ascii_lower(right[index])) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool contains_space_or_control(std::string_view value) noexcept {
    for (const unsigned char character : value) {
        if ((character <= 0x20U) or (character == 0x7fU)) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] result<std::uint16_t> parse_port(std::string_view value) {
    if (value.empty()) {
        return invalid_endpoint("parse_server_endpoint", "endpoint port must not be empty");
    }

    unsigned int parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [position, conversion_error] = std::from_chars(begin, end, parsed);
    if ((conversion_error != std::errc{}) or (position != end) or (parsed == 0U) or (parsed > 65535U)) {
        return invalid_endpoint("parse_server_endpoint", "endpoint port must be an integer from 1 to 65535");
    }

    return static_cast<std::uint16_t>(parsed);
}

[[nodiscard]] std::string normalized_base_path(std::string_view path) {
    if (path.empty() or (path == "/")) {
        return {};
    }

    std::string normalized{path};
    while ((normalized.size() > 1U) and (normalized.back() == '/')) {
        normalized.pop_back();
    }

    if (normalized == "/") {
        return {};
    }

    return normalized;
}

[[nodiscard]] std::string formatted_host(std::string_view host) {
    if (host.find(':') != std::string_view::npos) {
        return "[" + std::string{host} + "]";
    }

    return std::string{host};
}

} // namespace

result<void> validate_server_endpoint(const server_endpoint& endpoint) {
    if (endpoint.host.empty()) {
        return invalid_endpoint("validate_server_endpoint", "endpoint host must not be empty");
    }

    if (contains_space_or_control(endpoint.host)) {
        return invalid_endpoint("validate_server_endpoint", "endpoint host contains whitespace or a control character");
    }

    if ((endpoint.host.find('/') != std::string::npos) or (endpoint.host.find('?') != std::string::npos) or (endpoint.host.find('#') != std::string::npos) or (endpoint.host.find('@') != std::string::npos) or (endpoint.host.find('[') != std::string::npos) or (endpoint.host.find(']') != std::string::npos)) {
        return invalid_endpoint("validate_server_endpoint", "endpoint host contains characters that belong outside the host component");
    }

    if (endpoint.port.has_value() and (endpoint.port.value() == 0U)) {
        return invalid_endpoint("validate_server_endpoint", "endpoint port must be greater than zero");
    }

    if (not endpoint.base_path.empty()) {
        if (endpoint.base_path.front() != '/') {
            return invalid_endpoint("validate_server_endpoint", "endpoint base path must begin with '/'");
        }

        if ((endpoint.base_path.find('?') != std::string::npos) or (endpoint.base_path.find('#') != std::string::npos)) {
            return invalid_endpoint("validate_server_endpoint", "endpoint base path must not contain a query or fragment");
        }

        if (contains_space_or_control(endpoint.base_path)) {
            return invalid_endpoint("validate_server_endpoint", "endpoint base path contains whitespace or a control character; encode it before constructing the endpoint");
        }
    }

    return result<void>::success();
}

result<void> validate_mountpoint(const mountpoint& value) {
    if (value.path.empty() or (value.path.front() != '/')) {
        return invalid_endpoint("validate_mountpoint", "mountpoint must be an absolute path beginning with '/'");
    }

    if ((value.path.find('?') != std::string::npos) or (value.path.find('#') != std::string::npos)) {
        return invalid_endpoint("validate_mountpoint", "mountpoint must not contain a query or fragment");
    }

    if (contains_space_or_control(value.path)) {
        return invalid_endpoint("validate_mountpoint", "mountpoint contains whitespace or a control character; encode it before constructing the mountpoint");
    }

    return result<void>::success();
}

result<server_endpoint> parse_server_endpoint(std::string_view value) {
    const auto scheme_end = value.find("://");
    if (scheme_end == std::string_view::npos) {
        return invalid_endpoint("parse_server_endpoint", "endpoint URL must begin with http:// or https://");
    }

    endpoint_scheme scheme;
    const auto scheme_value = value.substr(0, scheme_end);
    if (ascii_equal(scheme_value, "http")) {
        scheme = endpoint_scheme::http;
    } else if (ascii_equal(scheme_value, "https")) {
        scheme = endpoint_scheme::https;
    } else {
        return invalid_endpoint("parse_server_endpoint", "endpoint URL scheme must be http or https");
    }

    const auto authority_start = scheme_end + 3U;
    if (authority_start >= value.size()) {
        return invalid_endpoint("parse_server_endpoint", "endpoint URL is missing a host");
    }

    const auto authority_end = value.find_first_of("/?#", authority_start);
    if ((authority_end != std::string_view::npos) and ((value[authority_end] == '?') or (value[authority_end] == '#'))) {
        return invalid_endpoint("parse_server_endpoint", "endpoint URL must not contain a query or fragment");
    }

    const auto authority = value.substr(authority_start, authority_end == std::string_view::npos ? std::string_view::npos : authority_end - authority_start);
    if (authority.find('@') != std::string_view::npos) {
        return invalid_endpoint("parse_server_endpoint", "credentials must be supplied separately instead of embedding user information in the endpoint URL");
    }

    std::string host;
    std::optional<std::uint16_t> port;

    if (authority.starts_with('[')) {
        const auto bracket_end = authority.find(']');
        if (bracket_end == std::string_view::npos) {
            return invalid_endpoint("parse_server_endpoint", "IPv6 endpoint host is missing a closing ']'");
        }

        host = authority.substr(1, bracket_end - 1U);
        const auto remainder = authority.substr(bracket_end + 1U);
        if (not remainder.empty()) {
            if (not remainder.starts_with(':')) {
                return invalid_endpoint("parse_server_endpoint", "unexpected characters follow the IPv6 endpoint host");
            }

            auto parsed_port = parse_port(remainder.substr(1));
            if (not parsed_port) {
                return parsed_port.error();
            }
            port = parsed_port.value();
        }
    } else {
        const auto first_colon = authority.find(':');
        if ((first_colon != std::string_view::npos) and (authority.find(':', first_colon + 1U) != std::string_view::npos)) {
            return invalid_endpoint("parse_server_endpoint", "IPv6 endpoint hosts must use square brackets");
        }

        if (first_colon == std::string_view::npos) {
            host = authority;
        } else {
            host = authority.substr(0, first_colon);
            auto parsed_port = parse_port(authority.substr(first_colon + 1U));
            if (not parsed_port) {
                return parsed_port.error();
            }
            port = parsed_port.value();
        }
    }

    std::string base_path;
    if (authority_end != std::string_view::npos) {
        base_path = normalized_base_path(value.substr(authority_end));
    }

    server_endpoint endpoint{
        .scheme = scheme,
        .host = std::move(host),
        .port = port,
        .base_path = std::move(base_path),
    };

    auto validation = validate_server_endpoint(endpoint);
    if (not validation) {
        auto failure = validation.error();
        failure.operation = "parse_server_endpoint";
        return failure;
    }

    return endpoint;
}

result<mountpoint> parse_mountpoint(std::string_view value) {
    mountpoint parsed{.path = std::string{value}};
    auto validation = validate_mountpoint(parsed);
    if (not validation) {
        auto failure = validation.error();
        failure.operation = "parse_mountpoint";
        return failure;
    }

    return parsed;
}

std::uint16_t effective_port(const server_endpoint& endpoint) noexcept {
    if (endpoint.port.has_value()) {
        return endpoint.port.value();
    }

    return endpoint.scheme == endpoint_scheme::https ? 443U : 80U;
}

std::string_view to_string(endpoint_scheme scheme) noexcept {
    switch (scheme) {
        case endpoint_scheme::http:
            return "http";
        case endpoint_scheme::https:
            return "https";
    }

    return "http";
}

std::string format_server_endpoint(const server_endpoint& endpoint) {
    std::string formatted{to_string(endpoint.scheme)};
    formatted += "://";
    formatted += formatted_host(endpoint.host);
    if (endpoint.port.has_value()) {
        formatted += ':';
        formatted += std::to_string(endpoint.port.value());
    }
    formatted += normalized_base_path(endpoint.base_path);
    return formatted;
}

result<std::string> resolve_mount_url(const server_endpoint& endpoint, const mountpoint& value) {
    auto endpoint_validation = validate_server_endpoint(endpoint);
    if (not endpoint_validation) {
        auto failure = endpoint_validation.error();
        failure.operation = "resolve_mount_url";
        return failure;
    }

    auto mount_validation = validate_mountpoint(value);
    if (not mount_validation) {
        auto failure = mount_validation.error();
        failure.operation = "resolve_mount_url";
        return failure;
    }

    return format_server_endpoint(endpoint) + value.path;
}

} // namespace icecast
