#include <array>
#include <span>
#include <string_view>
#include <utility>

#include <icecast/stream/models.hxx>

namespace icecast {

namespace {

[[nodiscard]] error config_error(std::string operation, std::string message) {
    return {
        .category = error_category::configuration,
        .message = std::move(message),
        .operation = std::move(operation),
        .retryable = false,
        .http_status = std::nullopt,
        .backend = {},
        .backend_code = std::nullopt,
    };
}

[[nodiscard]] result<void> validate_request_headers(const headers& values, std::span<const std::string_view> managed_names, std::string_view operation) {
    for (const auto& field : values.fields) {
        auto field_result = validate_header_field(field);
        if (not field_result) {
            return field_result;
        }
        for (const auto name : managed_names) {
            if (header_name_equal(field.name, name)) {
                return result<void>::failure(config_error(std::string{operation}, "request header '" + field.name + "' is managed by icecast-cxx"));
            }
        }
    }
    return result<void>::success();
}

} // namespace

result<void> validate_listener_config(const listener_config& config) {
    if (auto value = validate_server_endpoint(config.endpoint); not value) {
        return value;
    }
    if (auto value = validate_mountpoint(config.mount); not value) {
        return value;
    }
    if (config.credentials.has_value()) {
        if (auto value = validate_basic_credentials(config.credentials.value()); not value) {
            return value;
        }
    }
    if (auto value = validate_reconnect_policy(config.reconnect); not value) {
        return value;
    }

    constexpr std::array managed_headers{
        std::string_view{"Host"},
        std::string_view{"Authorization"},
        std::string_view{"Icy-MetaData"},
    };
    return validate_request_headers(config.request_headers, managed_headers, "validate listener configuration");
}

result<void> validate_publisher_config(const publisher_config& config) {
    if (auto value = validate_server_endpoint(config.endpoint); not value) {
        return value;
    }
    if (auto value = validate_mountpoint(config.mount); not value) {
        return value;
    }
    if (auto value = validate_basic_credentials(config.credentials); not value) {
        return value;
    }
    if (config.content_type.empty()) {
        return result<void>::failure(config_error("validate publisher configuration", "publisher content type cannot be empty"));
    }
    const header_field content_type_header{"Content-Type", config.content_type};
    if (auto value = validate_header_field(content_type_header); not value) {
        return result<void>::failure(config_error("validate publisher configuration", "publisher content type is not a valid HTTP header value"));
    }
    if (config.max_buffered_bytes == 0) {
        return result<void>::failure(config_error("validate publisher configuration", "publisher maximum buffered bytes must be greater than zero"));
    }

    constexpr std::array managed_headers{
        std::string_view{"Host"},
        std::string_view{"Authorization"},
        std::string_view{"Content-Type"},
        std::string_view{"Content-Length"},
        std::string_view{"Transfer-Encoding"},
        std::string_view{"Ice-Public"},
        std::string_view{"Ice-Name"},
        std::string_view{"Ice-Description"},
        std::string_view{"Ice-Genre"},
        std::string_view{"Ice-Url"},
    };
    return validate_request_headers(config.request_headers, managed_headers, "validate publisher configuration");
}

} // namespace icecast
