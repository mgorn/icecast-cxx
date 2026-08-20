# Build system and dependency guidance

CMake is the primary first-party build system for `icecast-cxx`.

The build must serve two cases equally well:

1. a developer building the repository directly who wants the full applicable project with minimal setup;
2. a downstream project that manages dependencies itself and does not want `icecast-cxx` to pull in unrelated backends or fight its dependency setup.

## Implemented target status

The current consumer-facing target names are:

- `icecast::core` — real compiled library;
- `icecast::stream` — real compiled library when `ICECAST_CXX_ENABLE_STREAM=ON`;
- `icecast::admin` — INTERFACE placeholder;
- `icecast::transport_curl` — INTERFACE placeholder;
- `icecast::publish_libshout` — INTERFACE placeholder;
- `icecast::transport_web` — INTERFACE placeholder.

`core` and `stream` support normal static/shared project builds through `BUILD_SHARED_LIBS`. Both install their public header file sets and export as ordinary `icecast::` CMake package targets.

Do not add dummy source files merely to convert a future component from INTERFACE to compiled form. Convert a target when real implementation is introduced.

## Current CMake options

These cache variables are part of the implemented build surface and should not be renamed casually:

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
- embedded/FetchContent/add_subdirectory build: heavyweight backend targets default off;
- tests and install rules default on only for a top-level build.

The native/backend targets are still placeholders, so their current default-on state does not yet fetch third-party libraries. Once real dependencies are connected, preserve the rule that embedded consumers do not acquire them unless the corresponding backend is enabled.

## Conditional dependencies

A third-party library may be required by an enabled backend without being required by `icecast-cxx` as a whole.

Examples:

- `icecast::core` has no native HTTP or publishing dependency;
- `icecast::stream` has no native HTTP or publishing dependency;
- native listener/admin support may require libcurl;
- native publisher support may require libshout;
- browser transport applies to Emscripten builds and must not burden native-only consumers.

Do not make an optional module's dependency globally mandatory when that module is not enabled.

## Dependency resolution order

For each real dependency required by an enabled component, CMake must resolve it in this order:

1. **Already-provided target** — reuse a compatible target already defined by the parent project.
2. **Explicit source/package hint** — honor a developer-provided path/package hint or a source variable supplied by `build.py`.
3. **Repository-local checkout** — reuse a compatible source tree beneath `ICECAST_CXX_DEPENDENCIES_DIR`.
4. **Installed/system package** — use normal package discovery when compatible.
5. **FetchContent fallback** — if still missing and fetching is enabled, obtain the pinned immutable revision.
6. **Actionable failure** — if required and fetching is disabled, fail with clear instructions for supplying the dependency.

Do not invoke `FetchContent_MakeAvailable()` before parent-provided targets and local/system packages have been considered.

`ICECAST_CXX_FETCH_DEPENDENCIES` defaults to `ON`. When it is `OFF`, CMake configuration must remain network-free.

Per-dependency fetch switches may be added together with real dependencies, but they should complement rather than replace the global switch.

## Dependency pins and `dependencies.json`

`dependencies.json` is the authoritative machine-readable dependency manifest used by `build.py`.

The current schema is version 1 and the dependency array is empty because the implemented `core` and `stream` libraries require no third-party sources.

Git dependencies added later should record `name`, `type` (`git`), `repository`, an exact immutable `revision` commit SHA, and optionally a deterministic `directory` and `cmake_source_variable`.

Never follow `main`, `master`, `develop`, or another moving branch in reproducible project configuration. CMake's FetchContent revision and `dependencies.json` must identify the same immutable source revision.

## `dependencies/`

`dependencies/` contains repository-local dependency source trees prepared by `build.py`.

It must remain git-ignored and local build state. CMake should prefer compatible sources there before downloading another copy. `build.py` must preserve developer modifications rather than silently replacing dirty checkouts, and offline mode must fail clearly when a pinned source is unavailable.

Normal downstream consumers must not be assumed to have this directory.

## `build.py`

`build.py` is the convenience gateway for building this repository; CMake remains the build-system source of truth.

The normal path is:

```console
python build.py
```

It reads `dependencies.json`, prepares exact Git revisions, passes local source variables to CMake, configures native/web components, builds, and runs CTest unless disabled.

Useful switches include `--platform native|web`, `--configuration`, `--generator`, `--jobs`, `--target`, `--clean`, `--offline`, `--no-tests`, `--configure-only`, and repeatable `--cmake-arg`.

Normal downstream CMake users must not need Python.

## CMake consumption modes

### FetchContent

A parent project can make `icecast-cxx` available and link `icecast::core` / `icecast::stream` directly. Real heavyweight backends remain opt-in when nested.

When nested, do not assume `icecast-cxx` is top-level, mutate unrelated global settings, force project-wide compiler flags, or enable tests/install tools unexpectedly. Reuse compatible dependencies already supplied by the parent.

### Git submodule or downloaded source + `add_subdirectory`

Both a Git checkout and a source ZIP/archive must work with an ordinary `add_subdirectory()` call. Source archives must not require Git metadata just to configure.

### Installed package

The project exports enabled targets through `icecast-cxxConfig.cmake`, `icecast-cxxConfigVersion.cmake`, and `icecast-cxxTargets.cmake`.

Installed packages must not run FetchContent in downstream consumers. Once backend dependencies are real, package config files should locate required installed dependency targets through normal package mechanisms.

## Static/shared builds and visibility

Compiled libraries use their own export macros (`ICECAST_CXX_CORE_API`, `ICECAST_CXX_STREAM_API`) so Windows DLL and hidden-visibility toolchains can expose the intended ABI without leaking third-party types.

Keep implementation dependencies private whenever public headers do not require them. Do not assume static and shared linkage have identical third-party licensing/distribution implications, especially for future libshout packaging and Python wheels.

## Platform configuration

First-class planned platforms are Windows, macOS, Linux, and Emscripten/WebAssembly.

Keep platform-specific compiler/linker behavior in backend/build files, not semantic public modules. Native curl/libshout targets are invalid in Emscripten configurations; the web transport target is invalid in non-Emscripten configurations.

## Tests and validation

Fast unit tests should stay transport-free where possible. `core` and `stream` currently have dedicated CTest executables.

For each compiled semantic layer, validate GCC/Clang warning-clean compilation where available, static and shared builds, public-header self-containment, CTest, install/export, downstream `find_package(icecast-cxx CONFIG REQUIRED)` consumption, and embedded `add_subdirectory()` behavior.

Real Icecast/network integration tests should be added separately when transport code exists.

## Other build systems

CMake is the only planned first-party build system for initial releases.

Do not add another build system speculatively. Users who need Meson, Bazel, another build system, or package-manager integration should be encouraged to open an issue or submit a pull request. Any future build system must preserve the same target boundaries, optional-dependency behavior, pinned revisions, and consumer semantics.
