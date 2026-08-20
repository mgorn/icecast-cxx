# Agent documentation

This directory contains focused project guidance for coding agents and contributors working on `icecast-cxx`.

- [`architecture.md`](architecture.md) — architectural boundaries, responsibilities, and protocol/media separation.
- [`build-system.md`](build-system.md) — CMake integration, dependency discovery/fetching, `build.py`, packaging, and embedding requirements.
- [`cpp-style.md`](cpp-style.md) — C++ source style and public-API design conventions.

The root [`AGENTS.md`](../../AGENTS.md) is the entry point and defines when these documents apply.

These files are specification-oriented, but the repository is now in early implementation. Treat behavior that already exists in the CMake scaffold, `dependencies.json`, and `build.py` as real implementation that should remain compatible unless a deliberate design change is made. Planned C++ APIs and protocol behavior should still not be invented merely because they are described here.
