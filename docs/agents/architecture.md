# Architecture guidance

This document records the current architectural baseline for `icecast-cxx`. It is intended to prevent foundational design decisions from being invented ad hoc during implementation.

The project is still in the specification phase. Where this document says a component is "planned", it describes an intended boundary rather than existing code.

## Project role

`icecast-cxx` is a modern C++ client-facing networking/protocol library for interacting with Icecast servers.

It should provide C++ conveniences over Icecast behavior while reusing suitable Xiph components where technically and legally appropriate. It should **not** expose Icecast server-global structures or attempt to wrap the whole server implementation.

Primary expected consumers include:

- native and WebAssembly/browser Station UI clients for listening and, where browser capabilities permit, source publishing;
- native and WebAssembly/browser AudIDE builds for encoded stream input/output;
- the future separate `pycecast` repository, which will bind the complete C++ API for Python use, including administration.

`icecast-cxx` itself must not depend on Python.

## Media/protocol boundary

The library deals primarily in **encoded media bytes** plus Icecast/ICY protocol metadata.

Listening:

```text
Icecast -> encoded bytes -> icecast-cxx -> caller decoder/demuxer -> decoded media
```

Publishing:

```text
caller source -> caller encoder/muxer -> encoded bytes -> icecast-cxx -> Icecast
```

Unless a narrowly scoped protocol requirement proves otherwise, keep the following outside `icecast-cxx`:

- codec encode/decode policy;
- PCM processing;
- audio device input/output;
- media graphs;
- sample-rate/channel conversion;
- container demuxing/muxing as a general media service;
- media-file pacing based on codec/container timestamps.

ICY metadata framing is an Icecast streaming concern and belongs in the stream layer. Container-native metadata remains part of the encoded media stream unless explicitly handled by another media library.

## Planned public modules

The current logical decomposition is:

### `icecast::core`

Common dependency-light models and protocol concepts, such as:

- server/base endpoints;
- mountpoints;
- credentials;
- headers/protocol models;
- metadata models;
- structured result/error types;
- platform/server/session capability models;
- connection/status models shared across higher-level components.

`core` should avoid heavyweight transport dependencies.

### `icecast::stream`

Common stream semantics, including:

- listener session behavior;
- source/publisher session behavior;
- stream authentication semantics;
- ICY metadata framing/parsing;
- stream-level metadata;
- reconnection policy/state;
- bounded buffering and backpressure contracts.

`stream` describes Icecast streaming behavior. It should not require a specific native or browser transport in its public interface.

### `icecast::admin`

Typed administrative operations and response models, including common server/mount statistics and listener/source operations.

The intended direction is typed coverage for common operations plus a controlled lower-level escape hatch for valid Icecast admin endpoints that do not yet have first-class typed wrappers.

`admin` must remain separable so frontend consumers do not have to link or expose administrative functionality.

## Planned backend separation

Backends should be separately linkable and hidden from public semantic models.

Current leading choices are:

- native HTTP/HTTPS listening/admin: libcurl behind a transport target;
- native source publishing: libshout behind a separate publishing backend;
- browser/WebAssembly: browser networking/Fetch semantics rather than POSIX socket emulation.

Do not expose `CURL*`, `shout_t*`, Emscripten headers, browser JavaScript implementation details, or other backend-specific types in the public `core`, `stream`, or `admin` API.

The public architecture must not become a C++ wrapper around libshout specifically. Libshout may implement native publishing while the public publisher contract remains project-owned.

Likewise, `icecast-common/httpp` is currently considered reference material rather than a default runtime dependency. Any proposal to make an `icecast-common` component a direct dependency should justify both technical need and licensing implications.

## Public type philosophy

Use simple value-oriented `struct` types for public configuration, metadata, status, and result models where practical.

Active networking sessions are a justified exception to an entirely open-data model: they own platform resources and maintain invariants. Their implementation state should remain opaque rather than exposing mutable backend handles simply to avoid encapsulation.

Avoid unnecessary runtime polymorphism in public APIs. Prefer concrete types, composition, concepts/templates where they improve usability, and variants where a finite set of alternatives is appropriate.

Do not introduce a public virtual transport hierarchy merely to make native and browser implementations look identical.

## Listener semantics

A listener represents one logical Icecast listening session and should deliver:

- encoded media byte chunks;
- normalized stream/header information;
- separate ICY metadata events when requested and available;
- lifecycle/state changes;
- structured failures.

