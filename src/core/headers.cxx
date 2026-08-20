#include <icecast/core/headers.hxx>

#include <algorithm>
#include <utility>

namespace icecast {
namespace {

[[nodiscard]] char ascii_lower(char character) noexcept {
    if ((character >= 'A') and (character <= 'Z')) {
        return static_cast<char>(character - 'A' + 'a');
    }

    return character;
}

[[nodiscard]] bool valid_header_name_character(char character) noexcept {
    if (((character >= 'a') and (character <= 'z')) or ((character >= 'A') and (character <= 'Z')) or ((character >= '0') and (character <= '9'))) {
        return true;
    }

    switch (character) {
        case '!':
        case '#':
        case '$':
        case '%':
        case '&':
        case '\'':
        case '*':
        case '+':
        case '-':
        case '.':
        case '^':
        case '_':
        case '`':
        case '|':
        case '~':
            return true;
        default:
            return false;
    }
}

[[nodiscard]] error invalid_header(std::string message) {
    error failure;
    failure.category = error_category::configuration;
    failure.message = std::move(message);
    failure.operation = "validate_header_field";
    return failure;
}

} // namespace

bool header_name_equal(std::string_view left, std::string_view right) noexcept {
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

result<void> validate_header_field(const header_field& field) {
    if (field.name.empty()) {
        return invalid_header("HTTP header name must not be empty");
    }

    for (const char character : field.name) {
        if (not valid_header_name_character(character)) {
            return invalid_header("HTTP header name contains a non-token character");
        }
    }

    for (const unsigned char character : field.value) {
        if ((character == 0) or (character == '\r') or (character == '\n')) {
            return invalid_header("HTTP header value contains a forbidden control character");
        }
    }

    return result<void>::success();
}

result<void> headers::append(std::string name, std::string value) {
    header_field field{.name = std::move(name), .value = std::move(value)};
    auto validation = validate_header_field(field);
    if (not validation) {
        return validation.error();
    }

    fields.push_back(std::move(field));
    return result<void>::success();
}

result<void> headers::set(std::string name, std::string value) {
    header_field field{.name = std::move(name), .value = std::move(value)};
    auto validation = validate_header_field(field);
    if (not validation) {
        return validation.error();
    }

    (void)erase(field.name);
    fields.push_back(std::move(field));
    return result<void>::success();
}

std::size_t headers::erase(std::string_view name) noexcept {
    const auto previous_size = fields.size();
    std::erase_if(fields, [name](const header_field& field) {
        return header_name_equal(field.name, name);
    });
    return previous_size - fields.size();
}

bool headers::contains(std::string_view name) const noexcept {
    return first(name).has_value();
}

std::optional<std::string_view> headers::first(std::string_view name) const noexcept {
    for (const auto& field : fields) {
        if (header_name_equal(field.name, name)) {
            return field.value;
        }
    }

    return std::nullopt;
}

std::vector<std::string_view> headers::all(std::string_view name) const {
    std::vector<std::string_view> values;
    for (const auto& field : fields) {
        if (header_name_equal(field.name, name)) {
            values.emplace_back(field.value);
        }
    }

    return values;
}

} // namespace icecast
