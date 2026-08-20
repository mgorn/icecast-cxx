# Agent documentation

This directory contains focused project guidance for coding agents and contributors working on `icecast-cxx`.

- [`architecture.md`](architecture.md) — architectural boundaries, responsibilities, and protocol/media separation.
- [`core-api.md`](core-api.md) — implemented foundational C++ models, validation rules, results/errors, and capabilities.
- [`stream-api.md`](stream-api.md) — implemented transport-independent stream models, listener state/reconnect behavior, backpressure, and ICY demultiplexing.
- [`build-system.md`](build-system.md) — CMake integration, dependency discovery/fetching, `build.py`, packaging, and embedding requirements.
- [`cpp-style.md`](cpp-style.md) — C++ source style and public-API design conventions.

The root [`AGENTS.md`](../../AGENTS.md) is the entry point and defines when these documents apply.

Specification-oriented sections still describe future administration and transport/publishing backends. `core-api.md`, `stream-api.md`, and implemented portions of the build-system documentation describe code that exists and must stay synchronized with behavior.
