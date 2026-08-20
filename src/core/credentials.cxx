#include <icecast/core/credentials.hxx>

#include <string_view>

namespace icecast {
namespace {

[[nodiscard]] error invalid_credentials(std::string message) {
    error failure;
    failure.category = error_category::configuration;
    failure.message = std::move(message);
    failure.operation = "validate_basic_credentials";
    return failure;
}

[[nodiscard]] bool contains_forbidden_control(std::string_view value) noexcept {
    for (const unsigned char character : value) {
        if ((character == 0) or (character == '\r') or (character == '\n')) {
            return true;
        }
    }

    return false;
}

} // namespace

result<void> validate_basic_credentials(const basic_credentials& credentials) {
    if (credentials.username.find(':') != std::string::npos) {
        return invalid_credentials("basic-auth username must not contain ':'");
    }

    if (contains_forbidden_control(credentials.username)) {
        return invalid_credentials("basic-auth username contains a forbidden control character");
    }

    if (contains_forbidden_control(credentials.password)) {
        return invalid_credentials("basic-auth password contains a forbidden control character");
    }

    return result<void>::success();
}

std::string format_redacted_credentials(const basic_credentials& credentials) {
    std::string username;
    username.reserve(credentials.username.size());
    for (const unsigned char character : credentials.username) {
        if ((character < 0x20U) or (character == 0x7fU)) {
            username.push_back('?');
        } else {
            username.push_back(static_cast<char>(character));
        }
    }

    return username + ":<redacted>";
}

} // namespace icecast
