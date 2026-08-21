# icecast-cxx

A modern, modular C++ interface for interacting with Icecast servers.

> **Project status:** early implementation. `icecast::core`, transport-independent `icecast::stream`, the native libcurl listener backend, and the native libshout publisher backend are implemented. Administration and browser/WebAssembly transport remain under development.

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
  - explicit listener pause/stop backpressure
  - bounded publisher write results (`accepted`, `would_block`, `closed`)
  - allocation-free media delivery through the ICY demultiplexer
  - raw and best-effort-parsed ICY metadata
- `icecast::transport_curl`
  - native HTTP/HTTPS listener transport using libcurl
  - Basic authentication, TLS verification, redirect controls, ICY metadata, pause/resume/stop, and listener reconnect
  - caller-driven polling with no hidden worker thread
- `icecast::publish_libshout`
  - native source publishing using libshout
  - already encoded/muxed byte input
  - bounded buffering and partial acceptance with explicit `would_block`
  - graceful drain/finish and immediate abort
  - HTTP/HTTPS source connections and Basic source authentication
  - stream metadata/public-directory configuration
  - explicit `interrupted` state on an established connection failure; no blind publisher reconnect

The following targets currently remain placeholders:

- `icecast::admin`
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

`curl_context::poll()` drives network progress. There is no hidden thread, and listener callbacks execute on the polling thread.

A media or metadata callback can return `icecast::stream_action::pause`. Call `listener.resume()` when the consumer is ready and continue polling. The curl adapter keeps at most one bounded libcurl receive tail while paused; it does not create an unbounded application queue.

Each successful connection increments `listener_status::connection_generation`, including successful reconnects. Callers can use that generation to reset a decoder when a new physical stream connection begins.

## Native publisher quick start

```cpp
#include <icecast/publish_libshout.hxx>

#include <cstddef>
#include <iostream>
#include <span>

int main() {
    auto context_result = icecast::libshout_context::create();
    if (not context_result) {
        std::cerr << context_result.error().message << '\n';
        return 1;
    }
    auto context = std::move(context_result).value();

    icecast::publisher_config config;
    config.endpoint.scheme = icecast::endpoint_scheme::https;
    config.endpoint.host = "radio.example.com";
    config.mount.path = "/live.ogg";
    config.credentials = {"source", "secret"};
    config.content_type = "audio/ogg";

    auto publisher_result = context.publish(std::move(config));
    if (not publisher_result) {
        std::cerr << publisher_result.error().message << '\n';
        return 1;
    }
    auto publisher = std::move(publisher_result).value();

    // `encoded_bytes` must already contain the codec/container stream.
    std::span<const std::byte> encoded_bytes;
    auto write = publisher.write(encoded_bytes);
    if (write and (write.value().status == icecast::publisher_write_status::would_block)) {
        // Keep the unaccepted tail and call context.poll() until capacity is available.
    }

    // Call poll regularly while producing bytes. When the logical stream is complete:
    if (auto result = publisher.finish(); not result) {
        return 1;
    }
    while (not publisher.finished()) {
        if (auto result = context.poll(); not result) {
            return 1;
        }
    }
}
```

`publisher.write()` never silently drops bytes. It reports how many bytes were accepted into the bounded publication buffer. A `would_block` result means the caller retains and retries the unaccepted tail after driving `libshout_context::poll()`.

`finish()` is graceful: queued/application and libshout-owned bytes are drained before the source connection closes. `abort()` stops immediately. If an established source connection fails, the publisher enters `publisher_state::interrupted`; the backend does not reconnect and continue in the middle of the old encoded/container stream.

The libshout backend currently maps the MIME types libshout 2.4.x can represent directly: `audio/mpeg`, `audio/ogg`, `video/ogg`, `application/ogg`, `audio/webm`, `video/webm`, `audio/x-matroska`, and `video/x-matroska`. Other MIME types fail explicitly with `error_category::unsupported`. Arbitrary custom source request headers are also not exposed by libshout and therefore fail explicitly in this backend.

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

This prints `https://radio.example.com/icecast/live.ogg`. Credentials remain separate from endpoint URLs.

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

For a native developer build, `build.py` prepares the pinned native dependency sources under the git-ignored `dependencies/` directory and passes those source paths to CMake. Git dependencies are pinned to exact commits; release archives are pinned by SHA-256.

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

Dependencies are conditional on the enabled component. `core` and `stream` have no native transport dependency. `transport_curl` requires libcurl; `publish_libshout` requires libshout >= 2.4.4.

CMake prefers a compatible target supplied by the parent, then explicit/repository-local source, then installed packages, and only then an automatic pinned source fallback. Setting `ICECAST_CXX_FETCH_DEPENDENCIES=OFF` makes configuration network-free.

Important backend-specific controls are:

- `ICECAST_CXX_FETCH_CURL`
- `ICECAST_CXX_CURL_SOURCE_DIR`
- `ICECAST_CXX_FETCH_LIBSHOUT`
- `ICECAST_CXX_LIBSHOUT_SOURCE_DIR`

