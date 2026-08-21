#include <icecast/publish_libshout/publisher.hxx>

#include <icecast/core/endpoint.hxx>

#include <shout/shout.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace icecast {

namespace detail {

struct libshout_publisher_impl {
    publisher_state m_state = publisher_state::idle;
    bool m_finish_requested = false;
    bool m_abort_requested = false;
    std::size_t m_queued_bytes = 0;
    std::size_t m_backend_buffered_bytes = 0;
    std::size_t m_max_buffered_bytes = 0;
    std::deque<std::vector<std::byte>> m_queue;
    std::size_t m_front_offset = 0;
    std::optional<error> m_last_error;
};

struct libshout_session {
    std::shared_ptr<libshout_publisher_impl> m_control;
    publisher_config m_config;
    libshout_publisher_callbacks m_callbacks;
    libshout_publisher_options m_options;
    shout_t* mp_shout = nullptr;
    std::chrono::steady_clock::time_point m_connect_started;
};

struct libshout_context_impl {
    std::vector<std::unique_ptr<libshout_session>> m_sessions;
};

} // namespace detail

namespace {

std::mutex g_runtime_mutex;
std::size_t g_runtime_references = 0;

void acquire_runtime() {
    const std::scoped_lock lock{g_runtime_mutex};
    if (g_runtime_references == 0) {
        shout_init();
    }
    ++g_runtime_references;
}

void release_runtime() noexcept {
    const std::scoped_lock lock{g_runtime_mutex};
    if (g_runtime_references == 0) {
        return;
    }
    --g_runtime_references;
    if (g_runtime_references == 0) {
        shout_shutdown();
    }
}

[[nodiscard]] error make_error(error_category category, std::string operation, std::string message, bool retryable = false, std::optional<std::int64_t> backend_code = std::nullopt) {
    return {
        .category = category,
        .message = std::move(message),
        .operation = std::move(operation),
        .retryable = retryable,
        .http_status = std::nullopt,
        .backend = "libshout",
        .backend_code = backend_code,
    };
}

[[nodiscard]] error libshout_error(shout_t* shout, int code, std::string operation) {
    error_category category = error_category::connection;
    bool retryable = false;

    switch (code) {
        case SHOUTERR_NOLOGIN:
            category = error_category::authentication;
            break;
        case SHOUTERR_NOTLS:
        case SHOUTERR_TLSBADCERT:
            category = error_category::tls;
            break;
        case SHOUTERR_UNSUPPORTED:
            category = error_category::unsupported;
            break;
        case SHOUTERR_INSANE:
            category = error_category::configuration;
            break;
        case SHOUTERR_MALLOC:
            category = error_category::internal;
            break;
        case SHOUTERR_NOCONNECT:
        case SHOUTERR_SOCKET:
        case SHOUTERR_UNCONNECTED:
            category = error_category::connection;
            retryable = true;
            break;
        case SHOUTERR_METADATA:
            category = error_category::protocol;
            break;
        default:
            category = error_category::connection;
            retryable = true;
            break;
    }

    const char* detail = shout ? shout_get_error(shout) : nullptr;
    std::string message = detail ? detail : "libshout operation failed";
    return make_error(category, std::move(operation), std::move(message), retryable, code);
}

[[nodiscard]] bool is_progress_status(int code) noexcept {
    return (code == SHOUTERR_BUSY) or (code == SHOUTERR_RETRY);
}

[[nodiscard]] bool is_terminal_state(publisher_state state) noexcept {
    return (state == publisher_state::stopped) or (state == publisher_state::interrupted) or (state == publisher_state::failed);
}

[[nodiscard]] std::optional<error> invoke_state_callback(detail::libshout_session& session, publisher_state state) noexcept {
    if (not session.m_callbacks.on_state) {
        return std::nullopt;
    }
    try {
        session.m_callbacks.on_state(state);
        return std::nullopt;
    } catch (const std::exception& exception) {
        return make_error(error_category::internal, "run libshout publisher state callback", exception.what());
    } catch (...) {
        return make_error(error_category::internal, "run libshout publisher state callback", "publisher state callback threw a non-standard exception");
    }
}

void invoke_error_callback(detail::libshout_session& session, const error& value) noexcept {
    if (not session.m_callbacks.on_error) {
        return;
    }
    try {
        session.m_callbacks.on_error(value);
    } catch (...) {
        // The publisher is already terminal and the original failure remains primary.
    }
}

void close_handle(detail::libshout_session& session) noexcept {
    if (session.mp_shout) {
        (void)shout_close(session.mp_shout);
        shout_free(session.mp_shout);
        session.mp_shout = nullptr;
    }
    session.m_control->m_backend_buffered_bytes = 0;
}

void clear_buffer(detail::libshout_session& session) noexcept {
    session.m_control->m_queue.clear();
    session.m_control->m_queued_bytes = 0;
    session.m_control->m_backend_buffered_bytes = 0;
    session.m_control->m_front_offset = 0;
}

void stop_session(detail::libshout_session& session) noexcept {
    if (not is_terminal_state(session.m_control->m_state)) {
        session.m_control->m_state = publisher_state::stopping;
        if (auto callback_error = invoke_state_callback(session, publisher_state::stopping); callback_error.has_value()) {
            session.m_control->m_last_error = std::move(callback_error).value();
        }
    }
    close_handle(session);
    clear_buffer(session);
    if (session.m_control->m_state != publisher_state::stopped) {
        session.m_control->m_state = publisher_state::stopped;
        if (auto callback_error = invoke_state_callback(session, publisher_state::stopped); callback_error.has_value()) {
            session.m_control->m_last_error = std::move(callback_error).value();
        }
    }
}

void fail_session(detail::libshout_session& session, error failure) noexcept {
    const auto was_publishing = session.m_control->m_state == publisher_state::publishing;
    const auto backend_interruption = (failure.category == error_category::connection) or (failure.category == error_category::timeout) or
        (failure.category == error_category::tls) or (failure.category == error_category::protocol) or
        (failure.category == error_category::server_rejected);
    const auto terminal_state = (was_publishing and backend_interruption) ? publisher_state::interrupted : publisher_state::failed;

    close_handle(session);
    clear_buffer(session);
    session.m_control->m_last_error = failure;
    session.m_control->m_state = terminal_state;
    invoke_error_callback(session, failure);
    (void)invoke_state_callback(session, terminal_state);
}

[[nodiscard]] std::string source_mount(const publisher_config& config) {
    std::string base = config.endpoint.base_path;
    while ((base.size() > 1) and (base.back() == '/')) {
        base.pop_back();
    }
    if (base.empty() or (base == "/")) {
        return config.mount.path;
    }
    if (config.mount.path == "/") {
        return base + "/";
    }
    return base + config.mount.path;
}

struct content_format {
    unsigned int m_format = 0;
    unsigned int m_usage = 0;
};

[[nodiscard]] result<content_format> map_content_type(std::string_view content_type) {
    if (content_type == "audio/mpeg") {
        return content_format{SHOUT_FORMAT_MP3, SHOUT_USAGE_AUDIO};
    }
    if (content_type == "audio/ogg") {
        return content_format{SHOUT_FORMAT_OGG, SHOUT_USAGE_AUDIO};
    }
    if (content_type == "video/ogg") {
        return content_format{SHOUT_FORMAT_OGG, SHOUT_USAGE_AUDIO | SHOUT_USAGE_VISUAL};
    }
    if (content_type == "application/ogg") {
        return content_format{SHOUT_FORMAT_OGG, SHOUT_USAGE_UNKNOWN};
    }
    if (content_type == "audio/webm") {
        return content_format{SHOUT_FORMAT_WEBM, SHOUT_USAGE_AUDIO};
    }
    if (content_type == "video/webm") {
        return content_format{SHOUT_FORMAT_WEBM, SHOUT_USAGE_AUDIO | SHOUT_USAGE_VISUAL};
    }
    if (content_type == "audio/x-matroska") {
        return content_format{SHOUT_FORMAT_MATROSKA, SHOUT_USAGE_AUDIO};
    }
    if (content_type == "video/x-matroska") {
        return content_format{SHOUT_FORMAT_MATROSKA, SHOUT_USAGE_AUDIO | SHOUT_USAGE_VISUAL};
    }
    return result<content_format>::failure(make_error(error_category::unsupported, "configure libshout publisher", "content type is not representable by libshout 2.4.x"));
}

[[nodiscard]] result<void> check_option(int code, shout_t* shout, std::string operation) {
    if (code == SHOUTERR_SUCCESS) {
        return result<void>::success();
    }
    return result<void>::failure(libshout_error(shout, code, std::move(operation)));
}

[[nodiscard]] result<void> configure_shout(detail::libshout_session& session) {
    auto format = map_content_type(session.m_config.content_type);
    if (not format) {
        return result<void>::failure(std::move(format).error());
    }
    if (not session.m_config.request_headers.fields.empty()) {
        return result<void>::failure(make_error(error_category::unsupported, "configure libshout publisher", "libshout does not expose arbitrary source request headers"));
    }

    session.mp_shout = shout_new();
    if (not session.mp_shout) {
        return result<void>::failure(make_error(error_category::internal, "create libshout publisher", "libshout could not allocate a source handle"));
    }

    const auto mount = source_mount(session.m_config);
    if (auto value = check_option(shout_set_host(session.mp_shout, session.m_config.endpoint.host.c_str()), session.mp_shout, "set libshout host"); not value) { return value; }
    if (auto value = check_option(shout_set_port(session.mp_shout, effective_port(session.m_config.endpoint)), session.mp_shout, "set libshout port"); not value) { return value; }
    if (auto value = check_option(shout_set_user(session.mp_shout, session.m_config.credentials.username.c_str()), session.mp_shout, "set libshout username"); not value) { return value; }
    if (auto value = check_option(shout_set_password(session.mp_shout, session.m_config.credentials.password.c_str()), session.mp_shout, "set libshout password"); not value) { return value; }
    if (auto value = check_option(shout_set_mount(session.mp_shout, mount.c_str()), session.mp_shout, "set libshout mount"); not value) { return value; }
    if (auto value = check_option(shout_set_public(session.mp_shout, session.m_config.public_stream ? 1U : 0U), session.mp_shout, "set libshout public flag"); not value) { return value; }
    if (auto value = check_option(shout_set_protocol(session.mp_shout, SHOUT_PROTOCOL_HTTP), session.mp_shout, "set libshout protocol"); not value) { return value; }
    if (auto value = check_option(shout_set_nonblocking(session.mp_shout, 1U), session.mp_shout, "enable libshout nonblocking I/O"); not value) { return value; }
    if (session.m_config.endpoint.scheme == endpoint_scheme::https) {
        if (auto value = check_option(shout_set_tls(session.mp_shout, SHOUT_TLS_RFC2818), session.mp_shout, "enable libshout HTTPS"); not value) { return value; }
    } else {
        const auto tls_result = shout_set_tls(session.mp_shout, SHOUT_TLS_DISABLED);
        if ((tls_result != SHOUTERR_SUCCESS) and (tls_result != SHOUTERR_UNSUPPORTED)) {
            return result<void>::failure(libshout_error(session.mp_shout, tls_result, "disable libshout TLS"));
        }
    }
    if (auto value = check_option(shout_set_content_format(session.mp_shout, format.value().m_format, format.value().m_usage, nullptr), session.mp_shout, "set libshout content format"); not value) { return value; }

    const auto set_meta = [&](const char* name, const std::optional<std::string>& value) -> result<void> {
        if (not value.has_value()) {
            return result<void>::success();
        }
        return check_option(shout_set_meta(session.mp_shout, name, value->c_str()), session.mp_shout, "set libshout stream metadata");
    };
    if (auto value = set_meta(SHOUT_META_NAME, session.m_config.metadata.name); not value) { return value; }
    if (auto value = set_meta(SHOUT_META_DESCRIPTION, session.m_config.metadata.description); not value) { return value; }
    if (auto value = set_meta(SHOUT_META_GENRE, session.m_config.metadata.genre); not value) { return value; }
    if (auto value = set_meta(SHOUT_META_URL, session.m_config.metadata.url); not value) { return value; }

    return result<void>::success();
}

[[nodiscard]] bool refresh_backend_buffer(detail::libshout_session& session) {
    const auto length = shout_queuelen(session.mp_shout);
    if (length < 0) {
        fail_session(session, libshout_error(session.mp_shout, shout_get_errno(session.mp_shout), "query libshout send queue"));
        return false;
    }
    session.m_control->m_backend_buffered_bytes = static_cast<std::size_t>(length);
    return true;
}

void drain_buffer(detail::libshout_session& session) {
    if (session.m_control->m_queue.empty()) {
        return;
    }

    while (not session.m_control->m_queue.empty()) {
        auto& front = session.m_control->m_queue.front();
        const auto remaining = front.size() - session.m_control->m_front_offset;
        const auto count = std::min(remaining, session.m_options.send_chunk_bytes);
        const auto* data = reinterpret_cast<const unsigned char*>(front.data() + static_cast<std::ptrdiff_t>(session.m_control->m_front_offset));
        const auto sent = shout_send_raw(session.mp_shout, data, count);

        if (sent > 0) {
            const auto consumed = static_cast<std::size_t>(sent);
            session.m_control->m_front_offset += consumed;
            session.m_control->m_queued_bytes -= consumed;
            if (session.m_control->m_front_offset == front.size()) {
                session.m_control->m_queue.pop_front();
                session.m_control->m_front_offset = 0;
            }
            if (not refresh_backend_buffer(session)) {
                return;
            }
            continue;
        }

        if (sent == 0) {
            return;
        }

        const auto code = shout_get_errno(session.mp_shout);
        if (is_progress_status(code)) {
            return;
        }
        fail_session(session, libshout_error(session.mp_shout, code, "send encoded stream bytes"));
        return;
    }
}

void poll_session(detail::libshout_session& session) {
    if (session.m_control->m_abort_requested) {
        stop_session(session);
        return;
    }

    if (session.m_control->m_state == publisher_state::connecting) {
        if ((std::chrono::steady_clock::now() - session.m_connect_started) > session.m_options.connect_timeout) {
            fail_session(session, make_error(error_category::timeout, "connect libshout publisher", "timed out connecting to Icecast", true));
            return;
        }
        const auto connected = shout_get_connected(session.mp_shout);
        if (connected == SHOUTERR_CONNECTED) {
            session.m_control->m_state = publisher_state::publishing;
            if (auto callback_error = invoke_state_callback(session, publisher_state::publishing); callback_error.has_value()) {
                fail_session(session, std::move(callback_error).value());
                return;
            }
        } else if (is_progress_status(connected)) {
            return;
        } else {
            fail_session(session, libshout_error(session.mp_shout, connected, "connect libshout publisher"));
            return;
        }
    }

    if (session.m_control->m_state != publisher_state::publishing) {
        return;
    }

    const auto progressed = shout_send(session.mp_shout, nullptr, 0);
    if ((progressed != SHOUTERR_SUCCESS) and (not is_progress_status(progressed))) {
        fail_session(session, libshout_error(session.mp_shout, progressed, "progress libshout publisher"));
        return;
    }
    if (not refresh_backend_buffer(session)) {
        return;
    }

    drain_buffer(session);
    if (session.m_control->m_state != publisher_state::publishing) {
        return;
    }
    if (not refresh_backend_buffer(session)) {
        return;
    }

    if (session.m_control->m_finish_requested and (session.m_control->m_queued_bytes == 0) and (session.m_control->m_backend_buffered_bytes == 0)) {
        stop_session(session);
    }
}

void destroy_context(std::unique_ptr<detail::libshout_context_impl>& implementation) noexcept {
    if (not implementation) {
        return;
    }
    for (auto& session : implementation->m_sessions) {
        close_handle(*session);
        clear_buffer(*session);
        session->m_control->m_state = publisher_state::stopped;
    }
    implementation->m_sessions.clear();
    implementation.reset();
    release_runtime();
}

} // namespace

