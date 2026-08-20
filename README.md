# icecast-cxx

A modern, modular C++ interface for interacting with Icecast servers.

> **Project status:** early implementation phase. The build/package scaffold and foundational `icecast::core` API are implemented. Streaming, administration, native/browser transports, and source publishing are still under development and should not yet be treated as working Icecast networking functionality.

`icecast-cxx` exists to make Icecast pleasant to use from modern C++. The project provides the conveniences C++ developers expect on top of Icecast's client-facing protocols and useful Xiph components: strong value types, explicit ownership, RAII for active resources, structured errors, modular targets, predictable build integration, and platform-aware behavior.

The library is a networking/protocol library, **not an audio engine**. It deals primarily in encoded media bytes and Icecast stream metadata. Encoding, decoding, muxing, demuxing, audio devices, and realtime PCM processing belong in the calling application or other libraries.

## Goals

- Provide an idiomatic C++ interface instead of exposing Icecast server internals or raw C APIs.
- Make common Icecast operations convenient without hiding important ownership, authentication, backpressure, or platform constraints.
- Keep listening, publishing, administration, and platform backends modular so consumers only link what they use.
- Support native C++ applications and WebAssembly/browser applications without pretending browsers have normal POSIX sockets.
- Keep codec policy outside the library.
- Keep `icecast-cxx` independent of Python while remaining straightforward to bind from the future `pycecast` project.
- Reuse upstream Xiph code where it is technically and legally sensible, but do not blindly wrap or copy the Icecast server implementation.
- Make dependency management friendly to both zero-configuration users and developers who manage dependencies themselves.

## Current implementation

`icecast::core` is now a real compiled C++20 library. It has no third-party runtime dependency and supports normal static/shared CMake builds.

The foundational API currently includes:

- HTTP/HTTPS server endpoints with optional reverse-proxy base paths;
- Icecast mountpoint paths;
- Basic-auth credential values and safe diagnostic redaction;
- duplicate-preserving, case-insensitive HTTP header models;
- project-owned structured errors and `result<T>` / `result<void>`;
- platform, server, and effective capability models.

The remaining component targets currently exist as CMake **INTERFACE placeholders** so the intended consumer-facing target graph can be exercised without pretending their protocol implementations exist yet:

- `icecast::stream`
- `icecast::admin`
- `icecast::transport_curl`
- `icecast::publish_libshout`
- `icecast::transport_web`

Their planned responsibilities are:

- `icecast::stream`
  - listener semantics
  - publisher semantics
  - ICY metadata handling
  - stream state and reconnection policy
  - encoded-byte streaming contracts
- `icecast::admin`
  - server and mount statistics
  - listener/source management
  - move/disconnect operations
  - administrative metadata operations
  - typed coverage of common Icecast admin APIs plus an extension path for less-common endpoints
- `icecast::transport_curl`
  - native HTTP/HTTPS backend for listening and administrative requests
- `icecast::publish_libshout`
  - optional native publishing backend using libshout where appropriate
- `icecast::transport_web`
  - browser/WebAssembly transport using browser networking APIs

The modularity requirement is intentional: an application that only needs a subset of Icecast functionality should not be forced to build or link unrelated backends or dependencies.

## Core API quick start

Use the convenience umbrella header:

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

This prints:

```text
https://radio.example.com/icecast/live.ogg
```

Credentials are deliberately separate from endpoint URLs:

```cpp
icecast::basic_credentials credentials{
    .username = "source",
    .password = "secret",
};

if (not icecast::validate_basic_credentials(credentials)) {
    // Handle invalid local configuration.
}
```

HTTP headers preserve duplicates and use case-insensitive lookup:

```cpp
icecast::headers response_headers;
response_headers.append("Set-Cookie", "a=1");
response_headers.append("set-cookie", "b=2");

auto cookies = response_headers.all("SET-COOKIE");
```

Endpoint base paths and mountpoints are treated as already serialized URL path text. Core does not guess whether path input should be percent-encoded or decoded; callers should provide encoded path text when required.

