#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <curl/curl.h>

#include <icecast/core/endpoint.hxx>
#include <icecast/stream/icy.hxx>
#include <icecast/stream/reconnect.hxx>
#include <icecast/stream/state.hxx>
#include <icecast/transport_curl/listener.hxx>

namespace icecast {

namespace detail {

struct curl_listener_impl {
    listener_status listener_status_value;
    bool paused_value = false;
    bool stop_requested = false;
    bool resume_requested = false;
    std::optional<stream_info> stream_info_value;
    std::optional<error> last_error_value;
};

struct curl_session {
    listener_config config;
    curl_listener_callbacks callbacks;
    curl_listener_options options;
    std::shared_ptr<curl_listener_impl> control;
    CURL* easy = nullptr;
    curl_slist* request_headers = nullptr;
    icy_stream_decoder decoder;
    headers response_headers;
    long response_status = 0;
    std::optional<error> callback_error;
    std::array<char, CURL_ERROR_SIZE> error_buffer{};
    std::array<std::byte, CURL_MAX_WRITE_SIZE> pending{};
    std::size_t pending_offset = 0;
    std::size_t pending_size = 0;
    std::size_t reconnect_attempts = 0;
    std::chrono::steady_clock::time_point reconnect_at{};
    bool active = false;
    bool final_headers_complete = false;
};

struct curl_context_impl {
    CURLM* multi = nullptr;
    std::vector<std::unique_ptr<curl_session>> sessions;
    std::mt19937_64 random{std::random_device{}()};
};

} // namespace detail

namespace {

using clock_type = std::chrono::steady_clock;

[[nodiscard]] error make_error(error_category category, std::string operation, std::string message, bool retryable = false, std::optional<int> http_status = std::nullopt, std::optional<std::int64_t> backend_code = std::nullopt) {
    return {
        .category = category,
        .message = std::move(message),
        .operation = std::move(operation),
        .retryable = retryable,
        .http_status = http_status,
        .backend = "curl",
        .backend_code = backend_code,
    };
}

[[nodiscard]] error callback_exception_error(std::string_view callback) {
    return make_error(error_category::internal, "curl listener callback", std::string{callback} + " callback threw an exception");
}

[[nodiscard]] result<void> validate_options(const curl_listener_options& options) {
    if (options.connect_timeout.count() < 0) {
        return result<void>::failure(make_error(error_category::configuration, "validate curl listener options", "connect timeout cannot be negative"));
    }
    if (options.max_redirects < 0) {
        return result<void>::failure(make_error(error_category::configuration, "validate curl listener options", "maximum redirects cannot be negative"));
    }
    return result<void>::success();
}

[[nodiscard]] error curl_error(CURLcode code, std::string operation, const char* error_buffer = nullptr) {
    error_category category = error_category::connection;
    bool retryable = true;

    switch (code) {
        case CURLE_OPERATION_TIMEDOUT:
            category = error_category::timeout;
            break;
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY:
            category = error_category::name_resolution;
            break;
        case CURLE_LOGIN_DENIED:
            category = error_category::authentication;
            retryable = false;
            break;
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_SSL_CACERT_BADFILE:
            category = error_category::tls;
            retryable = false;
            break;
        case CURLE_SSL_CONNECT_ERROR:
            category = error_category::tls;
            break;
        case CURLE_TOO_MANY_REDIRECTS:
            category = error_category::http;
            retryable = false;
            break;
        case CURLE_UNSUPPORTED_PROTOCOL:
            category = error_category::unsupported;
            retryable = false;
            break;
        case CURLE_ABORTED_BY_CALLBACK:
            category = error_category::cancelled;
            retryable = false;
            break;
        default:
            break;
    }

    std::string message;
    if ((error_buffer != nullptr) and (error_buffer[0] != '\0')) {
        message = error_buffer;
    } else {
        message = curl_easy_strerror(code);
    }
    return make_error(category, std::move(operation), std::move(message), retryable, std::nullopt, static_cast<std::int64_t>(code));
}

[[nodiscard]] error curl_multi_error(CURLMcode code, std::string operation) {
    return make_error(error_category::internal, std::move(operation), curl_multi_strerror(code), false, std::nullopt, static_cast<std::int64_t>(code));
}

[[nodiscard]] error http_error(long status) {
    const auto status_value = static_cast<int>(status);
    if ((status == 401) or (status == 403)) {
        return make_error(error_category::authentication, "start curl listener", "Icecast rejected listener authentication", false, status_value);
    }

    const bool retryable = (status == 404) or (status == 408) or (status == 425) or (status == 429) or (status >= 500);
    return make_error(error_category::server_rejected, "start curl listener", "Icecast returned HTTP status " + std::to_string(status), retryable, status_value);
}

[[nodiscard]] bool curl_runtime_has_https() noexcept {
    const auto* version = curl_version_info(CURLVERSION_NOW);
    if ((version == nullptr) or (version->protocols == nullptr)) {
        return false;
    }
    for (auto protocol = version->protocols; *protocol != nullptr; ++protocol) {
        if (std::string_view{*protocol} == "https") {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string_view trim_http_whitespace(std::string_view value) noexcept {
    while ((not value.empty()) and ((value.front() == ' ') or (value.front() == '\t'))) {
        value.remove_prefix(1);
    }
    while ((not value.empty()) and ((value.back() == ' ') or (value.back() == '\t'))) {
        value.remove_suffix(1);
    }
    return value;
}

[[nodiscard]] bool parse_status_line(std::string_view line, long& status) noexcept {
    if (not line.starts_with("HTTP/")) {
        return false;
    }
    const auto first_space = line.find(' ');
    if (first_space == std::string_view::npos) {
        return false;
    }
    auto code_text = line.substr(first_space + 1);
    const auto second_space = code_text.find(' ');
    if (second_space != std::string_view::npos) {
        code_text = code_text.substr(0, second_space);
    }
    long parsed = 0;
    const auto result = std::from_chars(code_text.data(), code_text.data() + code_text.size(), parsed);
    if ((result.ec != std::errc{}) or (result.ptr != code_text.data() + code_text.size())) {
        return false;
    }
    status = parsed;
    return true;
}

[[nodiscard]] result<std::optional<std::size_t>> parse_icy_interval(const headers& values) {
    const auto all_values = values.all("icy-metaint");
    if (all_values.empty()) {
        return std::optional<std::size_t>{};
    }

    std::optional<std::size_t> parsed_value;
    for (auto text : all_values) {
        text = trim_http_whitespace(text);
        std::size_t value = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if ((text.empty()) or (parsed.ec != std::errc{}) or (parsed.ptr != text.data() + text.size()) or (value == 0)) {
            return result<std::optional<std::size_t>>::failure(make_error(error_category::protocol, "parse Icecast response headers", "invalid icy-metaint response header"));
        }
        if (parsed_value.has_value() and (parsed_value.value() != value)) {
            return result<std::optional<std::size_t>>::failure(make_error(error_category::protocol, "parse Icecast response headers", "conflicting icy-metaint response headers"));
        }
        parsed_value = value;
    }
    return parsed_value;
}

[[nodiscard]] bool notify_state(detail::curl_session& session) noexcept {
    if (not session.callbacks.on_state) {
        return true;
    }
    try {
        session.callbacks.on_state(session.control->listener_status_value);
        return true;
    } catch (...) {
        session.callback_error = callback_exception_error("state");
        return false;
    }
}

void notify_error(detail::curl_session& session, const error& value) noexcept {
    if (not session.callbacks.on_error) {
        return;
    }
    try {
        session.callbacks.on_error(value);
    } catch (...) {
    }
}

[[nodiscard]] bool set_state(detail::curl_session& session, listener_state state) noexcept {
    auto transition = transition_listener(session.control->listener_status_value, state);
    if (not transition) {
        session.callback_error = transition.error();
        return false;
    }
    return notify_state(session);
}

[[nodiscard]] stream_action invoke_media(detail::curl_session& session, std::span<const std::byte> bytes) noexcept {
    try {
        return session.callbacks.on_media(bytes);
    } catch (...) {
        session.callback_error = callback_exception_error("media");
        return stream_action::stop;
    }
}

[[nodiscard]] stream_action invoke_metadata(detail::curl_session& session, const icy_metadata_event_view& event) noexcept {
    if (not session.callbacks.on_metadata) {
        return stream_action::continue_stream;
    }
    try {
        return session.callbacks.on_metadata(event);
    } catch (...) {
        session.callback_error = callback_exception_error("metadata");
        return stream_action::stop;
    }
}

[[nodiscard]] bool finalize_headers(detail::curl_session& session) noexcept {
    if ((session.response_status < 200) or (session.response_status >= 300)) {
        return true;
    }

    auto interval = parse_icy_interval(session.response_headers);
    if (not interval) {
        session.callback_error = interval.error();
        return false;
    }

    stream_info info;
    if (auto value = session.response_headers.first("Content-Type"); value.has_value()) {
        info.content_type = std::string{trim_http_whitespace(value.value())};
    }
    info.response_headers = session.response_headers;
    info.icy_metadata_interval = interval.value();

    reset_icy_stream_decoder(session.decoder, info.icy_metadata_interval.value_or(0));
    session.control->stream_info_value = info;
    session.final_headers_complete = true;
    session.reconnect_attempts = 0;

    if (not set_state(session, listener_state::streaming)) {
        return false;
    }

    if (session.callbacks.on_stream_info) {
        try {
            session.callbacks.on_stream_info(session.control->stream_info_value.value());
        } catch (...) {
            session.callback_error = callback_exception_error("stream info");
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::size_t header_callback(char* data, std::size_t size, std::size_t count, void* userdata) noexcept {
    auto& session = *static_cast<detail::curl_session*>(userdata);
    const auto total = size * count;
    auto line = std::string_view{data, total};
    while ((not line.empty()) and ((line.back() == '\r') or (line.back() == '\n'))) {
        line.remove_suffix(1);
    }

    long status = 0;
    if (parse_status_line(line, status)) {
        session.response_status = status;
        session.response_headers.fields.clear();
        session.final_headers_complete = false;
        return total;
    }

    if (session.final_headers_complete) {
        return total;
    }

    if (line.empty()) {
        if (not finalize_headers(session)) {
            return 0;
        }
        return total;
    }

    const auto colon = line.find(':');
    if (colon == std::string_view::npos) {
        session.callback_error = make_error(error_category::protocol, "parse Icecast response headers", "malformed HTTP response header");
        return 0;
    }

    auto append_result = session.response_headers.append(std::string{line.substr(0, colon)}, std::string{trim_http_whitespace(line.substr(colon + 1))});
    if (not append_result) {
        session.callback_error = make_error(error_category::protocol, "parse Icecast response headers", append_result.error().message);
        return 0;
    }
    return total;
}

[[nodiscard]] stream_consume_result consume_body(detail::curl_session& session, std::span<const std::byte> body) noexcept {
    return consume_icy_stream(
        session.decoder,
        body,
        [&](std::span<const std::byte> media) { return invoke_media(session, media); },
        [&](const icy_metadata_event_view& metadata) { return invoke_metadata(session, metadata); });
}

[[nodiscard]] std::size_t write_callback(char* data, std::size_t size, std::size_t count, void* userdata) noexcept {
    auto& session = *static_cast<detail::curl_session*>(userdata);
    const auto total = size * count;
    if (total == 0) {
        return 0;
    }
    if (session.control->stop_requested) {
        return 0;
    }

    if ((session.response_status < 200) or (session.response_status >= 300) or (not session.final_headers_complete)) {
        return total;
    }

    const auto body = std::span<const std::byte>{reinterpret_cast<const std::byte*>(data), total};
    const auto consumed = consume_body(session, body);

    if (consumed.action == stream_action::continue_stream) {
        return total;
    }
    if (consumed.action == stream_action::stop) {
        session.control->stop_requested = true;
        return 0;
    }

    if (consumed.consumed < total) {
        const auto remaining = total - consumed.consumed;
        if (remaining > session.pending.size()) {
            session.callback_error = make_error(error_category::internal, "pause curl listener", "curl delivered a body chunk larger than the bounded pending buffer");
            return 0;
        }
        std::copy_n(body.begin() + static_cast<std::ptrdiff_t>(consumed.consumed), remaining, session.pending.begin());
        session.pending_offset = 0;
        session.pending_size = remaining;
    }

    session.control->paused_value = true;
    const auto pause_result = curl_easy_pause(session.easy, CURLPAUSE_RECV);
    if (pause_result != CURLE_OK) {
        session.callback_error = curl_error(pause_result, "pause curl listener");
        return 0;
    }
    return total;
}

[[nodiscard]] result<void> append_request_header(detail::curl_session& session, std::string value) {
    auto* next = curl_slist_append(session.request_headers, value.c_str());
    if (next == nullptr) {
        return result<void>::failure(make_error(error_category::internal, "configure curl listener", "failed to allocate curl request headers"));
    }
    session.request_headers = next;
    return result<void>::success();
}

void release_easy(detail::curl_context_impl& context, detail::curl_session& session) noexcept {
    if (session.easy != nullptr) {
        if (session.active) {
            curl_multi_remove_handle(context.multi, session.easy);
        }
        curl_easy_cleanup(session.easy);
        session.easy = nullptr;
    }
    session.active = false;
    if (session.request_headers != nullptr) {
        curl_slist_free_all(session.request_headers);
        session.request_headers = nullptr;
    }
}

[[nodiscard]] result<void> configure_easy(detail::curl_context_impl& context, detail::curl_session& session) {
    session.easy = curl_easy_init();
    if (session.easy == nullptr) {
        return result<void>::failure(make_error(error_category::internal, "create curl listener", "curl_easy_init failed"));
    }

    auto url = resolve_mount_url(session.config.endpoint, session.config.mount);
    if (not url) {
        release_easy(context, session);
        return result<void>::failure(url.error());
    }

    if ((session.config.endpoint.scheme == endpoint_scheme::https) and (not curl_runtime_has_https())) {
        release_easy(context, session);
        return result<void>::failure(make_error(error_category::unsupported, "create curl listener", "the selected libcurl build does not support HTTPS"));
    }

    session.error_buffer.fill('\0');
    const auto setopt = [&](CURLoption option, auto value) -> result<void> {
        const auto code = curl_easy_setopt(session.easy, option, value);
        if (code != CURLE_OK) {
            return result<void>::failure(curl_error(code, "configure curl listener", session.error_buffer.data()));
        }
        return result<void>::success();
    };

    if (auto value = setopt(CURLOPT_ERRORBUFFER, session.error_buffer.data()); not value) { release_easy(context, session); return value; }
    if (auto value = setopt(CURLOPT_URL, url.value().c_str()); not value) { release_easy(context, session); return value; }
    if (auto value = setopt(CURLOPT_HTTPGET, 1L); not value) { release_easy(context, session); return value; }
    if (auto value = setopt(CURLOPT_NOSIGNAL, 1L); not value) { release_easy(context, session); return value; }
    if (auto value = setopt(CURLOPT_TCP_KEEPALIVE, 1L); not value) { release_easy(context, session); return value; }
    if (auto value = setopt(CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(std::min<std::int64_t>(session.options.connect_timeout.count(), std::numeric_limits<long>::max()))); not value) { release_easy(context, session); return value; }
    if (auto value = setopt(CURLOPT_FOLLOWLOCATION, 1L); not value) { release_easy(context, session); return value; }
    if (auto value = setopt(CURLOPT_MAXREDIRS, session.options.max_redirects); not value) { release_easy(context, session); return value; }
    if (auto value = setopt(CURLOPT_UNRESTRICTED_AUTH, 0L); not value) { release_easy(context, session); return value; }
    if (auto value = setopt(CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L); not value) { release_easy(context, session); return value; }
    if (auto value = setopt(CURLOPT_HTTP_CONTENT_DECODING, 0L); not value) { release_easy(context, session); return value; }
    if (auto value = setopt(CURLOPT_SSL_VERIFYPEER, 1L); not value) { release_easy(context, session); return value; }
    if (auto value = setopt(CURLOPT_SSL_VERIFYHOST, 2L); not value) { release_easy(context, session); return value; }
#if LIBCURL_VERSION_NUM >= 0x075500
    if (auto value = setopt(CURLOPT_PROTOCOLS_STR, "http,https"); not value) { release_easy(context, session); return value; }
    const auto* redirect_protocols = session.config.endpoint.scheme == endpoint_scheme::https ? "https" : "http,https";
    if (auto value = setopt(CURLOPT_REDIR_PROTOCOLS_STR, redirect_protocols); not value) { release_easy(context, session); return value; }
#else
    if (auto value = setopt(CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS)); not value) { release_easy(context, session); return value; }
    const auto redirect_protocols = session.config.endpoint.scheme == endpoint_scheme::https ? CURLPROTO_HTTPS : (CURLPROTO_HTTP | CURLPROTO_HTTPS);
    if (auto value = setopt(CURLOPT_REDIR_PROTOCOLS, static_cast<long>(redirect_protocols)); not value) { release_easy(context, session); return value; }
#endif
    if (auto value = setopt(CURLOPT_WRITEFUNCTION, &write_callback); not value) { release_easy(context, session); return value; }
    if (auto value = setopt(CURLOPT_WRITEDATA, &session); not value) { release_easy(context, session); return value; }
    if (auto value = setopt(CURLOPT_HEADERFUNCTION, &header_callback); not value) { release_easy(context, session); return value; }
    if (auto value = setopt(CURLOPT_HEADERDATA, &session); not value) { release_easy(context, session); return value; }
    if (auto value = setopt(CURLOPT_PRIVATE, &session); not value) { release_easy(context, session); return value; }
    if (auto value = setopt(CURLOPT_USERAGENT, "icecast-cxx"); not value) { release_easy(context, session); return value; }

    if (session.config.credentials.has_value()) {
        if (auto value = setopt(CURLOPT_HTTPAUTH, static_cast<long>(CURLAUTH_BASIC)); not value) { release_easy(context, session); return value; }
        if (auto value = setopt(CURLOPT_USERNAME, session.config.credentials->username.c_str()); not value) { release_easy(context, session); return value; }
        if (auto value = setopt(CURLOPT_PASSWORD, session.config.credentials->password.c_str()); not value) { release_easy(context, session); return value; }
    }

    if (auto value = append_request_header(session, "Accept-Encoding: identity"); not value) { release_easy(context, session); return value; }
    if (session.config.request_icy_metadata) {
        if (auto value = append_request_header(session, "Icy-MetaData: 1"); not value) { release_easy(context, session); return value; }
    }
    for (const auto& field : session.config.request_headers.fields) {
        if (auto value = append_request_header(session, field.name + ": " + field.value); not value) { release_easy(context, session); return value; }
    }
    if (auto value = setopt(CURLOPT_HTTPHEADER, session.request_headers); not value) { release_easy(context, session); return value; }

    const auto add_result = curl_multi_add_handle(context.multi, session.easy);
    if (add_result != CURLM_OK) {
        release_easy(context, session);
        return result<void>::failure(curl_multi_error(add_result, "start curl listener"));
    }
    session.active = true;
    return result<void>::success();
}

[[nodiscard]] std::chrono::milliseconds reconnect_delay(detail::curl_context_impl& context, const reconnect_policy& policy, std::size_t attempt_index) {
    const auto base = reconnect_base_delay(policy, attempt_index);
    if ((base.count() == 0) or (policy.jitter == 0.0)) {
        return base;
    }
    std::uniform_real_distribution<double> distribution(1.0 - policy.jitter, 1.0 + policy.jitter);
    const auto jittered = static_cast<long double>(base.count()) * distribution(context.random);
    const auto maximum = static_cast<long double>(policy.max_delay.count());
    const auto clamped = std::clamp(jittered, 0.0L, maximum);
    return std::chrono::milliseconds{static_cast<std::int64_t>(std::llround(clamped))};
}

void stop_session(detail::curl_context_impl& context, detail::curl_session& session) noexcept {
    release_easy(context, session);
    session.pending_offset = 0;
    session.pending_size = 0;
    session.control->paused_value = false;

    const auto current = session.control->listener_status_value.state;
    if ((current != listener_state::stopping) and (current != listener_state::stopped)) {
        if (is_valid_listener_transition(current, listener_state::stopping)) {
            (void)set_state(session, listener_state::stopping);
        }
    }
    if (session.control->listener_status_value.state != listener_state::stopped) {
        (void)set_state(session, listener_state::stopped);
    }
}

void fail_or_reconnect(detail::curl_context_impl& context, detail::curl_session& session, error failure) noexcept {
    release_easy(context, session);
    session.pending_offset = 0;
    session.pending_size = 0;
    session.control->paused_value = false;
    session.control->last_error_value = failure;
    notify_error(session, failure);

    if (failure.retryable and allows_reconnect(session.config.reconnect, session.reconnect_attempts)) {
        if (set_state(session, listener_state::reconnect_wait)) {
            const auto delay = reconnect_delay(context, session.config.reconnect, session.reconnect_attempts);
            ++session.reconnect_attempts;
            session.reconnect_at = clock_type::now() + delay;
        }
        return;
    }
    (void)set_state(session, listener_state::failed);
}

void complete_session(detail::curl_context_impl& context, detail::curl_session& session, CURLcode code) noexcept {
    if (session.callback_error.has_value()) {
        fail_or_reconnect(context, session, session.callback_error.value());
        return;
    }
    if (session.control->stop_requested) {
        stop_session(context, session);
        return;
    }
    if (code != CURLE_OK) {
        fail_or_reconnect(context, session, curl_error(code, "receive Icecast stream", session.error_buffer.data()));
        return;
    }
    if ((session.response_status < 200) or (session.response_status >= 300)) {
        fail_or_reconnect(context, session, http_error(session.response_status));
        return;
    }
    fail_or_reconnect(context, session, make_error(error_category::connection, "receive Icecast stream", "Icecast stream ended", true));
}

void process_completions(detail::curl_context_impl& context) noexcept {
    int remaining = 0;
    while (auto* message = curl_multi_info_read(context.multi, &remaining)) {
        if (message->msg != CURLMSG_DONE) {
            continue;
        }
        detail::curl_session* session = nullptr;
        curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &session);
        if (session != nullptr) {
            complete_session(context, *session, message->data.result);
        }
    }
}

[[nodiscard]] bool resume_session(detail::curl_context_impl& context, detail::curl_session& session) noexcept {
    session.control->resume_requested = false;
    if ((not session.control->paused_value) or (session.easy == nullptr)) {
        return true;
    }

    if (session.pending_size > 0) {
        const auto pending = std::span<const std::byte>{session.pending.data() + session.pending_offset, session.pending_size};
        const auto consumed = consume_body(session, pending);
        session.pending_offset += consumed.consumed;
        session.pending_size -= consumed.consumed;

        if (consumed.action == stream_action::stop) {
            session.control->stop_requested = true;
            stop_session(context, session);
            return false;
        }
        if (consumed.action == stream_action::pause) {
            session.control->paused_value = true;
            return true;
        }
        if (session.pending_size != 0) {
            session.callback_error = make_error(error_category::internal, "resume curl listener", "stream decoder did not consume the complete pending buffer");
            fail_or_reconnect(context, session, session.callback_error.value());
            return false;
        }
        session.pending_offset = 0;
    }

    session.control->paused_value = false;
    const auto code = curl_easy_pause(session.easy, CURLPAUSE_CONT);
    if (code != CURLE_OK) {
        fail_or_reconnect(context, session, curl_error(code, "resume curl listener"));
        return false;
    }
    return true;
}

void apply_commands(detail::curl_context_impl& context) noexcept {
    for (auto& owned : context.sessions) {
        auto& session = *owned;
        if (session.control->stop_requested and (session.control->listener_status_value.state != listener_state::stopped) and (session.control->listener_status_value.state != listener_state::failed)) {
            stop_session(context, session);
            continue;
        }
        if (session.control->resume_requested) {
            (void)resume_session(context, session);
        }
    }
}

void start_due_reconnects(detail::curl_context_impl& context) noexcept {
    const auto now = clock_type::now();
    for (auto& owned : context.sessions) {
        auto& session = *owned;
        if ((session.control->listener_status_value.state != listener_state::reconnect_wait) or (session.reconnect_at > now) or session.control->stop_requested) {
            continue;
        }

        session.response_headers.fields.clear();
        session.response_status = 0;
        session.callback_error.reset();
        session.control->stream_info_value.reset();
        session.final_headers_complete = false;
        reset_icy_stream_decoder(session.decoder, 0);

        if (not set_state(session, listener_state::connecting)) {
            continue;
        }
        auto configured = configure_easy(context, session);
        if (not configured) {
            fail_or_reconnect(context, session, configured.error());
        }
    }
}

[[nodiscard]] std::chrono::milliseconds wait_until_next_reconnect(const detail::curl_context_impl& context, std::chrono::milliseconds requested) noexcept {
    auto result = requested;
    const auto now = clock_type::now();
    for (const auto& owned : context.sessions) {
        const auto& session = *owned;
        if (session.control->listener_status_value.state != listener_state::reconnect_wait) {
            continue;
        }
        if (session.reconnect_at <= now) {
            return std::chrono::milliseconds::zero();
        }
        const auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(session.reconnect_at - now);
        result = std::min(result, delay);
    }
    return result;
}

[[nodiscard]] bool has_active_easy(const detail::curl_context_impl& context) noexcept {
    return std::any_of(context.sessions.begin(), context.sessions.end(), [](const auto& session) { return session->active; });
}

} // namespace

curl_listener::curl_listener(std::shared_ptr<detail::curl_listener_impl> implementation) noexcept : implementation_(std::move(implementation)) {}

curl_listener::curl_listener(curl_listener&& other) noexcept = default;
curl_listener& curl_listener::operator=(curl_listener&& other) noexcept {
    if (this != &other) {
        stop();
        implementation_ = std::move(other.implementation_);
    }
    return *this;
}

curl_listener::~curl_listener() {
    stop();
}

listener_status curl_listener::status() const noexcept {
    return implementation_ ? implementation_->listener_status_value : listener_status{.state = listener_state::stopped};
}

bool curl_listener::paused() const noexcept {
    return implementation_ and implementation_->paused_value;
}

bool curl_listener::finished() const noexcept {
    const auto state = status().state;
    return (state == listener_state::stopped) or (state == listener_state::failed);
}

std::optional<stream_info> curl_listener::info() const {
    return implementation_ ? implementation_->stream_info_value : std::nullopt;
}

std::optional<error> curl_listener::last_error() const {
    return implementation_ ? implementation_->last_error_value : std::nullopt;
}

void curl_listener::resume() noexcept {
    if (implementation_) {
        implementation_->resume_requested = true;
    }
}

void curl_listener::stop() noexcept {
    if (implementation_) {
        implementation_->stop_requested = true;
    }
}

curl_context::curl_context(std::unique_ptr<detail::curl_context_impl> implementation) noexcept : implementation_(std::move(implementation)) {}

curl_context::curl_context(curl_context&& other) noexcept = default;
curl_context& curl_context::operator=(curl_context&& other) noexcept {
    if (this != &other) {
        if (implementation_) {
            for (auto& session : implementation_->sessions) {
                release_easy(*implementation_, *session);
                session->control->listener_status_value.state = listener_state::stopped;
                session->control->paused_value = false;
            }
            if (implementation_->multi != nullptr) {
                curl_multi_cleanup(implementation_->multi);
            }
            curl_global_cleanup();
        }
        implementation_ = std::move(other.implementation_);
    }
    return *this;
}

curl_context::~curl_context() {
    if (not implementation_) {
        return;
    }
    for (auto& session : implementation_->sessions) {
        release_easy(*implementation_, *session);
        session->control->listener_status_value.state = listener_state::stopped;
        session->control->paused_value = false;
    }
    if (implementation_->multi != nullptr) {
        curl_multi_cleanup(implementation_->multi);
        implementation_->multi = nullptr;
    }
    curl_global_cleanup();
}

result<curl_context> curl_context::create() {
    const auto global_result = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (global_result != CURLE_OK) {
        return result<curl_context>::failure(curl_error(global_result, "initialize libcurl"));
    }

    auto implementation = std::make_unique<detail::curl_context_impl>();
    implementation->multi = curl_multi_init();
    if (implementation->multi == nullptr) {
        curl_global_cleanup();
        return result<curl_context>::failure(make_error(error_category::internal, "initialize libcurl multi context", "curl_multi_init failed"));
    }
    return curl_context{std::move(implementation)};
}

result<curl_listener> curl_context::listen(listener_config config, curl_listener_callbacks callbacks, curl_listener_options options) {
    if (not implementation_) {
        return result<curl_listener>::failure(make_error(error_category::internal, "create curl listener", "curl context is not initialized"));
    }
    if (auto value = validate_listener_config(config); not value) {
        return result<curl_listener>::failure(value.error());
    }
    if (auto value = validate_options(options); not value) {
        return result<curl_listener>::failure(value.error());
    }
    if (not callbacks.on_media) {
        return result<curl_listener>::failure(make_error(error_category::configuration, "create curl listener", "a media callback is required"));
    }

    auto session = std::make_unique<detail::curl_session>();
    session->config = std::move(config);
    session->callbacks = std::move(callbacks);
    session->options = options;
    session->control = std::make_shared<detail::curl_listener_impl>();

    if (not set_state(*session, listener_state::connecting)) {
        return result<curl_listener>::failure(session->callback_error.value_or(make_error(error_category::internal, "create curl listener", "failed to enter connecting state")));
    }

    auto configured = configure_easy(*implementation_, *session);
    if (not configured) {
        return result<curl_listener>::failure(configured.error());
    }

    auto control = session->control;
    implementation_->sessions.push_back(std::move(session));
    return curl_listener{std::move(control)};
}

result<void> curl_context::poll(std::chrono::milliseconds max_wait) {
    if (not implementation_) {
        return result<void>::failure(make_error(error_category::internal, "poll curl context", "curl context is not initialized"));
    }
    if (max_wait.count() < 0) {
        return result<void>::failure(make_error(error_category::configuration, "poll curl context", "poll timeout cannot be negative"));
    }

    apply_commands(*implementation_);
    start_due_reconnects(*implementation_);

    int running = 0;
    auto code = curl_multi_perform(implementation_->multi, &running);
    if (code != CURLM_OK) {
        return result<void>::failure(curl_multi_error(code, "perform curl transfers"));
    }
    process_completions(*implementation_);

    const auto wait = wait_until_next_reconnect(*implementation_, max_wait);
    if (wait.count() > 0) {
        if (has_active_easy(*implementation_)) {
            int descriptors = 0;
            const auto timeout = static_cast<int>(std::min<std::int64_t>(wait.count(), std::numeric_limits<int>::max()));
            code = curl_multi_poll(implementation_->multi, nullptr, 0, timeout, &descriptors);
            if (code != CURLM_OK) {
                return result<void>::failure(curl_multi_error(code, "wait for curl transfers"));
            }
        } else {
            std::this_thread::sleep_for(wait);
        }
    }

    code = curl_multi_perform(implementation_->multi, &running);
    if (code != CURLM_OK) {
        return result<void>::failure(curl_multi_error(code, "perform curl transfers"));
    }
    process_completions(*implementation_);
    start_due_reconnects(*implementation_);
    return result<void>::success();
}

std::size_t curl_context::active_listener_count() const noexcept {
    if (not implementation_) {
        return 0;
    }
    return static_cast<std::size_t>(std::count_if(implementation_->sessions.begin(), implementation_->sessions.end(), [](const auto& session) {
        const auto state = session->control->listener_status_value.state;
        return (state != listener_state::stopped) and (state != listener_state::failed);
    }));
}

void curl_context::request_stop_all() noexcept {
    if (not implementation_) {
        return;
    }
    for (auto& session : implementation_->sessions) {
        session->control->stop_requested = true;
    }
}

} // namespace icecast
