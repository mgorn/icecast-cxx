# icecast-cxx

A modern, modular C++ interface for interacting with Icecast servers.

> **Project status:** early implementation phase. `icecast::core` and the transport-independent portions of `icecast::stream` are implemented. Administrative functionality, native/browser networking, and source-publishing backends are still under development; the project does not yet provide a complete network-connected Icecast client.

`icecast-cxx` exists to make Icecast pleasant to use from modern C++. It provides strong value types, explicit ownership, structured errors, modular CMake targets, platform-aware capability models, and protocol helpers without exposing Icecast server internals or forcing C APIs into application code.

The library is a networking/protocol library, **not an audio engine**. It deals in encoded media bytes and Icecast/ICY metadata. Encoding, decoding, muxing, demuxing, audio devices, and realtime PCM processing belong in the caller or other media libraries.

## Goals

- Provide idiomatic C++ interfaces instead of exposing Icecast server-global structures or raw third-party handles.
- Make common Icecast operations convenient without hiding ownership, authentication, backpressure, or platform constraints.
- Keep listening, publishing, administration, and platform backends modular so consumers only link what they use.
- Share protocol semantics between native and WebAssembly/browser builds without pretending browsers have normal POSIX sockets.
- Keep codec policy outside the library.
- Remain independent of Python while staying straightforward to bind from the future `pycecast` project.
- Reuse upstream Xiph components only where technically and legally sensible.
- Cooperate with both zero-configuration CMake users and projects that manage dependencies themselves.

## Current implementation

Two CMake targets now contain real C++20 implementation:

- `icecast::core`
- `icecast::stream`

The remaining targets are currently placeholders for later implementation:

- `icecast::admin`
- `icecast::transport_curl`
- `icecast::publish_libshout`
- `icecast::transport_web`

### `icecast::core`

The foundational API includes HTTP/HTTPS server endpoints with reverse-proxy base paths, Icecast mountpoints, Basic-auth credential values/redaction, duplicate-preserving case-insensitive headers, project-owned `result<T>` / structured errors, and platform/server/effective capability models.

### `icecast::stream`

The transport-independent stream layer includes listener/publisher configuration, stream-level metadata, normalized stream response information, explicit `continue_stream` / `pause` / `stop` backpressure, listener reconnect policy, listener lifecycle/generation tracking, publisher lifecycle vocabulary, ICY framing demultiplexing, and raw/best-effort-parsed ICY metadata.

No libcurl, libshout, Emscripten, codec, or audio-engine type appears in these public semantic APIs.

## Core quick start

```cpp
#include <icecast/core.hxx>

#include <iostream>

int main() {
    auto endpoint = icecast::parse_server_endpoint("https://radio.example.com/icecast/");
    auto mount = icecast::parse_mountpoint("/live.ogg");

    if ((not endpoint) or (not mount)) {
        return 1;
    }

    auto stream_url = icecast::resolve_mount_url(endpoint.value(), mount.value());
    if (not stream_url) {
        std::cerr << stream_url.error().message << '\n';
        return 1;
    }

    std::cout << stream_url.value() << '\n';
}
```

Credentials are deliberately separate from endpoint URLs. Endpoint base paths and mountpoints are already-serialized URL path text; `core` does not guess whether caller input should be percent-encoded or decoded.

## Stream configuration

The semantic stream models can be used independently of any network backend:

```cpp
#include <icecast/stream.hxx>

icecast::listener_config listener;
listener.endpoint.host = "radio.example.com";
listener.endpoint.scheme = icecast::endpoint_scheme::https;
listener.mount.path = "/live.ogg";
listener.request_icy_metadata = true;

if (not icecast::validate_listener_config(listener)) {
    // Handle local configuration error.
}
```

Publisher configuration describes **already encoded/muxed bytes**. The conventional source username defaults to `source`, while callers provide the password and media content type:

```cpp
icecast::publisher_config publisher;
publisher.endpoint.host = "radio.example.com";
publisher.endpoint.scheme = icecast::endpoint_scheme::https;
publisher.mount.path = "/live.ogg";
publisher.credentials.password = "secret";
publisher.content_type = "audio/ogg";
publisher.metadata.name = "My Stream";

if (not icecast::validate_publisher_config(publisher)) {
    // Handle local configuration error.
}
```

Publisher buffering is explicitly bounded through `max_buffered_bytes`; the default semantic limit is 256 KiB. A future backend must expose backpressure rather than silently dropping encoded bytes or growing an unbounded queue.

## ICY metadata and encoded-byte delivery

When a future transport observes an `icy-metaint` response value, it can feed received response-body bytes through `icy_stream_decoder`.

```cpp
icecast::icy_stream_decoder decoder;
icecast::reset_icy_stream_decoder(decoder, metadata_interval);

auto result = icecast::consume_icy_stream(
    decoder,
    received_bytes,
    [&](std::span<const std::byte> encoded_media) {
        decoder_or_muxer.consume(encoded_media);
        return icecast::stream_action::continue_stream;
    },
    [&](const icecast::icy_metadata_event_view& event) {
        auto owned = icecast::parse_icy_metadata(event.metadata);
        handle_metadata(owned);
        return icecast::stream_action::continue_stream;
    }
);
```

Media spans borrow the caller-supplied input buffer and require no per-chunk allocation. ICY framing is removed completely before media is delivered.

The protocol length byte limits a metadata block to 4080 bytes, so the decoder keeps a fixed-size metadata buffer. Metadata callback views borrow that buffer and should be copied/parsed if they need to outlive the callback/use interval.

