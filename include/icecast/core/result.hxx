#pragma once

#include <optional>
#include <utility>
#include <variant>

#include <icecast/core/error.hxx>

namespace icecast {

template <typename value_type>
struct result {
    result(value_type value) : storage(std::move(value)) {}
    result(icecast::error value) : storage(std::move(value)) {}

    [[nodiscard]] static result success(value_type value) {
        return result{std::move(value)};
    }

    [[nodiscard]] static result failure(icecast::error value) {
        return result{std::move(value)};
    }

    [[nodiscard]] bool has_value() const noexcept {
        return std::holds_alternative<value_type>(storage);
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] value_type& value() & {
        return std::get<value_type>(storage);
    }

    [[nodiscard]] const value_type& value() const& {
        return std::get<value_type>(storage);
    }

    [[nodiscard]] value_type&& value() && {
        return std::get<value_type>(std::move(storage));
    }

    [[nodiscard]] icecast::error& error() & {
        return std::get<icecast::error>(storage);
    }

    [[nodiscard]] const icecast::error& error() const& {
        return std::get<icecast::error>(storage);
    }

    [[nodiscard]] icecast::error&& error() && {
        return std::get<icecast::error>(std::move(storage));
    }

    [[nodiscard]] value_type* value_if() noexcept {
        return std::get_if<value_type>(&storage);
    }

    [[nodiscard]] const value_type* value_if() const noexcept {
        return std::get_if<value_type>(&storage);
    }

    [[nodiscard]] icecast::error* error_if() noexcept {
        return std::get_if<icecast::error>(&storage);
    }

    [[nodiscard]] const icecast::error* error_if() const noexcept {
        return std::get_if<icecast::error>(&storage);
    }

    std::variant<value_type, icecast::error> storage;
};

template <>
struct result<void> {
    result() = default;
    result(icecast::error value) : failure_value(std::move(value)) {}

    [[nodiscard]] static result success() {
        return result{};
    }

    [[nodiscard]] static result failure(icecast::error value) {
        return result{std::move(value)};
    }

    [[nodiscard]] bool has_value() const noexcept {
        return not failure_value.has_value();
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value();
    }

    void value() const {
        if (failure_value.has_value()) {
            throw std::bad_variant_access{};
        }
    }

    [[nodiscard]] icecast::error& error() & {
        return failure_value.value();
    }

    [[nodiscard]] const icecast::error& error() const& {
        return failure_value.value();
    }

    [[nodiscard]] icecast::error&& error() && {
        return std::move(failure_value).value();
    }

    [[nodiscard]] icecast::error* error_if() noexcept {
        return failure_value ? &failure_value.value() : nullptr;
    }

    [[nodiscard]] const icecast::error* error_if() const noexcept {
        return failure_value ? &failure_value.value() : nullptr;
    }

    std::optional<icecast::error> failure_value;
};

} // namespace icecast