libshout_publisher::libshout_publisher(std::shared_ptr<detail::libshout_publisher_impl> implementation) noexcept : m_implementation(std::move(implementation)) {}
libshout_publisher::libshout_publisher(libshout_publisher&& other) noexcept : m_implementation(std::move(other.m_implementation)) {}

libshout_publisher& libshout_publisher::operator=(libshout_publisher&& other) noexcept {
    if (this != &other) {
        abort();
        m_implementation = std::move(other.m_implementation);
    }
    return *this;
}

libshout_publisher::~libshout_publisher() {
    abort();
}

publisher_state libshout_publisher::state() const noexcept {
    return m_implementation ? m_implementation->m_state : publisher_state::stopped;
}

bool libshout_publisher::finished() const noexcept {
    return is_terminal_state(state());
}

std::size_t libshout_publisher::buffered_bytes() const noexcept {
    if (not m_implementation) {
        return 0;
    }
    return m_implementation->m_queued_bytes + m_implementation->m_backend_buffered_bytes;
}

std::optional<error> libshout_publisher::last_error() const {
    return m_implementation ? m_implementation->m_last_error : std::optional<error>{};
}

result<publisher_write_result> libshout_publisher::write(std::span<const std::byte> encoded_bytes) {
    if (not m_implementation) {
        return publisher_write_result{.accepted = 0, .status = publisher_write_status::closed};
    }
    if (encoded_bytes.empty()) {
        return publisher_write_result{};
    }
    if (m_implementation->m_finish_requested or m_implementation->m_abort_requested or is_terminal_state(m_implementation->m_state)) {
        return publisher_write_result{.accepted = 0, .status = publisher_write_status::closed};
    }

    const auto buffered = buffered_bytes();
    if (buffered >= m_implementation->m_max_buffered_bytes) {
        return publisher_write_result{.accepted = 0, .status = publisher_write_status::would_block};
    }

    const auto available = m_implementation->m_max_buffered_bytes - buffered;
    const auto accepted = std::min(available, encoded_bytes.size());
    try {
        m_implementation->m_queue.emplace_back(encoded_bytes.begin(), encoded_bytes.begin() + static_cast<std::ptrdiff_t>(accepted));
        m_implementation->m_queued_bytes += accepted;
    } catch (const std::bad_alloc&) {
        return result<publisher_write_result>::failure(make_error(error_category::internal, "buffer encoded stream bytes", "could not allocate publisher buffer"));
    }

    return publisher_write_result{
        .accepted = accepted,
        .status = accepted == encoded_bytes.size() ? publisher_write_status::accepted : publisher_write_status::would_block,
    };
}

