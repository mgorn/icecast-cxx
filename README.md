# icecast-cxx

A modern, modular C++ interface for interacting with Icecast servers.

> **Project status:** architecture and specification phase. The public C++ API, CMake targets, dependency versions, and `build.py` workflow described below are the intended developer experience and are not implemented yet.

`icecast-cxx` exists to make Icecast pleasant to use from modern C++. The project should provide the conveniences C++ developers expect on top of Icecast's client-facing protocols and useful Xiph components: strong value types, explicit ownership, RAII for active resources, structured errors, modular targets, predictable build integration, and platform-aware behavior.

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

## Planned architecture

The current design is centered around small public modules with separately linkable platform/backend components. Candidate CMake targets are:

- `icecast::core`
  - endpoints and mountpoints
  - credentials
  - protocol/header models
  - metadata
  - structured errors/results
  - capability models
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

The exact target split may still be refined before the first implementation. The modularity requirement is intentional: an application that only listens should not be forced to build or link publishing or administrative dependencies.

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

## Build and dependency philosophy

CMake is the primary build system.

Dependencies are **conditional requirements**, not unconditional project requirements. For example, a native publishing backend may require libshout, while a consumer that only builds `icecast::core` should not need libshout at all.

For every dependency required by an enabled component, CMake should prefer dependencies already supplied by the developer and only download what is missing.

The intended dependency resolution order is:

1. Reuse an already-defined compatible CMake target from the parent project.
2. Honor an explicit dependency source/package hint supplied by the developer or by `build.py`.
3. Reuse a compatible dependency checkout already present under `dependencies/`.
4. Discover a compatible installed/system package when available.
5. If the dependency is still missing and its download option is enabled, download a pinned immutable revision with CMake `FetchContent`.
6. If fetching is disabled, fail with an actionable message explaining what dependency is missing and how to provide it.

Fetch options for missing dependencies should default to **ON**. Developers who want a completely network-free CMake configure must be able to explicitly disable fetching and provide all required dependencies themselves.

Dependencies fetched by the project must be pinned to immutable releases or commits rather than moving branches.

The project should also provide a global convenience option to disable automatic dependency downloads, while retaining per-dependency controls for advanced builds. Exact option names will be frozen when the first CMake implementation is designed.

### `build.py`

The repository will provide a cross-platform Python script named `build.py` as the gateway for developers who want to build the full project without manually orchestrating dependencies.

The intended default workflow is:

```console
python build.py
```

`build.py` should:

1. download the pinned dependency sources needed by the requested build into `dependencies/`;
2. configure CMake so those local dependency sources are reused rather than downloaded again by `FetchContent`;
3. configure the requested `icecast-cxx` modules/backends;
4. build the project;
5. provide clear diagnostics and a useful `--help` interface.

`dependencies/` is repository-local build state and must be git-ignored.

Python is **not** intended to be required for normal CMake consumption of `icecast-cxx`. It is a convenience gateway for building this repository itself.

## Consuming with CMake

The following examples describe the intended integration interface once the first implementation lands.

### FetchContent

This should be the easiest zero-configuration integration for many CMake projects:

```cmake
include(FetchContent)

FetchContent_Declare(
    icecast_cxx
    GIT_REPOSITORY https://github.com/mgorn/icecast-cxx.git
    GIT_TAG <immutable-release-tag-or-commit>
)

FetchContent_MakeAvailable(icecast_cxx)

target_link_libraries(my_app PRIVATE icecast::core icecast::stream icecast::transport_curl)
```

A released tag or immutable commit should be used instead of a moving branch for reproducible builds.

### Git submodule

```console
git submodule add https://github.com/mgorn/icecast-cxx.git external/icecast-cxx
```

```cmake
add_subdirectory(external/icecast-cxx)
target_link_libraries(my_app PRIVATE icecast::core icecast::stream icecast::transport_curl)
```

### Downloaded source or ZIP

A downloaded release archive should work like any normal CMake subdirectory:

```cmake
add_subdirectory(external/icecast-cxx)
target_link_libraries(my_app PRIVATE icecast::core icecast::stream icecast::transport_curl)
```

The project must not assume it lives at the top level of the consuming build. Development-only targets such as tests, examples, and internal tools should not unexpectedly become part of a parent project's default build.

### Installed package

Install/export support is planned so packaged installations can eventually use:

```cmake
find_package(icecast-cxx CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE icecast::core icecast::stream icecast::transport_curl)
```

The installed package should export namespaced CMake targets without requiring consumers to know which third-party libraries implement them internally.

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

Concrete C++ usage examples will be added after the public API is designed. Documentation should not invent unstable API names merely to make an early README look complete.

## Platform direction

Planned first-class platforms are:

- Windows
- macOS
- Linux
- Emscripten/WebAssembly in supported browsers

Native and browser implementations should share public Icecast semantics and models while allowing materially different transport implementations underneath.

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

During the current specification phase, implementation should not introduce foundational API, dependency, licensing, threading, or transport decisions ad hoc. Those decisions should be documented and reviewed first.
