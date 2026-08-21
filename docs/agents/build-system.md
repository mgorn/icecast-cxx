# Build system and dependency guidance

CMake is the primary first-party build system. The build must support both a convenient top-level developer checkout and parent projects that fully manage their own dependencies.

## Current target status

- `icecast::core` — compiled.
- `icecast::stream` — compiled when enabled.
- `icecast::transport_curl` — compiled native listener backend when enabled.
- `icecast::publish_libshout` — compiled native publisher backend when enabled.
- `icecast::admin` — placeholder.
- `icecast::transport_web` — placeholder.

Compiled libraries support static/shared icecast-cxx builds through `BUILD_SHARED_LIBS`, install public header file sets, and export normal `icecast::` package targets.

## Build options

Stable implemented cache variables include:

- `ICECAST_CXX_BUILD_TESTS`
- `ICECAST_CXX_INSTALL`
- `ICECAST_CXX_FETCH_DEPENDENCIES`
- `ICECAST_CXX_FETCH_CURL`
- `ICECAST_CXX_FETCH_LIBSHOUT`
- `ICECAST_CXX_DEPENDENCIES_DIR`
- `ICECAST_CXX_CURL_SOURCE_DIR`
- `ICECAST_CXX_LIBSHOUT_SOURCE_DIR`
- `ICECAST_CXX_ENABLE_STREAM`
- `ICECAST_CXX_ENABLE_ADMIN`
- `ICECAST_CXX_ENABLE_TRANSPORT_CURL`
- `ICECAST_CXX_ENABLE_PUBLISH_LIBSHOUT`
- `ICECAST_CXX_ENABLE_TRANSPORT_WEB`

Heavyweight/native backend targets default on for a matching top-level developer build and off when embedded. Tests/install rules default on only at the top level.

## Conditional dependencies

A dependency is required only when its component is enabled. `core` and `stream` do not require curl or libshout. `transport_curl` requires libcurl >= 7.66.0. `publish_libshout` requires libshout >= 2.4.4.

Never make an optional backend dependency globally mandatory.

## Resolution order

For every real dependency, preserve this order:

1. compatible already-provided parent target;
2. explicit source/package hint;
3. compatible source under `ICECAST_CXX_DEPENDENCIES_DIR`;
4. installed/system package discovery;
5. pinned automatic source fallback;
6. actionable failure when fetching is disabled.

For curl, Unix system discovery may use pkg-config between CMake config mode and generic `FindCURL` to preserve transitive dependencies of static system libcurl installations.

For libshout, an already-defined `Shout::shout` target wins. System discovery uses the `shout` pkg-config module. Upstream libshout uses autotools rather than CMake, so the pinned source fallback uses `ExternalProject` rather than pretending the upstream source is a CMake subproject.

`ICECAST_CXX_FETCH_DEPENDENCIES=OFF` must make CMake configuration network-free.

## Dependency manifest

`dependencies.json` is the authoritative machine-readable source pin manifest for `build.py` and CMake automatic source fallbacks.

Git dependencies record exact revisions. Archive dependencies record immutable release URLs and SHA-256 values. Current native pins include curl and libshout 2.4.6.

Do not duplicate pinned revisions/checksums in CMake helpers or Python source files.

## build.py

`build.py` is a convenience gateway, not a second build system. It prepares applicable sources in `dependencies/`, passes local source paths to CMake, configures the requested platform, builds, and runs CTest.

Schema version 1 supports both exact Git dependencies and hash-pinned tar archives. Archive extraction validates paths, rejects link entries, and records a marker describing the archive that populated the directory. A manually supplied archive-source directory without that marker is treated as developer-owned and is never overwritten automatically.

Dependencies can declare a `platforms` array. A web build therefore does not download native-only curl or libshout.

Dirty/mismatched Git checkouts must never be overwritten silently. Offline mode fails clearly when a required pinned dependency is missing or incompatible.

## Fetched/native source configuration

When icecast-cxx builds the pinned curl source, keep it intentionally small and HTTP-focused as documented in `transport-curl.md`.

When icecast-cxx builds libshout from its pinned release source on a Unix-like host:

- use an isolated autotools `ExternalProject` build;
- disable examples/tools;
- build shared libshout even when the icecast-cxx target itself is static, avoiding hidden static transitive-link requirements;
- keep libshout headers/types private to `icecast::publish_libshout`;
- expect libshout's ordinary source-build prerequisites, including libogg development support and optional TLS prerequisites.

Automatic libshout source building is not currently provided for native Windows/MSVC. Windows consumers should provide a compatible package/target (`Shout::shout`). This is a build-integration limitation, not a reason to expose libshout types in the public API.

## Installed package

`icecast-cxxConfig.cmake` must never invoke a network download. When `transport_curl` was installed, it resolves an installed curl target. When `publish_libshout` was installed, it reuses a caller-provided `Shout::shout` target or resolves the installed `shout` pkg-config module before loading exported icecast-cxx targets.

The source-built private copy of libshout is developer/build-tree state and is not silently bundled into an `icecast-cxx` installation. Packaging/distribution of libshout must remain deliberate because it has its own license and transitive dependencies.

Validate installed static and shared icecast-cxx packages with a separate downstream `find_package(icecast-cxx CONFIG REQUIRED)` project.

## Tests

Fast core/stream tests remain transport-free. Native curl tests use a loopback synthetic server. Native libshout tests use a loopback source endpoint and must not require an external Icecast deployment.

Where available, validate GCC and Clang warning-clean builds, public-header self-containment, CTest, install/export, installed downstream consumption, and embedded builds.

## Other build systems

CMake is the only planned first-party build system for initial releases. Additional build systems are welcome through issues/PRs if they preserve the same target boundaries and optional-dependency semantics.