The public value models remain ordinary `struct`s for convenient direct construction. Parsing and validation helpers are provided for textual/untrusted values and should be used at protocol boundaries.

## Media boundary

Listening:

```text
Icecast
    -> encoded stream bytes
    -> icecast-cxx
    -> caller's decoder/demuxer
    -> PCM or other decoded media
```

Publishing:

```text
PCM or other source media
    -> caller's encoder/muxer
    -> encoded bytes
    -> icecast-cxx
    -> Icecast
```

`icecast-cxx` should not become responsible for Opus, Vorbis, MP3, AAC, Ogg, WebM, audio devices, or realtime graph processing unless some narrowly scoped dependency is genuinely required to interpret Icecast protocol behavior itself.

## Building the project

CMake is the primary build system. C++20 is the current language baseline.

The easiest developer build is:

```console
python build.py
```

`build.py` is the repository gateway for full-project builds. It:

1. reads the pinned dependency manifest in `dependencies.json`;
2. prepares required dependency checkouts beneath the git-ignored `dependencies/` directory;
3. passes those local source paths to CMake so they can be reused instead of fetched again;
4. configures the appropriate project components;
5. builds the project;
6. runs CTest unless tests are disabled.

The manifest is currently empty because `icecast::core` has no third-party dependency and no networking backend dependency has been wired into the build yet.

Useful commands include:

```console
python build.py --help
python build.py --clean
python build.py --configuration Release
python build.py --offline
python build.py --configure-only
python build.py --no-tests
```

`--offline` prevents both `build.py` dependency downloads and the CMake `FetchContent` fallback.

Python is **not** required for normal downstream CMake consumption of `icecast-cxx`. It is a convenience gateway for developers building this repository itself.

## Dependency philosophy

Dependencies are **conditional requirements**, not unconditional project requirements. A native publishing backend may require libshout, for example, while a consumer that only uses `icecast::core` should not need libshout at all.

For every dependency required by an enabled component, the intended CMake resolution order is:

1. Reuse an already-defined compatible CMake target from the parent project.
2. Honor an explicit dependency source/package hint supplied by the developer or by `build.py`.
3. Reuse a compatible dependency checkout already present under `ICECAST_CXX_DEPENDENCIES_DIR`.
4. Discover a compatible installed/system package when available.
5. If the dependency is still missing and downloads are enabled, obtain the pinned immutable revision with CMake `FetchContent`.
6. If fetching is disabled, fail with an actionable message explaining what dependency is missing and how to provide it.

The initial build options are:

- `ICECAST_CXX_FETCH_DEPENDENCIES`
  - defaults to `ON`;
  - permits CMake to download a missing dependency after developer-provided sources/packages have been considered.
- `ICECAST_CXX_DEPENDENCIES_DIR`
  - defaults to `<icecast-cxx source>/dependencies`;
  - points CMake at repository-local dependency source trees.
- `ICECAST_CXX_BUILD_TESTS`
  - defaults to `ON` for a top-level developer checkout and `OFF` when embedded.
- `ICECAST_CXX_INSTALL`
  - defaults to `ON` for a top-level developer checkout and `OFF` when embedded.
- `ICECAST_CXX_ENABLE_STREAM`
- `ICECAST_CXX_ENABLE_ADMIN`
- `ICECAST_CXX_ENABLE_TRANSPORT_CURL`
- `ICECAST_CXX_ENABLE_PUBLISH_LIBSHOUT`
- `ICECAST_CXX_ENABLE_TRANSPORT_WEB`

`core`, `stream`, and `admin` are enabled in the current top-level configuration, although only `core` contains real C++ implementation so far. Native/browser backend targets default on only for the appropriate **top-level** developer build. When `icecast-cxx` is embedded into another project, heavyweight backends default off so a core-only consumer does not unexpectedly acquire curl, libshout, or browser-specific build requirements.

All automatically downloaded dependencies must be pinned to immutable revisions rather than moving branches.

