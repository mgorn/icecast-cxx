#include <icecast/core/error.hxx>

namespace icecast {

std::string_view to_string(error_category category) noexcept {
    switch (category) {
        case error_category::configuration:
            return "configuration";
        case error_category::cancelled:
            return "cancelled";
        case error_category::timeout:
            return "timeout";
        case error_category::name_resolution:
            return "name_resolution";
        case error_category::connection:
            return "connection";
        case error_category::tls:
            return "tls";
        case error_category::authentication:
            return "authentication";
        case error_category::http:
            return "http";
        case error_category::protocol:
            return "protocol";
        case error_category::server_rejected:
            return "server_rejected";
        case error_category::unsupported:
            return "unsupported";
        case error_category::browser_policy:
            return "browser_policy";
        case error_category::backpressure:
            return "backpressure";
        case error_category::internal:
            return "internal";
    }

    return "internal";
}

} // namespace icecast