result<void> libshout_publisher::finish() {
    if (not m_implementation) {
        return result<void>::success();
    }
    if (m_implementation->m_state == publisher_state::stopped) {
        return result<void>::success();
    }
    if ((m_implementation->m_state == publisher_state::interrupted) or (m_implementation->m_state == publisher_state::failed)) {
        if (m_implementation->m_last_error.has_value()) {
            return result<void>::failure(m_implementation->m_last_error.value());
        }
        return result<void>::failure(make_error(error_category::connection, "finish libshout publisher", "publisher is no longer connected"));
    }
    m_implementation->m_finish_requested = true;
    return result<void>::success();
}

void libshout_publisher::abort() noexcept {
    if (m_implementation and (not is_terminal_state(m_implementation->m_state))) {
        m_implementation->m_abort_requested = true;
    }
}

libshout_context::libshout_context(std::unique_ptr<detail::libshout_context_impl> implementation) noexcept : m_implementation(std::move(implementation)) {}
libshout_context::libshout_context(libshout_context&& other) noexcept : m_implementation(std::move(other.m_implementation)) {}

libshout_context& libshout_context::operator=(libshout_context&& other) noexcept {
    if (this != &other) {
        destroy_context(m_implementation);
        m_implementation = std::move(other.m_implementation);
    }
    return *this;
}