## Consuming with CMake

### FetchContent

For the currently implemented core library:

```cmake
include(FetchContent)

FetchContent_Declare(
    icecast_cxx
    GIT_REPOSITORY https://github.com/mgorn/icecast-cxx.git
    GIT_TAG <immutable-release-tag-or-commit>
)

FetchContent_MakeAvailable(icecast_cxx)

target_link_libraries(my_app PRIVATE icecast::core)
```

Use a released tag or immutable commit instead of a moving branch for reproducible builds.

When real backends gain third-party dependencies, they remain intentionally opt-in for embedded builds through their documented `ICECAST_CXX_ENABLE_*` options.

### Git submodule

```console
git submodule add https://github.com/mgorn/icecast-cxx.git external/icecast-cxx
```

```cmake
add_subdirectory(external/icecast-cxx)
target_link_libraries(my_app PRIVATE icecast::core)
```

### Downloaded source or ZIP

A downloaded release archive works like an ordinary CMake subdirectory:

```cmake
add_subdirectory(external/icecast-cxx)
target_link_libraries(my_app PRIVATE icecast::core)
```

The project does not assume it lives at the top level of the consuming build. Development-only targets such as tests should not unexpectedly become part of a parent project's default build.

### Installed package

The project generates CMake install/export metadata and installs the public `icecast::core` headers/library:

```cmake
find_package(icecast-cxx CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE icecast::core)
```

As real third-party dependencies are introduced, installed-package dependency handling must use normal package/imported-target behavior and must not unexpectedly run `FetchContent` inside the downstream consumer.

## Developer experience principles

The C++ API should make common Icecast operations concise without hiding important semantics.

Public interfaces should prefer:

- strong/value types over loosely related string parameters;
- RAII and explicit lifetime ownership;
- structured results/errors over backend-specific integer codes;
- spans/views for borrowed stream data where lifetime is clear;
- bounded buffering and explicit backpressure rather than unbounded queues;
- capability queries rather than browser/platform checks scattered through applications;
- sensible defaults with explicit overrides;
- public headers that do not expose libcurl, libshout, Emscripten, or other backend implementation types;
- APIs that are pleasant for direct C++ use first while remaining straightforward to bind from Python later.

The implemented core follows those rules with value-oriented models, explicit parsing/validation, backend-neutral error information, and no third-party headers in its public API.

## Platform direction

Planned first-class platforms are:

- Windows
- macOS
- Linux
- Emscripten/WebAssembly in supported browsers

Native and browser implementations should share public Icecast semantics and models while allowing materially different transport implementations underneath.

`build.py --platform web` is reserved for Emscripten builds and expects `emcmake` on `PATH`.

Browser publishing is expected to be capability-dependent because browser streaming request bodies, CORS, mixed-content policy, and HTTP protocol constraints differ from native networking. The public API should report those capabilities rather than promise unsupported behavior.

## Licensing

The original `icecast-cxx` code is licensed under the [MIT License](LICENSE).

Third-party dependencies retain their own licenses. Dependency choice and distribution strategy must be reviewed deliberately, particularly for Xiph components under GNU Library/LGPL-family terms.

Do not copy GPL-licensed Icecast server implementation code into this project. The Icecast server source may be used as an authoritative behavioral reference while the client-facing C++ implementation remains independently designed.

## Other build systems

CMake is the only planned first-party build system for the initial releases.

Support for other build systems is welcome as future work. If you need Meson, Bazel, another build system, or additional packaging integration, please open an issue describing the use case or submit a pull request. New build-system support should preserve the same modularity and dependency-management principles as the CMake build.

## Contributing and coding agents

Project-specific guidance for human contributors and coding agents lives in [`AGENTS.md`](AGENTS.md) and [`docs/agents/`](docs/agents/).

During this early implementation phase, foundational API, dependency, licensing, threading, and transport decisions should continue to be documented and reviewed rather than introduced incidentally while implementing unrelated functionality.
