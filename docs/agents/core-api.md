# Core API guidance

`icecast::core` is the first implemented C++ module in `icecast-cxx`. It is a compiled C++20 library with no third-party runtime dependency.

Read this document before changing the foundational public models under `include/icecast/core/`.

## Include and namespace

The convenience include is:

```cpp
#include <icecast/core.hxx>
```

Granular headers under `icecast/core/` are also public and should remain independently includable.

Public C++ names currently live directly in the `icecast` namespace. The CMake component name `icecast::core` does not imply an extra `icecast::core` C++ namespace.

## Value-oriented models

Core models intentionally prefer ordinary `struct` values. Developers may construct them directly when that is the convenient path, while parsing/validation helpers exist for untrusted or textual input.

Do not turn simple core values into access-control-heavy classes merely to force every construction path through a factory. Validate at protocol/transport boundaries even when a value was constructed directly.

## Endpoints and mountpoints

`server_endpoint` contains:

- `endpoint_scheme` (`http` or `https`);
- host without URI square brackets;
- optional explicit port;
- optional base-path prefix for reverse-proxy/subpath deployments.

`parse_server_endpoint()` accepts absolute HTTP(S) server URLs and deliberately rejects:

- embedded credentials/userinfo;
- unsupported schemes;
- invalid/out-of-range ports;
- URL query strings and fragments;
- whitespace/control characters.

Credentials must be supplied separately.

IPv6 literals use URI square brackets in text form but are stored without brackets in `server_endpoint::host`. Formatting adds brackets when needed.

The parser normalizes a trailing slash from the server base path. `format_server_endpoint()` also avoids a trailing base-path slash so `resolve_mount_url()` can append a mountpoint without producing an accidental double slash.

`mountpoint` represents the serialized absolute path component only. `parse_mountpoint()` requires a leading `/` and rejects query/fragment text and unencoded whitespace/control characters.

Core currently treats path text as already serialized for a URL. It does not guess whether caller input should be percent-encoded or decoded. Do not silently add automatic URI encoding without a deliberate API decision, because double-encoding is worse than requiring explicit serialized input.

## Credentials

`basic_credentials` is a generic HTTP Basic username/password value. Role-specific conveniences for source/listener/admin authentication may be added by higher-level APIs later without changing the transport-independent credential representation.

`validate_basic_credentials()` rejects:

- `:` in the username, because Basic authentication uses it as the user/password separator;
- NUL, CR, or LF in either value.

Never add an API that formats a password into normal diagnostic output. `format_redacted_credentials()` exists for diagnostics and sanitizes control characters in the displayed username while replacing the password with `<redacted>`.

Core does not Base64-encode credentials. The eventual HTTP backend owns creation of the actual Authorization header so backend policy remains centralized.

## HTTP headers

`headers` preserves insertion order and duplicate fields because HTTP response/request fields are not universally single-valued.

Lookup is ASCII case-insensitive through:

- `contains()`;
- `first()`;
- `all()`.

`append()` validates then appends another field. `set()` validates first, then replaces all existing fields with the same case-insensitive name. An invalid `set()` is transactional and must not erase the previous valid value.

Header names follow HTTP token-character rules. Header values reject NUL/CR/LF to prevent header injection.

`headers::fields` remains public as part of the value-oriented model. Prefer `append()`/`set()` for untrusted or dynamically produced values, and validate externally populated field collections before a transport serializes them.

The `std::string_view` values returned by lookup helpers refer to strings owned by the collection and can be invalidated by later mutation.

## Errors and results

Routine operational failures are values.

`error` currently records:

- `error_category`;
- human-readable `message`;
- semantic `operation`;
- whether the failure is retryable;
- optional HTTP status;
- optional backend name and numeric backend code.

Backend-specific error types such as `CURLcode` must be translated at the backend boundary rather than exposed in public signatures.

`result<T>` is the project-owned C++20 expected-like result type. It supports value/error construction, `has_value()`, boolean checks, `value()`, `error()`, and pointer-style `value_if()`/`error_if()` inspection. `result<void>` represents operations that only succeed or fail.

Do not replace routine protocol/network failures with exceptions as the only observation mechanism.

## Capability model

Capability availability has three explicit states:

- `unsupported`;
- `conditional`;
- `supported`.

A capability may also carry a detail string and machine-readable-ish requirement strings that higher-level code can surface to users or use for feature decisions.

Platform and server capability sets are distinct types:

- `platform_capabilities` describes what the current compiled/runtime environment can do;
- `server_capabilities` describes what the remote server is known to support;
- `effective_capabilities` is the intersection.

`combine_capabilities()` computes the effective support state and merges requirements without duplicating identical requirement strings.

Do not collapse these layers into browser-specific booleans scattered through application code.

## ABI and linkage

`icecast::core` is a real static/shared library rather than an INTERFACE placeholder.

Public non-template functions use `ICECAST_CXX_CORE_API` so shared builds work on Windows without enabling broad auto-export behavior. Template code remains in headers.

No stable binary ABI guarantee exists yet. Do not optimize the early API around ABI compatibility at the expense of getting the public semantics right.

## Tests

The core test executable covers:

- result/error behavior;
- HTTP/HTTPS endpoint parsing and formatting;
- IPv6 and default-port behavior;
- rejection of embedded credentials/query text/invalid ports;
- mountpoint validation;
- Basic credential validation and redaction;
- duplicate/case-insensitive header behavior and injection rejection;
- capability intersection.

When adding core behavior, extend these fast tests before involving networking or real Icecast servers.
