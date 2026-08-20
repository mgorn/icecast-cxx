# Native curl transport guidance

`icecast::transport_curl` is the implemented native listener backend. It depends publicly on `icecast::stream` semantics and privately on libcurl.

## Public API

The public entry points are `curl_context`, `curl_listener`, the existing `listener_config` input, `curl_listener_callbacks`, and `curl_listener_options` through `<icecast/transport_curl.hxx>`.

Do not expose curl headers, `CURL*`, `CURLM*`, curl error enums, or curl callback signatures in public headers.

`curl_context` and `curl_listener` are active resource-owning types, so opaque implementation state is a deliberate exception to the project's otherwise `struct`-first/no-access-control preference.

## Execution and callbacks

Network progress occurs only when the application calls `curl_context::poll()`.

- no hidden worker thread is created;
- callbacks execute on the thread calling `poll()`;
- `curl_listener::resume()` and `stop()` are commands processed by the same caller-driven context;
- the current API does not promise cross-thread access to context/listener objects.

Do not add implicit threads merely to make a backend operation easier.

## Stream semantics are authoritative

The curl backend must reuse:

- `listener_config` validation;
- listener state transitions and connection generations;
- reconnect policy/base delay;
- `consume_icy_stream()` framing behavior;
- `stream_action` backpressure.

Do not implement a parallel curl-specific ICY parser or state machine.

## Partial-buffer pause invariant

A libcurl write callback cannot report a partially consumed buffer and simultaneously ask curl to pause; a curl pause means the callback consumed none of that callback buffer.

When the stream consumer pauses after consuming a prefix, the backend therefore copies only the unconsumed tail into a fixed buffer of `CURL_MAX_WRITE_SIZE`, marks the receive handle paused, and reports the original curl callback buffer as consumed. On resume, the pending tail is processed before curl receives `CURLPAUSE_CONT`.

This is intentionally bounded to one libcurl write chunk. Do not replace it with an unbounded queue.

## HTTP behavior

- Only HTTP and HTTPS protocols are permitted.
- Requests use GET.
- Basic credentials use libcurl's username/password options and are never embedded in the URL.
- `Icy-MetaData: 1` is managed when requested.
- `Accept-Encoding: identity` is managed so servers are asked not to wrap encoded media in an HTTP content coding.
- Intermediate redirect/auth header blocks are discarded; `stream_info` represents the final response.
- A received `icy-metaint` is honored as framing even if a server sent it unexpectedly without the request header.
- 401/403 map to authentication errors; retryable HTTP/server/network failures participate in listener reconnect policy.

## Redirect and TLS security

TLS peer verification and hostname verification are explicitly enabled.

An HTTPS endpoint may redirect only to HTTPS, preventing downgrade. An HTTP endpoint may redirect to HTTP or HTTPS. `CURLOPT_UNRESTRICTED_AUTH` remains disabled so credentials are not deliberately forwarded across unrelated redirect hosts.

## Dependency boundary

The backend requires libcurl >= 7.66.0. The project pin for automatic source builds lives in `dependencies.json`; do not duplicate the commit SHA in CMake.

The dependency resolution helper must preserve the documented order: parent target, explicit/local source, installed package discovery, then FetchContent.

Installed packages must discover libcurl normally and must never FetchContent it from a downstream project.

## Testing

Keep a loopback synthetic HTTP server test for:

- Basic auth request construction;
- ICY request header;
- exact encoded-byte delivery;
- metadata separation;
- pause/resume across a partial curl receive buffer;
- callback stop;
- final stream info/state/generation;
- authentication failure classification.

Tests must not depend on a public Icecast server or external network availability.
