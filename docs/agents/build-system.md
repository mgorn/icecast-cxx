# Build system and dependency guidance

CMake is the primary first-party build system. The build must support both a convenient top-level developer checkout and parent projects that fully manage their own dependencies.

## Current target status

- `icecast::core` — compiled.
- `icecast::stream` — compiled when enabled.
- `icecast::transport_curl` — compiled native listener backend when enabled.
- `icecast::admin` — placeholder.
- `icecast::publish_libshout` — placeholder.
- `icecast::transport_web` — placeholder.

Compiled libraries support static/shared builds through `BUILD_SHARED_LIBS`, install public header file sets, and export normal `icecast::` package targets.

## Build options

Stable implemented cache variables include:

- `ICECAST_CXX_BUILD_TESTS`
- `ICECAST_CXX_INSTALL`
- `ICECAST_CXX_FETCH_DEPENDENCIES`
- `ICECAST_CXX_FETCH_CURL`
- `ICECAST_CXX_DEPENDENCIES_DIR`
- `ICECAST_CXX_CURL_SOURCE_DIR`
- `ICECAST_CXX_ENABLE_STREAM`
- `ICECAST_CXX_ENABLE_ADMIN`
- `ICECAST_CXX_ENABLE_TRANSPORT_CURL`
- `ICECAST_CXX_ENABLE_PUBLISH_LIBSHOUT`
- `ICECAST_CXX_ENABLE_TRANSPORT_WEB`

Heavyweight/native backend targets default on for a matching top-level developer build and off when embedded. Tests/install rules default on only at the top level.

## Conditional dependencies

A dependency is required only when its component is enabled. `core` and `stream` have no curl dependency. `transport_curl` requires libcurl >= 7.66.0.

Never make an optional backend dependency globally mandatory.

## Resolution order

For every real dependency, preserve this order:

1. compatible already-provided parent target;
2. explicit source/package hint;
3. compatible checkout under `ICECAST_CXX_DEPENDENCIES_DIR`;
4. installed/system package discovery;
5. pinned FetchContent fallback;
6. actionable failure when fetching is disabled.

For curl specifically, Unix system discovery may use pkg-config between CMake config mode and generic `FindCURL` to preserve transitive dependencies of static system libcurl installations.

`ICECAST_CXX_FETCH_DEPENDENCIES=OFF` must make CMake configuration network-free.

## Dependency manifest

`dependencies.json` is the authoritative machine-readable source pin manifest for both `build.py` and CMake FetchContent fallback.

The curl entry records its exact repository, immutable revision, deterministic local directory, CMake source variable, and applicable platform.

Do not duplicate pinned commit hashes in a CMake helper or Python source file.

## build.py

`build.py` is a convenience gateway, not a second build system. It prepares applicable exact Git revisions in `dependencies/`, passes local source paths to CMake, configures the requested platform, builds, and runs CTest.

Dependencies can declare a `platforms` array in schema version 1. A web build therefore does not download native-only curl.

Dirty local dependency checkouts must never be overwritten silently. Offline mode fails clearly when a required pinned checkout is missing or at the wrong revision.

## Fetched curl configuration

When icecast-cxx builds the pinned curl source, keep it intentionally small:

- HTTP-only protocol set;
- curl command-line tool/examples/docs/tests disabled;
- curl installation rules disabled inside the parent project;
- unnecessary libpsl/SSH/IDN/HTTP2/compression integrations disabled;
- TLS enabled;
- Schannel on Windows;
- OpenSSL on other native platforms for now.

The non-Windows source-build path therefore currently expects an OpenSSL development environment. This is a transitive dependency limitation to improve later, not a reason to leak TLS implementation types into public APIs.

## Installed package

`icecast-cxxConfig.cmake` must never invoke FetchContent. When `transport_curl` was installed, the package config recreates/finds a compatible `CURL::libcurl` target before loading exported icecast-cxx targets. The Unix pkg-config fallback exists to support static curl installations whose generic CMake module target omits transitive libraries.

Validate installed static and shared packages with a separate downstream `find_package(icecast-cxx CONFIG REQUIRED)` project.

## Tests

Fast core/stream tests remain transport-free. Native curl tests use a loopback synthetic server and must not require external network access.

Where available, validate GCC and Clang warning-clean builds, static/shared targets, public-header self-containment, CTest, install/export, installed downstream consumption, and embedded builds.

## Other build systems

CMake is the only planned first-party build system for initial releases. Additional build systems are welcome through issues/PRs if they preserve the same target boundaries and optional-dependency semantics.