The authoritative source pins live in `dependencies.json`. The current native pins are curl 8.21.0 at commit `9187ef7ec8a8d5650adb8ca6b89a5800e94fba26` and the official libshout 2.4.6 release archive with its SHA-256 checksum.

libshout upstream uses autotools rather than CMake. When icecast-cxx must build the pinned libshout source itself on a Unix-like host, CMake uses an isolated `ExternalProject` build and links the resulting shared libshout privately. That source build still needs libshout's normal build prerequisites (notably a C toolchain, make/pkg-config, and libogg development support; TLS support additionally depends on the upstream libshout TLS prerequisites). On Windows, automatic autotools source building is not currently provided; supply a compatible `Shout::shout` target/package instead.

## Consuming with CMake

### FetchContent

```cmake
include(FetchContent)

set(ICECAST_CXX_ENABLE_TRANSPORT_CURL ON CACHE BOOL "")
set(ICECAST_CXX_ENABLE_PUBLISH_LIBSHOUT ON CACHE BOOL "")

FetchContent_Declare(
    icecast_cxx
    GIT_REPOSITORY https://github.com/mgorn/icecast-cxx.git
    GIT_TAG <immutable-release-tag-or-commit>
)

FetchContent_MakeAvailable(icecast_cxx)

target_link_libraries(my_app PRIVATE icecast::transport_curl icecast::publish_libshout)
```

Heavyweight backend targets default off when `icecast-cxx` is embedded, so explicitly enable only the backends your parent project needs.

### Git submodule or downloaded source

```cmake
set(ICECAST_CXX_ENABLE_TRANSPORT_CURL ON CACHE BOOL "")
set(ICECAST_CXX_ENABLE_PUBLISH_LIBSHOUT ON CACHE BOOL "")
add_subdirectory(external/icecast-cxx)
target_link_libraries(my_app PRIVATE icecast::transport_curl icecast::publish_libshout)
```

The source tree does not require Git metadata merely to configure.

### Installed package

```cmake
find_package(icecast-cxx CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE icecast::transport_curl icecast::publish_libshout)
```

Installed packages never download dependencies. If the installed package contains `transport_curl` and/or `publish_libshout`, the downstream environment must provide the corresponding installed development dependency (`CURL::libcurl` or a compatible libshout/shout pkg-config package / `Shout::shout` target).

## Build options

Important cache variables currently include:

- `ICECAST_CXX_BUILD_TESTS`
- `ICECAST_CXX_INSTALL`
- `ICECAST_CXX_FETCH_DEPENDENCIES`
- `ICECAST_CXX_FETCH_CURL`
- `ICECAST_CXX_FETCH_LIBSHOUT`
- `ICECAST_CXX_DEPENDENCIES_DIR`
- `ICECAST_CXX_CURL_SOURCE_DIR`
- `ICECAST_CXX_LIBSHOUT_SOURCE_DIR`
- `ICECAST_CXX_ENABLE_STREAM`
- `ICECAST_CXX_ENABLE_ADMIN`
- `ICECAST_CXX_ENABLE_TRANSPORT_CURL`
- `ICECAST_CXX_ENABLE_PUBLISH_LIBSHOUT`
- `ICECAST_CXX_ENABLE_TRANSPORT_WEB`

Top-level native builds enable the native backend targets by default. Embedded builds leave heavyweight backends off unless the parent opts in. Tests and install rules also default on only for top-level builds.

## Security behavior

The native listener keeps TLS peer/hostname verification enabled, restricts protocols to HTTP/HTTPS, prevents HTTPS downgrade redirects, and does not place credentials in URLs.

The native publisher uses libshout's HTTP source protocol and Basic source authentication. HTTPS endpoints request libshout's RFC2818 TLS mode; certificate/hostname validation remains libshout's responsibility. Credentials remain separate from endpoint URLs.

## Platforms

Planned first-class platforms are Windows, macOS, Linux, and Emscripten/WebAssembly.

`icecast::transport_curl` and `icecast::publish_libshout` are native-only. Browser networking will use a separate Fetch-based backend rather than POSIX socket emulation. Automatic libshout source building is currently Unix-like only; Windows consumers can use a prebuilt/package-managed libshout target.

## Licensing

Original `icecast-cxx` code is licensed under the [MIT License](LICENSE).

Third-party dependencies retain their own licenses. libcurl uses the curl license. libshout is distributed under the GNU Library General Public License version 2 or later; it remains isolated behind the optional `icecast::publish_libshout` target and is not copied into this repository.

Do not copy GPL-licensed Icecast server implementation code into this project. The server source may be studied as a behavioral reference while client-facing code is independently implemented.

## Other build systems

CMake is the only first-party build system currently planned.

If you need Meson, Bazel, another build system, or additional package-manager integration, please open an issue or submit a pull request. New build-system support should preserve the same target boundaries and optional-dependency behavior.

## Contributors and coding agents

Project guidance lives in [`AGENTS.md`](AGENTS.md) and [`docs/agents/`](docs/agents/). Keep those documents synchronized with externally visible API and build behavior.
