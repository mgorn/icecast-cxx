#include <algorithm>
#include <cctype>
#include <cstring>

#include <icecast/stream/icy.hxx>

namespace icecast {

namespace {

[[nodiscard]] unsigned char byte_value(std::byte value) noexcept {
    return std::to_integer<unsigned char>(value);
}

[[nodiscard]] bool ascii_space(std::byte value) noexcept {
    const auto character = byte_value(value);
    return (character == ' ') or (character == '\t');
}

[[nodiscard]] std::string byte_string(std::span<const std::byte> bytes) {
    std::string value(bytes.size(), '\0');
    if (not bytes.empty()) {
        std::memcpy(value.data(), bytes.data(), bytes.size());
    }
    return value;
}

[[nodiscard]] std::span<const std::byte> trim_ascii_space(std::span<const std::byte> value) noexcept {
    while ((not value.empty()) and ascii_space(value.front())) {
        value = value.subspan(1);
    }
    while ((not value.empty()) and ascii_space(value.back())) {
        value = value.first(value.size() - 1);
    }
    return value;
}

} // namespace

bool icy_metadata_name_equal(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto left_value = static_cast<unsigned char>(left[index]);
        const auto right_value = static_cast<unsigned char>(right[index]);
        if (std::tolower(left_value) != std::tolower(right_value)) {
            return false;
        }
    }
    return true;
}

std::optional<std::span<const std::byte>> icy_metadata::first(std::string_view name) const noexcept {
    const auto found = std::find_if(fields.begin(), fields.end(), [&](const icy_metadata_field& field) {
        return icy_metadata_name_equal(field.name, name);
    });
    if (found == fields.end()) {
        return std::nullopt;
    }
    return std::span<const std::byte>{found->value};
}

icy_metadata parse_icy_metadata(icy_metadata_view metadata) {
    icy_metadata result;
    result.raw_block.assign(metadata.raw_block.begin(), metadata.raw_block.end());
    result.payload_size = std::min(metadata.payload.size(), result.raw_block.size());

    auto payload = metadata.payload;
    std::size_t offset = 0;

    while (offset < payload.size()) {
        while ((offset < payload.size()) and ((payload[offset] == std::byte{';'}) or ascii_space(payload[offset]))) {
            ++offset;
        }
        if (offset >= payload.size()) {
            break;
        }

        const auto name_start = offset;
        while ((offset < payload.size()) and (payload[offset] != std::byte{'='}) and (payload[offset] != std::byte{';'})) {
            ++offset;
        }
        if ((offset >= payload.size()) or (payload[offset] != std::byte{'='})) {
            break;
        }

        auto name_bytes = trim_ascii_space(payload.subspan(name_start, offset - name_start));
        ++offset;
        if (name_bytes.empty()) {
            continue;
        }

        while ((offset < payload.size()) and ascii_space(payload[offset])) {
            ++offset;
        }

        const bool quoted = (offset < payload.size()) and (payload[offset] == std::byte{'\''});
        if (quoted) {
            ++offset;
        }
        const auto value_start = offset;

        if (quoted) {
            while (offset < payload.size()) {
                if (payload[offset] == std::byte{'\''}) {
                    auto next = offset + 1;
                    while ((next < payload.size()) and ascii_space(payload[next])) {
                        ++next;
                    }
                    if ((next >= payload.size()) or (payload[next] == std::byte{';'})) {
                        break;
                    }
                }
                ++offset;
            }
        } else {
            while ((offset < payload.size()) and (payload[offset] != std::byte{';'})) {
                ++offset;
            }
        }

        auto value_bytes = payload.subspan(value_start, offset - value_start);
        if (not quoted) {
            value_bytes = trim_ascii_space(value_bytes);
        }

        result.fields.push_back({
            .name = byte_string(name_bytes),
            .value = std::vector<std::byte>{value_bytes.begin(), value_bytes.end()},
        });

        if (quoted and (offset < payload.size()) and (payload[offset] == std::byte{'\''})) {
            ++offset;
        }
        while ((offset < payload.size()) and (payload[offset] != std::byte{';'})) {
            ++offset;
        }
        if ((offset < payload.size()) and (payload[offset] == std::byte{';'})) {
            ++offset;
        }
    }

    return result;
}

void reset_icy_stream_decoder(icy_stream_decoder& decoder, std::size_t metadata_interval) noexcept {
    decoder.metadata_interval = metadata_interval;
    decoder.media_remaining = metadata_interval;
    decoder.metadata_remaining = 0;
    decoder.metadata_size = 0;
    decoder.metadata_sequence = 0;
    decoder.phase = icy_decoder_phase::media;
}

} // namespace icecast