Low-copy receive paths should make borrowed-buffer lifetime explicit. Per-chunk heap allocation should not be required by the public contract.

Backpressure must be explicit and buffering bounded. A slow consumer must not cause silent unbounded queue growth.

Automatic listener reconnection is appropriate as a first-class policy. A successful reconnect should be distinguishable as a new connection generation so callers can reset decoders or other state when necessary.

## Publisher semantics

A publisher accepts an already encoded/muxed byte stream from the caller.

The caller remains responsible for producing valid media/container data and for media timing. The library is responsible for publishing those bytes subject to transport backpressure and server behavior.

A publisher must not silently drop data when an outgoing queue fills. Backpressure or `would_block`-style behavior should be visible to the caller.

Prefer a simple convenience path that copies borrowed caller data into a bounded queue plus an ownership-transfer path for avoiding an additional payload copy. Avoid complicated externally owned asynchronous buffer lifetimes until profiling demonstrates a need.

Do not implement blind publisher auto-reconnect that continues midway through an encoded logical stream on a fresh connection. Some containers require a new logical stream/header sequence after reconnect. V1 should report publisher interruption clearly; a later reconnection design can explicitly coordinate a new publication generation with the caller.

## Metadata

ICY metadata handling belongs in `stream` and should separate metadata framing from media bytes.

Do not assume historical ICY metadata bytes are always valid UTF-8. Preserve enough raw information for callers to apply an encoding policy when necessary.

Administrative metadata updates may be exposed from both an active publisher context and the administrative API when that is the natural user-facing operation. Shared implementation does not imply a public dependency from `stream` to `admin`.

## Authentication and security

HTTP Basic authentication is the expected V1 baseline for listener/source/admin roles, but role-specific credential models may still improve API clarity.

Security defaults should be conservative:

- TLS certificate verification enabled;
- hostname verification enabled;
- credentials excluded/redacted from logs and diagnostics;
- credentials not embedded in URLs;
- redirects handled carefully, especially for credential forwarding and live publishing requests.

The API should remain extensible for future authentication methods without making credentials a bag of backend-specific curl options.

## Native and browser differences

Do not pretend WebAssembly/browser networking has native socket capabilities.

Native and browser implementations should share public semantics where those semantics are genuinely portable, while exposing capability information when the environment imposes meaningful restrictions.

At minimum, distinguish:

- platform capabilities;
- observed server capabilities;
- effective/session capabilities resulting from both.

Browser publishing should be capability-dependent rather than guaranteed. CORS, mixed-content/security policy, streaming request-body support, redirects, and browser HTTP behavior may make direct publishing unavailable even when native publishing works.

## Threading and asynchronous behavior

The current preferred direction is:

- internally nonblocking/session-oriented operations;
- no hidden worker thread per stream by default;
- an explicit native execution/context mechanism that applications may drive themselves or run on a thread they own;
- predictable callback execution context;
- native blocking convenience adapters only where they improve usability;
- browser operations integrated with browser event-loop semantics rather than fake blocking APIs.

Do not introduce a custom coroutine framework or global thread pool without a separate design decision.

## Error model

Backend-specific integer error codes should not define the public API.

Prefer a structured value-oriented result/error model that can represent categories such as configuration, cancellation, timeout, resolution/connection/TLS failures, authentication, HTTP/protocol errors, server rejection, unsupported capabilities, browser policy failures, and backpressure.

Backend error codes may be retained as diagnostics without becoming the stable error taxonomy.

## Python bindability

Design the C++ API for C++ first, but avoid unnecessary constructs that make bindings difficult.

The expected first Python binding strategy is a separate `pycecast` project using pybind11 unless a later requirement justifies a stable C ABI.

Do not add Python dependencies or Python-specific behavior to `icecast-cxx` merely to simplify bindings.

## Compatibility direction

The intended initial compatibility focus is modern Icecast 2.5.x behavior with sensible support for Icecast 2.4.4 where practical.

Older protocol compatibility should be added because a real compatibility requirement justifies it, not simply because historic server code exists.

## Licensing boundary

The repository's original code is MIT licensed.

Do not copy GPL-licensed Icecast server implementation code. It may be studied as an authoritative behavior reference.

Third-party components retain their own licenses. Any direct dependency or incorporated source must be reviewed for static/shared distribution obligations, source/relinking requirements where applicable, and future Python wheel distribution.
