# Build system and dependency guidance

CMake is the primary first-party build system for `icecast-cxx`.

The build should optimize for two equally important cases:

1. a user who wants the project to configure with minimal effort and is happy for missing dependencies to be downloaded automatically;
2. a user or parent project that manages dependencies itself and does not want `icecast-cxx` to fight that setup.

## Core rule: dependencies are conditional

A third-party library may be required by an enabled backend without being required by `icecast-cxx` as a whole.

Examples of the intended shape:

- `icecast::core` should not require a native HTTP or publishing library;
- native listener/admin support may require libcurl;
- native publisher support may require libshout;
- browser transport is relevant to Emscripten builds and should not burden native-only consumers.

Do not make an optional module's dependency globally mandatory when the module is not being built.

## Dependency resolution order

For each dependency required by an enabled component, CMake should resolve it in this order:

1. **Already-provided target** — reuse a compatible CMake target that the parent project has already defined.
2. **Explicit source/package hint** — honor a user-provided path, package hint, or a path supplied by `build.py`.
3. **Repository-local dependency checkout** — when a compatible dependency exists in `dependencies/`, reuse it rather than fetching another copy.
4. **Installed/system package** — use normal CMake package discovery when a compatible installation is available.
5. **FetchContent fallback** — if the dependency is still missing and fetching is enabled, obtain the project's pinned immutable revision.
6. **Actionable failure** — if the dependency is required for an enabled component and fetching is disabled, stop configuration with a message explaining what is missing and the supported ways to provide it.

Do not unconditionally invoke `FetchContent_MakeAvailable()` before giving parent projects a chance to supply dependencies.

## Fetch controls

Automatic fetching of missing dependencies should default to **ON** for convenience.

The eventual CMake interface should provide:

- a global switch that can disable all automatic dependency downloads;
- per-dependency switches for advanced configurations;
- module/backend enable/disable options so dependencies are only resolved when their consumer is enabled.

Exact cache-variable names should be chosen deliberately when CMake implementation begins. Once released, option names become part of the developer-facing compatibility surface and should not be renamed casually.

When fetching is disabled, CMake configuration should remain fully network-free.

## Immutable dependency versions

All automatically downloaded dependencies must be pinned to immutable release tags that are guaranteed immutable by project policy or, preferably when certainty is required, exact commit hashes.

Never follow `master`, `main`, `develop`, or another moving branch in reproducible project configuration.

Keep the authoritative dependency revisions in one obvious location so `build.py`, CMake fallback fetching, CI, and release tooling do not silently drift to different versions.

## `dependencies/`

A repository-local `dependencies/` directory will hold dependency source trees downloaded by `build.py`.

Requirements:

- it must be git-ignored;
- it is local build/development state, never vendored source committed accidentally;
- CMake should recognize compatible dependency sources there and prefer them over downloading another copy;
- source layout beneath the directory should be deterministic enough for `build.py` and CMake to agree on locations;
- changing a pinned dependency revision must invalidate/reconcile a stale local checkout clearly rather than silently building the wrong revision.

Do not assume every consumer of `icecast-cxx` has this directory. It is primarily for repository-local builds.

## `build.py`

`build.py` is the convenience gateway for building the full project from a source checkout.

The expected simple path is:

```console
python build.py
```

The script should eventually support a useful `--help` interface and explicit configuration without requiring users to memorize raw CMake invocations.

Its responsibilities are to:

1. determine which dependency sources are required for the requested build;
2. download/check out the project's pinned revisions into `dependencies/`;
3. pass explicit local dependency hints to CMake so those sources are used instead of CMake downloading them again;
4. configure the requested platform/modules/backends;
5. invoke the CMake build;
6. report failures with actionable context.

`build.py` must not become a second independent build system. CMake remains the source of truth for how targets are configured and built; the script orchestrates dependency preparation and CMake invocation.

Normal downstream CMake users must **not** need Python merely to consume `icecast-cxx`.

## Supported CMake consumption modes

The project should behave correctly in all common CMake integration styles.

### `FetchContent`

A parent project should be able to declare `icecast-cxx`, make it available, and link namespaced targets.

When nested this way:

- do not assume `icecast-cxx` is the top-level project;
- avoid changing unrelated global CMake settings;
- avoid forcing project-wide compiler flags on the parent;
- do not build tests/examples/tools by default unless explicitly requested;
- reuse dependencies already supplied by the parent when compatible.

### Git submodule + `add_subdirectory`

A checked-out repository nested under a parent source tree should work with an ordinary `add_subdirectory()` call.

Apply the same nested-project rules as FetchContent.

### Downloaded release archive/ZIP + `add_subdirectory`

A source archive should not require Git metadata or submodules merely to configure. If dependency source retrieval is needed and enabled, CMake should use the documented fallback mechanism.

### Installed CMake package

The project should eventually provide install/export support with a package config and namespaced imported targets.

An installed `icecast-cxx` package should not unexpectedly run FetchContent inside a downstream consumer's configure step. Installed-package dependency behavior should use normal package dependencies/imported targets appropriate to the packaged artifact.

## Target design

Public target names should be stable, namespaced, and responsibility-oriented.

Current candidates include:

- `icecast::core`
- `icecast::stream`
- `icecast::admin`
- `icecast::transport_curl`
- `icecast::publish_libshout`
- `icecast::transport_web`

The exact target graph is not frozen yet. Avoid creating unnecessary tiny targets, but keep heavyweight/legally distinct backend dependencies separable so consumers do not inherit what they do not use.

Public targets should propagate only the usage requirements their consumers genuinely need. Backend implementation dependencies should remain private whenever public headers do not expose them.

## Static and shared builds

The project is intended to support both static and shared library builds where technically practical.

Do not assume that static and shared linkage have identical third-party licensing/distribution implications. In particular, dependency packaging and future Python wheel builds must be reviewed against each dependency's license.

Avoid source-level design that unnecessarily prevents either linkage model.

## Platform configuration

First-class target platforms are planned to include:

- Windows;
- macOS;
- Linux;
- Emscripten/WebAssembly.

Keep platform-specific compiler/linker behavior localized. Do not scatter platform checks throughout semantic/public modules when a backend target can own them.

Emscripten builds must use browser-appropriate networking semantics; do not add native socket dependencies to browser targets merely because they are convenient on desktop platforms.

## Tests, examples, and development tools

When implementation begins:

- tests should be easy to enable for a top-level developer checkout;
- tests should default off or otherwise avoid surprising parent projects when `icecast-cxx` is nested;
- examples should not be required to consume the library;
- test-only dependencies must not leak into installed/public targets;
- real Icecast integration tests should be separable from fast unit tests.

## Other build systems

CMake is the only planned first-party build system for initial releases.

Do not add another build system speculatively. Users who need Meson, Bazel, another build system, or additional package-manager integration should be encouraged to open an issue or submit a pull request.

Any added build system should preserve the same module boundaries, optional-dependency behavior, pinned dependency revisions, and install/consumer semantics rather than creating a divergent project layout.
