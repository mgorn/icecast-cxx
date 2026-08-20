# icecast-cxx

A modern, modular C++ interface for interacting with Icecast servers.

> **Project status:** early implementation. `icecast::core`, the transport-independent portions of `icecast::stream`, and the native libcurl listener backend are implemented. Administration, native source publishing, and browser transport remain under development.

`icecast-cxx` is designed around developer convenience without hiding important networking semantics. Public APIs use C++ value types, RAII, structured errors, explicit backpressure, and modular CMake targets rather than exposing Icecast server internals or third-party C handles.

The library deals in **encoded media bytes** and Icecast/ICY protocol metadata. Encoding, decoding, muxing, demuxing, PCM processing, audio devices, and codec-specific pacing belong to the caller or other media libraries.

## Implemented targets

- `icecast::core`
  - HTTP/HTTPS server endpoints and mountpoints
  - credentials and redacted diagnostics
  - duplicate-preserving HTTP headers
  - structured `error` and `result<T>`
  - platform/server/effective capability models
- `icecast::stream`
  - listener and publisher configuration models
  - listener/publisher lifecycle vocabulary
  - listener reconnect policy and connection generations
  - explicit `continue_stream` / `pause` / `stop` backpressure
  - allocation-free media delivery through the ICY demultiplexer
  - raw and best-effort-parsed ICY metadata
- `icecast::transport_curl`
  - native HTTP/HTTPS listener transport using libcurl
  - Basic authentication
  - redirects with HTTPS downgrade protection
  - TLS peer and hostname verification enabled
  - response header normalization
  - ICY metadata integration
  - pause/resume/stop without unbounded buffering
  - listener reconnect handling
  - caller-driven polling with no hidden worker thread

The following targets currently remain placeholders:

- `icecast::admin`
- `icecast::publish_libshout`
- `icecast::transport_web`

## Native listener quick start

```cpp
#include <icecast/transport_curl.hxx>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <span>

int main() {
    auto context_result = icecast::curl_context::create();
    if (not context_result) {
        std::cerr << context_result.error().message << '\n';
        return 1;
    }
    auto context = std::move(context_result).value();

    icecast::listener_config config;
    config.endpoint.scheme = icecast::endpoint_scheme::https;
    config.endpoint.host = "radio.example.com";
    config.mount.path = "/live.ogg";

    icecast::curl_listener_callbacks callbacks;
    callbacks.on_media = [](std::span<const std::byte> encoded_bytes) {
        // Feed encoded_bytes to your decoder/demuxer here.
        return icecast::stream_action::continue_stream;
    };
    callbacks.on_metadata = [](const icecast::icy_metadata_event_view& metadata) {
        // metadata.metadata.payload is borrowed for this callback only.
        return icecast::stream_action::continue_stream;
    };
    callbacks.on_error = [](const icecast::error& error) {
        std::cerr << error.message << '\n';
    };

    auto listener_result = context.listen(std::move(config), std::move(callbacks));
    if (not listener_result) {
        std::cerr << listener_result.error().message << '\n';
        return 1;
    }
    auto listener = std::move(listener_result).value();

    using namespace std::chrono_literals;
    while (not listener.finished()) {
        if (auto result = context.poll(100ms); not result) {
            std::cerr << result.error().message << '\n';
            return 1;
        }
    }
}
```

`curl_context::poll()` drives network progress. There is no hidden thread, and listener callbacks execute on the thread that calls `poll()`.

A media or metadata callback can return `icecast::stream_action::pause`. Call `listener.resume()` when the consumer is ready and continue polling. The curl adapter keeps at most one bounded libcurl receive tail while paused; it does not create an unbounded application queue.

Each successful connection increments `listener_status::connection_generation`, including successful reconnects. Callers can use that generation to reset a decoder when a new physical stream connection begins.

## Core URL example

```cpp
#include <icecast/core.hxx>

#include <iostream>

int main() {
    auto endpoint = icecast::parse_server_endpoint("https://radio.example.com/icecast/");
    auto mount = icecast::parse_mountpoint("/live.ogg");

    if ((not endpoint) or (not mount)) {
        return 1;
    }

    auto url = icecast::resolve_mount_url(endpoint.value(), mount.value());
    if (not url) {
        return 1;
    }

    std::cout << url.value() << '\n';
}
```

This prints `https://radio.example.com/icecast/live.ogg`.

Credentials are intentionally separate from endpoint URLs. Endpoint base paths and mountpoints are already-serialized URL path text; the library does not guess whether caller input should be percent-encoded or decoded.

## Media boundary

Listening:

```text
Icecast -> encoded bytes -> icecast-cxx -> caller decoder/demuxer -> decoded media
```

Publishing:

```text
caller media -> caller encoder/muxer -> encoded bytes -> icecast-cxx -> Icecast
```

Container-native metadata stays in the encoded stream. ICY metadata framing is an Icecast protocol concern and is separated by `icecast::stream` when present.

## Building the project

CMake 3.24+ is the primary build system and C++20 is the language baseline.

The repository developer gateway is:

```console
python build.py
```

For a native build, `build.py` prepares the pinned libcurl source under the git-ignored `dependencies/` directory, then passes that source path to CMake. CMake reuses it rather than fetching a second copy.

Useful commands include:

```console
python build.py --help
python build.py --clean
python build.py --configuration Release
python build.py --offline
python build.py --configure-only
python build.py --no-tests
```

Python is **not** required to consume `icecast-cxx` from another CMake project.

