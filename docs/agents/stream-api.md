# Stream API guidance

`icecast::stream` is an implemented, transport-independent C++20 library layered on `icecast::core`.

It defines Icecast streaming semantics and ICY framing without depending on libcurl, libshout, Emscripten, codecs, or a particular asynchronous runtime. Native and browser backends should build on these types rather than recreate their own public stream models.

## Current public headers

The convenience header is:

```cpp
#include <icecast/stream.hxx>
```

Focused headers live under `include/icecast/stream/`:

- `models.hxx` — listener/publisher configuration, stream metadata/info, listener consumer actions, and publisher write results;
- `reconnect.hxx` — listener reconnect policy and deterministic base-delay calculation;
- `state.hxx` — listener lifecycle state/generation semantics and publisher lifecycle vocabulary;
- `icy.hxx` — ICY metadata framing, byte demultiplexing, raw metadata views, and best-effort field parsing;
- `export.hxx` — shared-library visibility declarations.

## Listener configuration

`listener_config` contains a server endpoint, mountpoint, optional Basic credentials, additional request headers, ICY metadata intent, and reconnect policy. Use `validate_listener_config()` before transport work.

The library owns protocol headers that would conflict with these models. Custom listener headers must not override `Host`, `Authorization`, `Accept-Encoding`, or `Icy-MetaData`; transports synthesize those values from the semantic configuration.

## Publisher configuration

`publisher_config` describes an already encoded/muxed source stream. It does not encode, mux, pace, or inspect media payloads.

The default source username is `source`, matching the conventional Icecast source login. A non-empty password and content type are required by `validate_publisher_config()`.

The model also contains optional stream-level name/description/genre/URL values, public/private directory intent, additional request headers, and `max_buffered_bytes` (256 KiB by default) as the semantic upper bound for publisher buffering.

The following publisher headers are managed by `icecast-cxx` and cannot be overridden through `request_headers`: `Host`, `Authorization`, `Content-Type`, `Content-Length`, `Transfer-Encoding`, `Ice-Public`, `Ice-Name`, `Ice-Description`, `Ice-Genre`, and `Ice-Url`.

A specific backend may support fewer extension headers than the semantic model. For example, libshout does not expose arbitrary source-request headers, so `icecast::publish_libshout` rejects non-empty custom publisher headers explicitly rather than silently ignoring them.

## Publisher writes and backpressure

Publisher backends use `publisher_write_result`:

```text
accepted     all provided bytes were accepted
would_block a prefix may have been accepted; caller retains the remainder
closed       the logical publication no longer accepts bytes
```

`accepted` is always the exact prefix length taken from the caller's span. Backends must never claim bytes they did not retain/send and must never silently drop bytes.

`max_buffered_bytes` is a bound across application-side and backend-owned pending bytes where the backend can observe both. A backend must not treat an unbounded third-party queue as outside the limit merely because that queue is internal to the dependency.

A publisher may accept bytes while connecting, subject to the same bound, so callers can begin filling a small queue before the source handshake completes.

## Publisher lifecycle

The publisher lifecycle vocabulary is:

```text
idle
connecting
publishing
stopping
stopped
interrupted
failed
```

`stopped` is a normal terminal state. `failed` means the publication could not become a usable established stream or encountered a non-stream-interruption terminal failure. `interrupted` means an established publication was broken by a connection/TLS/protocol/server failure.

Publisher interruption is terminal for that logical encoded stream. This project does not blindly reconnect a source and continue from the middle of the previous container. A caller may create a new publisher after preparing a fresh logical publication/container generation.

## Listener reconnect policy

`reconnect_policy` defaults to enabled, unlimited attempts, a 500 ms initial delay, 30 s maximum delay, 2.0 exponential multiplier, and 0.2 jitter fraction.

`max_attempts` counts reconnection attempts after the current/initial connection; `std::nullopt` means unlimited.

`reconnect_base_delay()` returns the deterministic exponential delay before jitter. Randomness/scheduling stays with the execution/transport context instead of becoming a dependency of the semantic model.

## Listener state and connection generation

The listener lifecycle vocabulary is:

```text
idle
connecting
streaming
reconnect_wait
stopping
stopped
failed
```

Use `transition_listener()` rather than hand-editing status in backends. Invalid transitions fail without mutating status.

Every successful transition from `connecting` to `streaming` increments `connection_generation`. Reconnecting is a new physical stream connection, so downstream decoders/demuxers can use the generation change to reset state.

## Listener consumer backpressure

Listening callbacks use `stream_action`: `continue_stream`, `pause`, or `stop`.

When a parser returns `pause` or `stop`, `stream_consume_result.consumed` tells the transport exactly how many input bytes were already accepted. The remainder must not be discarded. A transport must not continue reading indefinitely after the consumer asks to pause.

## ICY metadata demultiplexing

`icy_stream_decoder` removes ICY metadata framing from a received response body while forwarding media spans without copying.

Initialize/reset it with the observed `icy-metaint` value:

```cpp
icecast::icy_stream_decoder decoder;
icecast::reset_icy_stream_decoder(decoder, metadata_interval);
```

An interval of zero disables ICY framing and passes input directly to the media consumer.

For nonzero intervals, `consume_icy_stream()` accepts arbitrarily fragmented input and tracks media bytes, the one-byte metadata length, the metadata block, and the next media interval.

The length byte is at most 255, so the largest metadata block is 4080 bytes. The decoder owns a fixed 4080-byte metadata buffer and performs no heap allocation for media delivery or metadata framing.

A zero metadata-length byte means no metadata update and does not emit a metadata callback.

## Borrowed metadata views and owned parsing

The metadata callback receives `icy_metadata_event_view` with a monotonic non-empty metadata sequence number, `raw_block` including protocol padding, and `payload` with trailing NUL padding removed.

These spans borrow the decoder's internal buffer and are valid only until the decoder consumes more metadata. Copy or parse them if they must outlive that use interval.

`parse_icy_metadata()` creates an owned `icy_metadata` containing the complete raw block, payload size, and best-effort parsed key/value fields. Field names are case-insensitive for lookup. Field values remain bytes; the stream layer does not guess Latin-1, UTF-8, or another historic ICY character encoding.

## Tests

Stream unit tests cover reconnect validation/delay, configuration and managed-header protection, listener transitions/generations, ICY-disabled pass-through, one-byte input fragmentation, zero-length metadata, raw metadata preservation, case-insensitive field lookup, apostrophes in quoted values, and pause/resume at an interval boundary.

Backend-specific publication behavior belongs in backend tests, not in this transport-independent layer.
