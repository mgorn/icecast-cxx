#include <icecast/stream.hxx>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
    if (not condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<std::byte> bytes(std::string_view value) {
    std::vector<std::byte> output;
    output.reserve(value.size());
    for (const auto character : value) {
        output.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return output;
}

std::string text(std::span<const std::byte> value) {
    std::string output;
    output.reserve(value.size());
    for (const auto item : value) {
        output.push_back(static_cast<char>(std::to_integer<unsigned char>(item)));
    }
    return output;
}

void test_reconnect_policy() {
    icecast::reconnect_policy policy;
    check(icecast::validate_reconnect_policy(policy).has_value(), "default reconnect policy is valid");
    check(icecast::allows_reconnect(policy, 1), "unlimited reconnect policy allows attempts");
    check(icecast::reconnect_base_delay(policy, 0) == std::chrono::milliseconds{500}, "first reconnect delay uses initial delay");
    check(icecast::reconnect_base_delay(policy, 1) == std::chrono::milliseconds{1000}, "second reconnect delay uses multiplier");
    check(icecast::reconnect_base_delay(policy, 100) == std::chrono::milliseconds{30'000}, "reconnect delay caps at maximum");

    policy.max_attempts = 2;
    check(icecast::allows_reconnect(policy, 0), "attempt zero is allowed");
    check(icecast::allows_reconnect(policy, 1), "attempt one is allowed");
    check(not icecast::allows_reconnect(policy, 2), "max attempts is respected");

    policy.jitter = 1.5;
    check(not icecast::validate_reconnect_policy(policy), "out-of-range jitter is rejected");
}

void test_configs() {
    icecast::listener_config listener;
    listener.endpoint.host = "localhost";
    listener.mount.path = "/radio";
    check(icecast::validate_listener_config(listener).has_value(), "basic listener config is valid");

    listener.request_headers.fields.push_back({"Authorization", "Basic bad"});
    check(not icecast::validate_listener_config(listener), "listener cannot override managed auth header");

    icecast::publisher_config publisher;
    publisher.endpoint.host = "localhost";
    publisher.mount.path = "/radio";
    publisher.credentials = {"source", "secret"};
    publisher.content_type = "audio/ogg";
    check(icecast::validate_publisher_config(publisher).has_value(), "basic publisher config is valid");
    check(publisher.max_buffered_bytes == 256 * 1024, "publisher has bounded default queue size");

    publisher.max_buffered_bytes = 0;
    check(not icecast::validate_publisher_config(publisher), "zero publisher queue size is rejected");
}

void test_listener_state() {
    icecast::listener_status status;
    check(icecast::transition_listener(status, icecast::listener_state::connecting).has_value(), "idle can connect");
    check(icecast::transition_listener(status, icecast::listener_state::streaming).has_value(), "connecting can stream");
    check(status.connection_generation == 1, "first streaming connection creates generation one");
    check(icecast::transition_listener(status, icecast::listener_state::reconnect_wait).has_value(), "stream can wait to reconnect");
    check(icecast::transition_listener(status, icecast::listener_state::connecting).has_value(), "reconnect wait can connect");
    check(icecast::transition_listener(status, icecast::listener_state::streaming).has_value(), "reconnection can stream");
    check(status.connection_generation == 2, "reconnection increments connection generation");
    check(not icecast::transition_listener(status, icecast::listener_state::connecting), "streaming cannot jump directly to connecting");
    check(status.state == icecast::listener_state::streaming, "invalid transition does not mutate state");
}

void test_icy_passthrough() {
    icecast::icy_stream_decoder decoder;
    icecast::reset_icy_stream_decoder(decoder, 0);

    const auto input = bytes("plain media");
    std::string media;
    int metadata_calls = 0;
    const auto consumed = icecast::consume_icy_stream(
        decoder,
        input,
        [&](std::span<const std::byte> chunk) {
            media += text(chunk);
            return icecast::stream_action::continue_stream;
        },
        [&](const icecast::icy_metadata_event_view&) {
            ++metadata_calls;
            return icecast::stream_action::continue_stream;
        }
    );

    check(consumed.consumed == input.size(), "pass-through consumes all bytes");
    check(media == "plain media", "metadata-disabled decoder passes media through unchanged");
    check(metadata_calls == 0, "metadata-disabled decoder emits no metadata");
}

void test_icy_fragmentation() {
    icecast::icy_stream_decoder decoder;
    icecast::reset_icy_stream_decoder(decoder, 5);

    auto metadata = bytes("StreamTitle='Hi';StreamUrl='x';");
    metadata.resize(32, std::byte{0});

    std::vector<std::byte> wire;
    const auto first_media = bytes("abcde");
    wire.insert(wire.end(), first_media.begin(), first_media.end());
    wire.push_back(std::byte{2});
    wire.insert(wire.end(), metadata.begin(), metadata.end());
    const auto second_media = bytes("FGHIJ");
    wire.insert(wire.end(), second_media.begin(), second_media.end());
    wire.push_back(std::byte{0});
    const auto third_media = bytes("klmno");
    wire.insert(wire.end(), third_media.begin(), third_media.end());

    std::string media;
    std::vector<icecast::icy_metadata> events;

    for (std::size_t offset = 0; offset < wire.size();) {
        const auto result = icecast::consume_icy_stream(
            decoder,
            std::span<const std::byte>{wire}.subspan(offset, 1),
            [&](std::span<const std::byte> chunk) {
                media += text(chunk);
                return icecast::stream_action::continue_stream;
            },
            [&](const icecast::icy_metadata_event_view& event) {
                check(event.sequence == 1, "first non-empty metadata event has sequence one");
                events.push_back(icecast::parse_icy_metadata(event.metadata));
                return icecast::stream_action::continue_stream;
            }
        );
        check(result.consumed == 1, "single-byte fragmented input is consumed");
        offset += result.consumed;
    }

    check(media == "abcdeFGHIJklmno", "ICY framing is completely removed from media bytes");
    check(events.size() == 1, "zero-length ICY metadata does not emit an update event");
    check(events.front().raw_block.size() == 32, "owned metadata preserves the complete padded raw block");
    check(events.front().payload_size == 31, "metadata payload size excludes trailing zero padding");

    const auto title = events.front().first("streamtitle");
    check(title.has_value(), "metadata lookup is ASCII case-insensitive");
    if (title) {
        check(text(title.value()) == "Hi", "StreamTitle value is parsed without decoding policy");
    }
    const auto url = events.front().first("StreamUrl");
    check(url.has_value() and (text(url.value()) == "x"), "StreamUrl value is parsed");
}

void test_icy_apostrophe_value() {
    auto block = bytes("StreamTitle='Guns N' Roses';");
    icecast::icy_metadata_view view{
        .raw_block = block,
        .payload = block,
    };
    const auto parsed = icecast::parse_icy_metadata(view);
    const auto title = parsed.first("StreamTitle");
    check(title.has_value() and (text(title.value()) == "Guns N' Roses"), "quoted ICY values can contain apostrophes before the closing quote");
}

void test_icy_pause() {
    icecast::icy_stream_decoder decoder;
    icecast::reset_icy_stream_decoder(decoder, 5);
    auto input = bytes("abcde");
    input.push_back(std::byte{0});
    const auto tail = bytes("12345");
    input.insert(input.end(), tail.begin(), tail.end());

    int media_calls = 0;
    const auto first = icecast::consume_icy_stream(
        decoder,
        input,
        [&](std::span<const std::byte>) {
            ++media_calls;
            return icecast::stream_action::pause;
        },
        [&](const icecast::icy_metadata_event_view&) {
            return icecast::stream_action::continue_stream;
        }
    );
    check(first.action == icecast::stream_action::pause, "consumer can pause stream delivery");
    check(first.consumed == 5, "pause reports exactly the bytes already delivered");
    check(decoder.phase == icecast::icy_decoder_phase::metadata_length, "pause preserves decoder phase at metadata boundary");

    std::string resumed;
    const auto second = icecast::consume_icy_stream(
        decoder,
        std::span<const std::byte>{input}.subspan(first.consumed),
        [&](std::span<const std::byte> chunk) {
            resumed += text(chunk);
            return icecast::stream_action::continue_stream;
        },
        [&](const icecast::icy_metadata_event_view&) {
            return icecast::stream_action::continue_stream;
        }
    );
    check(second.consumed == input.size() - first.consumed, "resumed decoder consumes remaining wire bytes");
    check(resumed == "12345", "resumed decoder continues after zero-length metadata marker");
    check(media_calls == 1, "paused callback is not repeated implicitly");
}

} // namespace

int main() {
    test_reconnect_policy();
    test_configs();
    test_listener_state();
    test_icy_passthrough();
    test_icy_fragmentation();
    test_icy_apostrophe_value();
    test_icy_pause();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "stream tests passed\n";
    return EXIT_SUCCESS;
}
