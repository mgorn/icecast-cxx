# Native libshout publisher guidance

`icecast::publish_libshout` is the implemented native source-publishing backend. It depends publicly on `icecast::stream` and privately on libshout; no `shout_t*`, libshout constant, or libshout header appears in the public API.

## Public API

Use:

```cpp
#include <icecast/publish_libshout.hxx>
```

The active types are move-only:

- `libshout_context` owns/drives native publisher sessions;
- `libshout_publisher` is a handle to one logical source publication;
- `libshout_publisher_callbacks` reports state changes and terminal errors;
- `libshout_publisher_options` currently controls connect timeout and maximum per-poll raw-send chunk size.

`libshout_context::poll()` is the progress mechanism. There is no hidden publisher worker thread.

## Media contract

The backend receives already encoded/muxed bytes. It intentionally uses libshout's raw-send path rather than asking libshout to pace the stream from codec/container timestamps. Pacing remains the caller's responsibility.

Do not add codec encoders, PCM handling, file pacing, or container generation to this backend.

## Bounded buffering

`publisher_config::max_buffered_bytes` bounds the sum of icecast-cxx-owned queued bytes and the libshout send queue as reported by `shout_queuelen()`.

`libshout_publisher::write()` accepts at most the available capacity and returns the exact accepted prefix length. Queue exhaustion is `publisher_write_status::would_block`, not silent loss and not an unbounded allocation.

`send_chunk_bytes` bounds how much queued data the adapter offers to `shout_send_raw()` in one operation; it is not a second queue size.

## Finish, abort, and interruption

`finish()` marks the logical publication complete and drains both the local queue and libshout's observed send queue before closing normally.

`abort()` requests immediate stop and discards pending bytes.

Once a source reached `publisher_state::publishing`, a connection/TLS/protocol/server interruption transitions it to `publisher_state::interrupted`. Do not automatically reconnect this handle. The caller owns creation of a fresh logical encoded stream/container if it wants to publish again.

## Supported content types

The semantic `publisher_config::content_type` remains an open MIME string, but this backend can only represent formats understood by libshout 2.4.x. It currently accepts:

- `audio/mpeg`
- `audio/ogg`
- `video/ogg`
- `application/ogg`
- `audio/webm`
- `video/webm`
- `audio/x-matroska`
- `video/x-matroska`

Unsupported MIME values fail during publisher creation with `error_category::unsupported`; never silently map an unknown MIME type to a different libshout format.

## Endpoint, authentication, and TLS

The backend uses the configured endpoint host/effective port and combines the endpoint base path with the mountpoint before passing the source resource to libshout.

Basic source credentials come from `publisher_config::credentials`. The conventional username defaults to `source`; the password must be non-empty.

HTTP endpoints explicitly request disabled TLS when supported by the libshout build. HTTPS endpoints require libshout's RFC2818 TLS mode. Certificate/hostname validation is performed by libshout's TLS implementation; failure is translated into the project error taxonomy.

Do not put credentials into URLs or diagnostics.

## Metadata and request headers

`publisher_config::metadata` maps to libshout source metadata (`name`, `description`, `genre`, `url`), and `public_stream` maps to the source public flag.

libshout 2.4.x does not expose an arbitrary source-request-header API. Therefore a non-empty `publisher_config::request_headers` is explicitly rejected by this backend. Do not silently ignore extension headers.

## Error mapping

Translate libshout status/error codes into `icecast::error`. Keep the raw numeric code only as backend diagnostics.

Authentication failures map to `authentication`; TLS errors to `tls`; unsupported operations/formats to `unsupported`; invalid backend configuration to `configuration`; allocation/internal failures to `internal`; connection/socket/unconnected failures to retryable `connection` errors.

Callback exceptions must never unwind through libshout/C boundaries. Convert state-callback exceptions to structured internal failures; suppress exceptions from error callbacks after recording the primary failure.

## Dependency and licensing boundary

The preferred parent-project integration point is an existing `Shout::shout` target. System discovery uses pkg-config `shout >= 2.4.4`. The pinned developer source is the official libshout 2.4.6 release archive recorded in `dependencies.json`.

Upstream libshout uses autotools. Automatic source fallback is therefore an isolated `ExternalProject` on Unix-like hosts, not `add_subdirectory()`/FetchContent-MakeAvailable. Windows currently requires a provided/package-managed libshout target.

libshout is distributed under the GNU Library General Public License version 2 or later. Keep it optional and isolated; do not copy its implementation into MIT-licensed project sources.

## Tests

Use loopback source-server tests so normal test runs do not depend on a public Icecast service. Cover source authentication/handshake, base-path mount construction, encoded byte delivery, bounded partial writes, graceful finish, explicit unsupported formats/headers, and terminal errors where deterministic.