libshout_context::~libshout_context() {
    destroy_context(m_implementation);
}

result<libshout_context> libshout_context::create() {
    int major = 0;
    int minor = 0;
    int patch = 0;
    acquire_runtime();
    (void)shout_version(&major, &minor, &patch);
    if ((major < 2) or ((major == 2) and (minor < 4)) or ((major == 2) and (minor == 4) and (patch < 4))) {
        release_runtime();
        return result<libshout_context>::failure(make_error(error_category::unsupported, "initialize libshout", "libshout 2.4.4 or newer is required"));
    }
    try {
        return libshout_context{std::make_unique<detail::libshout_context_impl>()};
    } catch (const std::bad_alloc&) {
        release_runtime();
        return result<libshout_context>::failure(make_error(error_category::internal, "initialize libshout", "could not allocate libshout context"));
    }
}

result<libshout_publisher> libshout_context::publish(publisher_config config, libshout_publisher_callbacks callbacks, libshout_publisher_options options) {
    if (not m_implementation) {
        return result<libshout_publisher>::failure(make_error(error_category::internal, "create libshout publisher", "libshout context is not initialized"));
    }
    if (auto validation = validate_publisher_config(config); not validation) {
        return result<libshout_publisher>::failure(std::move(validation).error());
    }
    if (options.connect_timeout.count() < 0) {
        return result<libshout_publisher>::failure(make_error(error_category::configuration, "create libshout publisher", "connect timeout cannot be negative"));
    }
    if (options.send_chunk_bytes == 0) {
        return result<libshout_publisher>::failure(make_error(error_category::configuration, "create libshout publisher", "send chunk size must be greater than zero"));
    }

    try {
        auto control = std::make_shared<detail::libshout_publisher_impl>();
        auto session = std::make_unique<detail::libshout_session>();
        session->m_control = control;
        session->m_config = std::move(config);
        session->m_callbacks = std::move(callbacks);
        session->m_options = options;
        control->m_state = publisher_state::connecting;
        control->m_max_buffered_bytes = session->m_config.max_buffered_bytes;

        if (auto configured = configure_shout(*session); not configured) {
            close_handle(*session);
            return result<libshout_publisher>::failure(std::move(configured).error());
        }

        session->m_connect_started = std::chrono::steady_clock::now();
        const auto opened = shout_open(session->mp_shout);
        if (opened == SHOUTERR_SUCCESS) {
            control->m_state = publisher_state::publishing;
        } else if (is_progress_status(opened)) {
            control->m_state = publisher_state::connecting;
        } else {
            auto failure = libshout_error(session->mp_shout, opened, "connect libshout publisher");
            close_handle(*session);
            return result<libshout_publisher>::failure(std::move(failure));
        }

        if (auto callback_error = invoke_state_callback(*session, control->m_state); callback_error.has_value()) {
            close_handle(*session);
            clear_buffer(*session);
            return result<libshout_publisher>::failure(std::move(callback_error).value());
        }

        m_implementation->m_sessions.push_back(std::move(session));
        return libshout_publisher{std::move(control)};
    } catch (const std::bad_alloc&) {
        return result<libshout_publisher>::failure(make_error(error_category::internal, "create libshout publisher", "could not allocate publisher state"));
    }
}

result<void> libshout_context::poll() {
    if (not m_implementation) {
        return result<void>::failure(make_error(error_category::internal, "poll libshout context", "libshout context is not initialized"));
    }
    try {
        for (auto& session : m_implementation->m_sessions) {
            poll_session(*session);
        }
        std::erase_if(m_implementation->m_sessions, [](const auto& session) { return is_terminal_state(session->m_control->m_state); });
        return result<void>::success();
    } catch (const std::exception& exception) {
        return result<void>::failure(make_error(error_category::internal, "poll libshout context", exception.what()));
    } catch (...) {
        return result<void>::failure(make_error(error_category::internal, "poll libshout context", "publisher callback threw a non-standard exception"));
    }
}

std::size_t libshout_context::active_publisher_count() const noexcept {
    return m_implementation ? m_implementation->m_sessions.size() : 0;
}

void libshout_context::request_stop_all() noexcept {
    if (not m_implementation) {
        return;
    }
    for (auto& session : m_implementation->m_sessions) {
        session->m_control->m_abort_requested = true;
    }
}

} // namespace icecast
