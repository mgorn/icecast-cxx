#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <icecast/stream/export.hxx>
#include <icecast/stream/models.hxx>

namespace icecast {

inline constexpr std::size_t icy_metadata_length_unit = 16;
inline constexpr std::size_t icy_max_metadata_block_size = 255 * icy_metadata_length_unit;

struct icy_metadata_view {
    std::span<const std::byte> raw_block;
    std::span<const std::byte> payload;
};

struct icy_metadata_field {
    std::string name;
    std::vector<std::byte> value;
};

struct icy_metadata {
    [[nodiscard]] ICECAST_CXX_STREAM_API std::optional<std::span<const std::byte>> first(std::string_view name) const noexcept;

    std::vector<std::byte> raw_block;
    std::size_t payload_size = 0;
    std::vector<icy_metadata_field> fields;
};

struct icy_metadata_event_view {
    std::uint64_t sequence = 0;
    icy_metadata_view metadata;
};

enum struct icy_decoder_phase {
    media,
    metadata_length,
    metadata,
};

struct icy_stream_decoder {
    std::size_t metadata_interval = 0;
    std::size_t media_remaining = 0;
    std::size_t metadata_remaining = 0;
    std::size_t metadata_size = 0;
    std::uint64_t metadata_sequence = 0;
    icy_decoder_phase phase = icy_decoder_phase::media;
    std::array<std::byte, icy_max_metadata_block_size> metadata_buffer{};
};

struct stream_consume_result {
    std::size_t consumed = 0;
    stream_action action = stream_action::continue_stream;
};

[[nodiscard]] ICECAST_CXX_STREAM_API bool icy_metadata_name_equal(std::string_view left, std::string_view right) noexcept;
[[nodiscard]] ICECAST_CXX_STREAM_API icy_metadata parse_icy_metadata(icy_metadata_view metadata);
ICECAST_CXX_STREAM_API void reset_icy_stream_decoder(icy_stream_decoder& decoder, std::size_t metadata_interval) noexcept;

namespace detail {

[[nodiscard]] inline std::size_t icy_payload_size(std::span<const std::byte> block) noexcept {
    std::size_t size = block.size();
    while ((size > 0) and (block[size - 1] == std::byte{0})) {
        --size;
    }
    return size;
}

} // namespace detail

template <typename media_consumer_type, typename metadata_consumer_type>
requires std::same_as<std::invoke_result_t<media_consumer_type&, std::span<const std::byte>>, stream_action> and
    std::same_as<std::invoke_result_t<metadata_consumer_type&, const icy_metadata_event_view&>, stream_action>
[[nodiscard]] stream_consume_result consume_icy_stream(icy_stream_decoder& decoder, std::span<const std::byte> input, media_consumer_type&& media_consumer, metadata_consumer_type&& metadata_consumer) {
    std::size_t offset = 0;

    if (decoder.metadata_interval == 0) {
        if (input.empty()) {
            return {};
        }

        return {
            .consumed = input.size(),
            .action = std::invoke(media_consumer, input),
        };
    }

    while (offset < input.size()) {
        if (decoder.phase == icy_decoder_phase::media) {
            const auto count = std::min(decoder.media_remaining, input.size() - offset);
            const auto media = input.subspan(offset, count);
            offset += count;
            decoder.media_remaining -= count;

            if (decoder.media_remaining == 0) {
                decoder.phase = icy_decoder_phase::metadata_length;
            }

            if (not media.empty()) {
                const auto action = std::invoke(media_consumer, media);
                if (action != stream_action::continue_stream) {
                    return {.consumed = offset, .action = action};
                }
            }
            continue;
        }

        if (decoder.phase == icy_decoder_phase::metadata_length) {
            const auto units = std::to_integer<unsigned int>(input[offset]);
            ++offset;

            decoder.metadata_size = static_cast<std::size_t>(units) * icy_metadata_length_unit;
            decoder.metadata_remaining = decoder.metadata_size;

            if (decoder.metadata_size == 0) {
                decoder.media_remaining = decoder.metadata_interval;
                decoder.phase = icy_decoder_phase::media;
            } else {
                decoder.phase = icy_decoder_phase::metadata;
            }
            continue;
        }

        const auto count = std::min(decoder.metadata_remaining, input.size() - offset);
        const auto destination_offset = decoder.metadata_size - decoder.metadata_remaining;
        std::copy_n(input.begin() + static_cast<std::ptrdiff_t>(offset), count, decoder.metadata_buffer.begin() + static_cast<std::ptrdiff_t>(destination_offset));
        offset += count;
        decoder.metadata_remaining -= count;

        if (decoder.metadata_remaining != 0) {
            continue;
        }

        const auto block = std::span<const std::byte>{decoder.metadata_buffer.data(), decoder.metadata_size};
        const auto payload_size = detail::icy_payload_size(block);
        const icy_metadata_event_view event{
            .sequence = ++decoder.metadata_sequence,
            .metadata = {
                .raw_block = block,
                .payload = block.first(payload_size),
            },
        };

        decoder.metadata_size = 0;
        decoder.media_remaining = decoder.metadata_interval;
        decoder.phase = icy_decoder_phase::media;

        const auto action = std::invoke(metadata_consumer, event);
        if (action != stream_action::continue_stream) {
            return {.consumed = offset, .action = action};
        }
    }

    return {.consumed = offset, .action = stream_action::continue_stream};
}

} // namespace icecast
