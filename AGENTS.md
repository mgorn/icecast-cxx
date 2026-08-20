# Coding agent guidance

This file applies to the entire `icecast-cxx` repository.

`icecast-cxx` is in early implementation. The build/package scaffold, `icecast::core`, transport-independent `icecast::stream`, and the native libcurl listener backend are real implementation. Administration, source publishing, and browser networking remain future layers.

## Before changing the project

1. Read [`README.md`](README.md) for current status and developer-facing usage.
2. Read [`docs/agents/architecture.md`](docs/agents/architecture.md) before changing module boundaries, media/protocol responsibility, transport strategy, threading, or ownership.
3. Read [`docs/agents/core-api.md`](docs/agents/core-api.md) before changing core value/error/capability models.
4. Read [`docs/agents/stream-api.md`](docs/agents/stream-api.md) before changing stream configuration, state, reconnect, backpressure, or ICY behavior.
5. Read [`docs/agents/transport-curl.md`](docs/agents/transport-curl.md) before changing native listener networking or libcurl integration.
6. Read [`docs/agents/build-system.md`](docs/agents/build-system.md) before changing CMake, dependencies, packaging, `dependencies.json`, or `build.py`.
7. Read [`docs/agents/cpp-style.md`](docs/agents/cpp-style.md) before writing or reviewing C++.

When a task conflicts with these documents, do not silently choose a new architecture. Surface the conflict and update the documented decision deliberately when the change is accepted.

## Architectural invariants

- This is a client-facing Icecast networking/protocol library, not an audio engine.
- Codecs, PCM processing, audio devices, media graphs, general mux/demux services, and codec/container pacing stay outside the library.
- Public semantic APIs do not expose libcurl, libshout, Emscripten, or Icecast server-internal types.
- Native and browser transports may differ internally while sharing genuinely portable stream semantics.
- Consumers only acquire backend dependencies for the backends they enable.
- Listener transports reuse `icecast::stream` state, reconnect, ICY, and backpressure semantics rather than reimplementing them.
- No hidden worker thread per listener. Native curl progress is caller-driven through `curl_context::poll()`.
- Do not copy GPL-licensed Icecast server implementation code.

## Developer experience

Treat developer convenience as a primary design constraint. Prefer strong defaults, clear ownership, actionable errors, normal modern CMake integration, predictable callback context, and escape hatches that do not leak backend internals.

## Repository changes

Keep changes focused. Do not commit fetched dependencies or build artifacts. `dependencies/` is git-ignored local state.

Update README/agent guidance with externally visible behavior. New dependencies must be pinned in `dependencies.json`; do not create an independent version constant elsewhere.