ICY field values remain raw bytes. The library deliberately does **not** guess whether historic metadata is Latin-1, UTF-8, or another encoding.

`stream_consume_result.consumed` plus `stream_action` allow future transports to implement real pause/resume without losing unread network bytes.

## Listener reconnection

`reconnect_policy` defaults to an enabled exponential policy with a 500 ms initial delay, 30 s maximum delay, 2x multiplier, and 20% jitter allowance.

Every successful physical listener connection increments `listener_status::connection_generation`, allowing applications to reset media decoders/demuxers after reconnect.

Publisher auto-reconnect is intentionally **not** implemented by this semantic layer. Continuing halfway through an encoded Ogg/WebM/etc. logical stream on a fresh source connection may be invalid; a later publishing backend must coordinate interruption/new-publication behavior explicitly.

## Media boundary

Listening:

```text
Icecast -> encoded stream bytes -> icecast-cxx -> caller decoder/demuxer -> decoded media
```

Publishing:

```text
caller source -> caller encoder/muxer -> encoded bytes -> icecast-cxx -> Icecast
```

`icecast-cxx` should not become responsible for Opus, Vorbis, MP3, AAC, Ogg, WebM, audio devices, or realtime graph processing unless a narrowly scoped dependency is genuinely required to interpret Icecast protocol behavior itself.

## Building the project

CMake is the primary build system. C++20 is the current language baseline.

The easiest developer build is:

```console
python build.py
```

`build.py` reads `dependencies.json`, prepares pinned dependency sources beneath the git-ignored `dependencies/` directory, passes those sources to CMake, builds selected components, and runs CTest unless tests are disabled.

Useful commands include:

```console
python build.py --help
python build.py --clean
python build.py --configuration Release
python build.py --offline
python build.py --configure-only
python build.py --no-tests
```

The dependency manifest is currently empty because the implemented `core` and `stream` layers have no third-party dependency and the network backends have not yet been wired into the build.

Python is **not** required for normal downstream CMake consumption.

## Dependency philosophy

Dependencies are **conditional requirements**, not unconditional project requirements. A future native publishing backend may require libshout while a consumer using only `icecast::core` and `icecast::stream` should not need it.

For every dependency required by an enabled component, CMake should resolve it in this order:

1. reuse an already-defined compatible target from the parent project;
2. honor an explicit source/package hint supplied by the developer or `build.py`;
3. reuse a compatible checkout beneath `ICECAST_CXX_DEPENDENCIES_DIR`;
4. discover a compatible installed/system package;
5. use the pinned immutable `FetchContent` fallback when downloads are enabled;
6. fail with an actionable message when the dependency is required and fetching is disabled.

`ICECAST_CXX_FETCH_DEPENDENCIES` defaults to `ON`; disabling it makes configuration network-free.

Heavyweight backend targets default off when `icecast-cxx` is embedded in another CMake project, so adding it through `FetchContent` or `add_subdirectory()` does not unexpectedly acquire curl, libshout, or browser-specific requirements.

## Consuming with CMake

### FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
    icecast_cxx
    GIT_REPOSITORY https://github.com/mgorn/icecast-cxx.git
    GIT_TAG <immutable-release-tag-or-commit>
)

FetchContent_MakeAvailable(icecast_cxx)

target_link_libraries(my_app PRIVATE icecast::core icecast::stream)
```

Use a released tag or immutable commit rather than a moving branch for reproducible builds.

### Git submodule

```console
git submodule add https://github.com/mgorn/icecast-cxx.git external/icecast-cxx
```

```cmake
add_subdirectory(external/icecast-cxx)
target_link_libraries(my_app PRIVATE icecast::core icecast::stream)
```

### Downloaded source/ZIP

```cmake
add_subdirectory(external/icecast-cxx)
target_link_libraries(my_app PRIVATE icecast::core icecast::stream)
```

The project does not assume it is the top-level CMake project, and developer-only tests/tools do not become part of an embedded build by default.

### Installed package

```cmake
find_package(icecast-cxx CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE icecast::core icecast::stream)
```

The project installs headers/libraries and CMake package metadata for enabled compiled components. Installed packages must not unexpectedly run `FetchContent` inside downstream projects.

## Platform direction

Planned first-class platforms are Windows, macOS, Linux, and Emscripten/WebAssembly in supported browsers.

Native and browser implementations will share `core`/`stream` semantics while using materially different networking mechanisms underneath. `build.py --platform web` is reserved for Emscripten builds and expects `emcmake` on `PATH`.

Browser publishing remains capability-dependent because CORS, mixed-content policy, streaming request bodies, redirects, and browser HTTP behavior differ from native networking.

## Licensing

Original `icecast-cxx` code is licensed under the [MIT License](LICENSE).

Third-party dependencies retain their own licenses. Dependency choice and distribution strategy must be reviewed deliberately, particularly for Xiph components under GNU Library/LGPL-family terms.

Do not copy GPL-licensed Icecast server implementation code into this project. The server source may be studied as an authoritative behavior reference while the client-facing C++ implementation remains independently designed.

## Other build systems

CMake is the only planned first-party build system for initial releases. If you need Meson, Bazel, another build system, or additional package-manager integration, please open an issue or submit a pull request. New build-system support should preserve target modularity and dependency-management principles.

## Contributing and coding agents

Project-specific contributor/agent guidance lives in [`AGENTS.md`](AGENTS.md) and [`docs/agents/`](docs/agents/).

Foundational API, dependency, licensing, threading, and transport decisions should continue to be documented and reviewed rather than introduced incidentally while implementing unrelated functionality.
