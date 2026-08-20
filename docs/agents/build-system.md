# Build system and dependency guidance

CMake is the primary first-party build system for `icecast-cxx`.

The build must optimize for two equally important cases:

1. a developer building the repository directly who wants the full applicable project with minimal setup;
2. a downstream/parent project that manages dependencies itself and does not want `icecast-cxx` to pull in unrelated backends or fight its dependency setup.

## Current scaffold

The repository now contains a working initial build scaffold:

- root `CMakeLists.txt`;
- `cmake/icecast-cxx-options.cmake`;
- install/export package generation;
- namespaced INTERFACE target placeholders;
- `dependencies.json` as the dependency-pin manifest;
- `build.py` as the repository build gateway;
- `tests/CMakeLists.txt` with initial CTest plumbing;
- git-ignored repository-local `dependencies/` state.

The component targets are currently INTERFACE placeholders. Preserve their consumer-facing names while replacing them with real compiled targets as implementation is introduced.

## Current CMake options

These cache variables are now part of the implemented build surface and should not be renamed casually:

- `ICECAST_CXX_BUILD_TESTS`
- `ICECAST_CXX_INSTALL`
- `ICECAST_CXX_FETCH_DEPENDENCIES`
- `ICECAST_CXX_DEPENDENCIES_DIR`
- `ICECAST_CXX_ENABLE_STREAM`
- `ICECAST_CXX_ENABLE_ADMIN`
- `ICECAST_CXX_ENABLE_TRANSPORT_CURL`
- `ICECAST_CXX_ENABLE_PUBLISH_LIBSHOUT`
- `ICECAST_CXX_ENABLE_TRANSPORT_WEB`

`core` is always present. `stream` and `admin` default on. Backend defaults depend on context:

- top-level native build: curl and libshout backend targets default on;
- top-level Emscripten build: web transport defaults on;
- embedded/subdirectory/FetchContent build: heavyweight backend targets default off;
- tests and install rules default on only for a top-level build.

This distinction is deliberate. A parent project using only `icecast::core` must not acquire curl/libshout simply because `icecast-cxx` was added as a subdirectory.

## Core rule: dependencies are conditional

A third-party library may be required by an enabled backend without being required by `icecast-cxx` as a whole.

Examples:

- `icecast::core` should not require a native HTTP or publishing library;
- native listener/admin support may require libcurl;
- native publisher support may require libshout;
- browser transport is relevant to Emscripten builds and should not burden native-only consumers.

Do not make an optional module's dependency globally mandatory when that module is not enabled.

## Dependency resolution order

When actual third-party dependencies are wired into CMake, each dependency required by an enabled component must be resolved in this order:

1. **Already-provided target** — reuse a compatible CMake target already defined by the parent project.
2. **Explicit source/package hint** — honor a user-provided path/package hint or the source variable supplied by `build.py`.
3. **Repository-local dependency checkout** — reuse a compatible source tree beneath `ICECAST_CXX_DEPENDENCIES_DIR`.
4. **Installed/system package** — use normal CMake package discovery when compatible.
5. **FetchContent fallback** — if still missing and fetching is enabled, obtain the pinned immutable revision.
6. **Actionable failure** — if required and fetching is disabled, fail with clear instructions for supplying the dependency.

Do not call `FetchContent_MakeAvailable()` unconditionally before parent-provided targets and local/system packages have been considered.

`ICECAST_CXX_FETCH_DEPENDENCIES` defaults to `ON`. When it is `OFF`, CMake configuration must remain network-free.

Per-dependency fetch switches may be introduced when the first real dependencies are added, but they should complement rather than replace the global switch.

## Dependency pins and `dependencies.json`

`dependencies.json` is the authoritative machine-readable manifest for dependency source revisions used by `build.py`.

The current schema is version 1:

```json
{
  "schema_version": 1,
  "dependencies": []
}
```

Git dependencies added to the manifest are expected to provide fields such as:

- `name`;
- `type` (`git`, currently the only supported type);
- `repository`;
- `revision` — an exact immutable commit SHA;
- optional `directory`;
- optional `cmake_source_variable` passed to CMake after preparation.

Keep revision pins exact. Do not use `main`, `master`, `develop`, or another moving branch.

When a real dependency is introduced, CMake's FetchContent revision and `dependencies.json` must refer to the same immutable source revision. Avoid creating separate unsynchronized version constants.

## `dependencies/`