### Dependency resolution

Dependencies are conditional on the enabled component. `core` and `stream` do not require libcurl; `transport_curl` does.

For libcurl, CMake resolves dependencies in this order:

1. an already-defined compatible `CURL::libcurl` target;
2. `ICECAST_CXX_CURL_SOURCE_DIR`;
3. the curl checkout under `ICECAST_CXX_DEPENDENCIES_DIR`;
4. an installed CMake config package;
5. pkg-config on supported Unix-like systems;
6. CMake's normal `FindCURL` package discovery;
7. pinned `FetchContent`, when permitted;
8. an actionable configuration error if fetching is disabled.

`ICECAST_CXX_FETCH_DEPENDENCIES` and `ICECAST_CXX_FETCH_CURL` both default to `ON`. Set either to `OFF` when automatic curl downloads are not desired.

The authoritative curl repository/revision is stored once in `dependencies.json`; both `build.py` and CMake's FetchContent fallback read that manifest.

The currently pinned source is curl 8.21.0 at commit `9187ef7ec8a8d5650adb8ca6b89a5800e94fba26`.

When icecast-cxx builds its own curl source, it builds a small HTTP-only libcurl configuration. Windows uses Schannel. Non-Windows source builds currently use OpenSSL as curl's TLS provider, so the corresponding development package/toolchain support must be available. This transitive TLS-provider provisioning can be improved separately without changing the public `icecast::transport_curl` API.

## Consuming with CMake

### FetchContent

```cmake
include(FetchContent)

set(ICECAST_CXX_ENABLE_TRANSPORT_CURL ON CACHE BOOL "")

FetchContent_Declare(
    icecast_cxx
    GIT_REPOSITORY https://github.com/mgorn/icecast-cxx.git
    GIT_TAG <immutable-release-tag-or-commit>
)

FetchContent_MakeAvailable(icecast_cxx)

target_link_libraries(my_app PRIVATE icecast::transport_curl)
```

Heavyweight backend targets default off when `icecast-cxx` is embedded, so explicitly enable the backend your parent project needs.

### Git submodule

```console
git submodule add https://github.com/mgorn/icecast-cxx.git external/icecast-cxx
```

```cmake
set(ICECAST_CXX_ENABLE_TRANSPORT_CURL ON CACHE BOOL "")
add_subdirectory(external/icecast-cxx)
target_link_libraries(my_app PRIVATE icecast::transport_curl)
```

### Downloaded source/ZIP

```cmake
set(ICECAST_CXX_ENABLE_TRANSPORT_CURL ON CACHE BOOL "")
add_subdirectory(external/icecast-cxx)
target_link_libraries(my_app PRIVATE icecast::transport_curl)
```

The source tree does not require Git metadata merely to configure.

### Installed package

```cmake
find_package(icecast-cxx CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE icecast::transport_curl)
```

Installed packages never run FetchContent in downstream projects. If the installed package includes `transport_curl`, its package config resolves an installed libcurl dependency.

## Build options

Important cache variables currently include:

- `ICECAST_CXX_BUILD_TESTS`
- `ICECAST_CXX_INSTALL`
- `ICECAST_CXX_FETCH_DEPENDENCIES`
- `ICECAST_CXX_FETCH_CURL`
- `ICECAST_CXX_DEPENDENCIES_DIR`
- `ICECAST_CXX_CURL_SOURCE_DIR`
- `ICECAST_CXX_ENABLE_STREAM`
- `ICECAST_CXX_ENABLE_ADMIN`
- `ICECAST_CXX_ENABLE_TRANSPORT_CURL`
- `ICECAST_CXX_ENABLE_PUBLISH_LIBSHOUT`
- `ICECAST_CXX_ENABLE_TRANSPORT_WEB`

Top-level native builds enable the native backend targets by default. Embedded builds leave heavyweight backends off unless the parent opts in. Tests and install rules also default on only for top-level builds.

## Security behavior

The native listener backend intentionally keeps conservative defaults:

- TLS peer certificate verification is enabled;
- TLS hostname verification is enabled;
- an HTTPS listener cannot redirect down to HTTP;
- credentials are not embedded in URLs;
- credentials are not forwarded to unrelated redirect hosts by enabling unrestricted auth;
- transport protocols are restricted to HTTP/HTTPS;
- `Accept-Encoding: identity` is managed so servers are asked not to add HTTP content coding around encoded media bytes.

Basic authentication is the current V1 authentication mechanism.

## Platforms

Planned first-class platforms are Windows, macOS, Linux, and Emscripten/WebAssembly.

`icecast::transport_curl` is native-only. Browser networking will use a separate Fetch-based backend rather than POSIX socket emulation.

## Licensing

Original `icecast-cxx` code is licensed under the [MIT License](LICENSE).

Third-party dependencies retain their own licenses. libcurl uses the curl license and remains an implementation dependency of the native transport target.

Do not copy GPL-licensed Icecast server implementation code into this project. The server source may be studied as a behavioral reference while client-facing code is independently implemented.

## Other build systems

CMake is the only first-party build system currently planned.

If you need Meson, Bazel, another build system, or additional package-manager integration, please open an issue or submit a pull request. New build-system support should preserve the same target boundaries and optional-dependency behavior.

## Contributors and coding agents

Project guidance lives in [`AGENTS.md`](AGENTS.md) and [`docs/agents/`](docs/agents/). Keep those documents synchronized with externally visible API and build behavior.
