# Coding agent guidance

This file applies to the entire `icecast-cxx` repository.

`icecast-cxx` is currently in the **early implementation phase**. The build/package scaffold exists, while the public C++ API and Icecast networking implementation are still being designed and added incrementally. Read the project README and the focused guidance under [`docs/agents/`](docs/agents/) before making changes.

## Before changing the project

1. Read [`README.md`](README.md) for the intended developer experience and current implementation status.
2. Read [`docs/agents/architecture.md`](docs/agents/architecture.md) before changing public boundaries, module responsibilities, networking semantics, threading, ownership, or platform behavior.
3. Read [`docs/agents/build-system.md`](docs/agents/build-system.md) before changing CMake, dependencies, packaging, `build.py`, or platform configuration.
4. Read [`docs/agents/cpp-style.md`](docs/agents/cpp-style.md) before writing or reviewing C++.

When a task conflicts with these documents, do not silently pick a new architecture. Call out the conflict and update the specification deliberately if the requested change is accepted.

## Current phase

Implementation should proceed in explicitly scoped layers. A documented future target, option, or API is not by itself permission to invent the implementation behind it.

The repository currently has a CMake/package scaffold, namespaced INTERFACE target placeholders, a dependency manifest, and the `build.py` developer gateway. Those pieces are real implementation and should be kept working as later modules become compiled libraries.

Foundational decisions should not be made ad hoc while coding. In particular, discuss and document changes involving:

- public API shape;
- CMake target/module boundaries;
- native or browser transport strategy;
- dependency additions or removals;
- dependency licensing/distribution implications;
- threading or asynchronous execution models;
- buffer ownership/backpressure semantics;
- authentication/security behavior;
- compatibility guarantees;
- stable ABI or Python-binding constraints.

## Architectural invariants

- This is a client-facing Icecast networking/protocol library, not an audio engine.
- Codecs, audio devices, PCM processing, media graph behavior, and general-purpose media pacing stay outside the library unless explicitly justified by protocol requirements.
- Icecast server internals and server-global C structures are not the public abstraction.
- Native and browser implementations may differ internally; the public API should share semantics where they are genuinely common rather than pretending WebAssembly has native sockets.
- Public modules should not expose libcurl, libshout, Emscripten, or other backend implementation types in their headers.
- Consumers should only build/link the modules and backend dependencies they need.
- Original project code must not copy GPL-licensed Icecast server implementation code.

## Developer experience priority

Treat developer convenience as a primary design constraint, not polish to add later.

Prefer APIs and build behavior that:

- make the common path short and unsurprising;
- provide strong defaults without removing control;
- make ownership and lifetime explicit;
- produce actionable errors;
- avoid leaking C implementation details;
- compose naturally with ordinary modern CMake projects;
- allow advanced consumers to provide and manage dependencies themselves.

## Repository changes

Keep changes focused. Do not add unrelated formatting, generated files, fetched dependencies, or build artifacts to commits.

`dependencies/` is repository-local build state populated by `build.py` and must remain git-ignored.

Update documentation together with externally visible behavior so the README and agent guidance remain useful as specifications rather than historical notes.