`dependencies/` contains dependency source trees prepared by `build.py`.

Requirements:

- it remains git-ignored;
- it is local build/development state, not vendored source;
- CMake should prefer compatible sources there before downloading another copy;
- `build.py` refuses to replace a checkout at the wrong revision when that checkout has local modifications;
- offline builds must fail clearly when the requested pinned source is unavailable locally.

Do not assume normal downstream consumers have this directory.

## `build.py`

`build.py` is the convenience gateway for building this repository. CMake remains the build-system source of truth.

The normal path is:

```console
python build.py
```

The implemented script currently:

1. reads `dependencies.json`;
2. prepares exact Git revisions in `dependencies/`;
3. passes configured local source variables to CMake;
4. configures the appropriate native or web component set;
5. builds with CMake;
6. runs CTest unless disabled.

Useful switches include:

- `--platform native|web`;
- `--configuration`;
- `--generator`;
- `--jobs`;
- `--target`;
- `--clean`;
- `--offline`;
- `--no-tests`;
- `--configure-only`;
- repeatable `--cmake-arg` for advanced escape hatches.

`--platform web` uses Emscripten's `emcmake` and therefore requires it on `PATH`.

Normal downstream CMake users must not need Python to consume the library.

## Supported CMake consumption modes

### FetchContent

A parent project can declare `icecast-cxx`, enable the backend(s) it needs through cache options, call `FetchContent_MakeAvailable()`, and link namespaced targets.

When nested:

- do not assume `icecast-cxx` is top-level;
- do not change unrelated global CMake settings;
- do not force project-wide compiler flags;
- tests/install/developer tools should not appear unexpectedly;
- heavyweight backends remain opt-in;
- reuse compatible dependencies already supplied by the parent.

### Git submodule + `add_subdirectory`

A checked-out repository nested under a parent source tree must work with an ordinary `add_subdirectory()` call and the same option semantics as FetchContent.

### Downloaded release source/ZIP + `add_subdirectory`

A release archive must not require Git metadata or repository submodules merely to configure. CMake-managed dependency fallback should work from source archives when enabled.

### Installed CMake package

The scaffold already exports installed targets through:

- `icecast-cxxConfig.cmake`;
- `icecast-cxxConfigVersion.cmake`;
- `icecast-cxxTargets.cmake`.

Installed packages must not unexpectedly run FetchContent in downstream consumers. Once backend dependencies are real, package config files should locate required external package targets using normal installed-package mechanisms.

## Target design

Current consumer-facing target names are:

- `icecast::core`
- `icecast::stream`
- `icecast::admin`
- `icecast::transport_curl`
- `icecast::publish_libshout`
- `icecast::transport_web`

These currently map to INTERFACE targets solely to establish and test the build surface. As implementation begins, convert components to compiled targets only when they gain source code; do not add dummy object files merely to make them non-INTERFACE.

Public targets must propagate only usage requirements consumers genuinely need. Backend implementation dependencies should remain private whenever public headers do not expose them.

## Static and shared builds

Compiled project libraries are intended to support static and shared builds where practical. The current INTERFACE scaffold does not yet exercise this.

Do not assume static and shared linkage have identical third-party licensing/distribution implications, particularly for libshout and future Python wheel packaging.

## Platform configuration

First-class planned targets are:

- Windows;
- macOS;
- Linux;
- Emscripten/WebAssembly.

Keep platform-specific compiler/linker behavior localized in backend/build files. Do not scatter platform checks through semantic public modules.

Native curl/libshout targets are rejected in Emscripten configurations. The web transport target is rejected in non-Emscripten configurations.

## Tests and validation

The scaffold currently configures CTest and includes a minimal configuration smoke test.

As implementation grows:

- add fast unit tests alongside each semantic component;
- keep test-only dependencies out of public/install targets;
- make real Icecast integration tests separately selectable;
- validate both top-level and embedded CMake configurations;
- validate installation followed by a downstream `find_package(icecast-cxx CONFIG REQUIRED)` configure;
- validate dependency-provided, local-source, system-package, FetchContent, and fully-offline paths for each external dependency.

## Other build systems

CMake is the only planned first-party build system for initial releases.

Do not add another build system speculatively. Users who need Meson, Bazel, another build system, or package-manager integration should be encouraged to open an issue or submit a pull request.

Any future build system must preserve the same module boundaries, optional-dependency behavior, pinned revisions, and consumer semantics rather than creating a divergent project structure.
