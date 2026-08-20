# Agent documentation

Focused guidance for contributors and coding agents:

- [`architecture.md`](architecture.md) — project/module boundaries and long-lived architectural invariants.
- [`core-api.md`](core-api.md) — foundational endpoint, credential, header, result/error, and capability models.
- [`stream-api.md`](stream-api.md) — listener/publisher semantic models, reconnect/state, backpressure, and ICY framing.
- [`transport-curl.md`](transport-curl.md) — implemented native listener transport and libcurl-specific invariants.
- [`build-system.md`](build-system.md) — CMake integration, dependency resolution, `dependencies.json`, `build.py`, installation, and embedding.
- [`cpp-style.md`](cpp-style.md) — C++ source/API conventions.

The root [`AGENTS.md`](../../AGENTS.md) is the entry point. Implemented behavior in these files is a compatibility surface; future-looking sections are design constraints, not permission to invent unrelated implementation.
