# C++ style and API guidance

`icecast-cxx` should feel like a deliberately designed C++ library, not a thin transliteration of C APIs.

Developer experience is a primary design requirement. Prefer interfaces that remove incidental C-level ceremony while preserving important protocol, ownership, lifetime, and error semantics.

## Language level

The current recommended baseline is modern C++ with C++20 as the initial target unless a later compatibility decision changes it.

Use modern standard-library facilities where they simplify ownership and interfaces without forcing unnecessary runtime abstraction.

## Naming and files

Prefer:

- `lower_snake_case` for functions, variables, members, aliases, and user-defined type names;
- `.hxx` for C++ headers;
- `.cxx` for C++ implementation files;
- clear semantic names rather than C-style prefixes carried over only because an upstream library uses them.

Public names should describe Icecast concepts, not the backing library. For example, do not expose libcurl/libshout naming in otherwise backend-independent public types.

## User-defined types

Prefer `struct` for project-defined types.

Avoid `public:` / `private:` access-control sections and access-control-heavy class designs unless there is an exceptional technical reason that should be discussed first.

A justified exception is an active resource-owning/networking handle whose implementation must stay opaque to preserve invariants, ABI flexibility, or backend independence. Do not expose native handles or mutable implementation state simply to avoid encapsulation.

For ordinary public data/configuration/result models, favor straightforward value-oriented structs.

## Boolean expressions

Prefer C++ alternative operator tokens:

- `not`
- `and`
- `or`

Use explicit parentheses around boolean operands when they improve readability.

Preferred:

```cpp
if ((not valid) or (not connected)) {
    // ...
}
```

Avoid changing this style to `!`, `&&`, and `||` merely to match common defaults in unrelated projects.

## Function declarations

Keep function parameter lists on one line.

Do not automatically reflow parameters vertically just because a formatter normally would. If a signature becomes so complicated that one-line parameters are difficult to read, first consider whether the API itself is taking too many loosely related parameters and would benefit from a configuration/value type.

## `static_assert`

Keep each `static_assert(...)` statement on one line.

## API design

Prioritize convenience without hiding semantics.

Prefer:

- strong/value types instead of groups of ambiguous strings or integers;
- RAII for resource lifetime;
- move semantics for unique active resources;
- `std::span`/views for clearly borrowed contiguous data;
- explicit ownership transfer when asynchronous retention is needed;
- bounded queues and explicit backpressure;
- structured errors/results rather than raw third-party error codes;
- `std::chrono` duration/time types rather than unit-ambiguous numeric parameters;
- extensible MIME/content-type representation instead of codec policy enums where the protocol accepts arbitrary media types;
- capability models rather than scattered platform preprocessor checks in application code.

Do not require callers to know libcurl, libshout, Icecast server-internal structs, or browser JavaScript internals to perform ordinary operations.

## Runtime polymorphism

Avoid unnecessary runtime polymorphism.

Prefer concrete types, composition, concepts/templates when they genuinely improve generic use, and variants for finite alternatives.

Do not introduce public virtual base classes for transports merely to share an API between native and browser builds. Different implementations may share semantic models without sharing a public inheritance hierarchy.

Use type erasure or opaque implementation internally when it meaningfully improves ABI/backend isolation, not as a default everywhere.

## Ownership and byte streaming

Streaming APIs must make buffer lifetime clear.

For listener callbacks, a borrowed `std::span<const std::byte>`-style view is a suitable direction when the contract states that the data remains valid only for the callback/operation duration.

For publishing, distinguish convenient copying from explicit ownership transfer. Do not design APIs where the caller must guess how long a pointer remains in use.

Avoid one heap allocation per network chunk when the same behavior can be expressed with reusable buffers or borrowed spans.

## Errors and exceptions

Operational network/protocol failures are expected events and should be representable as values.

Prefer a project-owned structured result/error model for ordinary operations. Do not make raw `CURLcode`, libshout integer errors, `errno`, or browser-specific failures the stable public API.

Exceptions may still be appropriate for programming errors or convenience layers if deliberately designed later, but do not make exception behavior the only way to observe routine network failures without a specific API decision.

## C interop

Wrap C dependencies behind narrow internal boundaries.

When interacting with C APIs:

- immediately translate ownership into RAII where possible;
- translate backend error values into project error information at the boundary;
- avoid leaking C allocation/free conventions to users;
- avoid exposing mutable C structs as project models;
- keep C headers out of public headers unless the public ABI truly requires them.

The objective is not to hide that Icecast/Xiph C libraries exist; it is to make them implementation details rather than obligations placed on every C++ caller.

## Platform-specific code

Keep platform/backend checks close to their implementation target.

Shared semantic code should not accumulate branches such as `#ifdef __EMSCRIPTEN__` throughout public-facing logic when a transport/backend component can isolate the difference.

When behavior is genuinely different across platforms, expose that difference through documented capability/state models rather than pretending unsupported behavior is available.

## Python compatibility

The future `pycecast` bindings matter, but do not deform the C++ API around Python.

Good C++ value types, explicit ownership, structured errors, and clear active-resource lifetimes are generally bindable already.

Do not add a stable C ABI, Python-specific wrapper types, or Python dependencies until a concrete cross-language/ABI requirement justifies them.

## Formatting and cleanup

Do not perform unrelated style rewrites while making focused changes.

When a formatter is eventually selected/configured, project-specific rules in this document should take precedence over a formatter's default preferences, especially:

- alternative operator tokens;
- one-line function parameter lists;
- one-line `static_assert(...)` statements;
- the project's `struct`-first type style.

If an automated formatting tool cannot represent these conventions reliably, configure it appropriately or limit its scope rather than letting it silently redefine the project's style.
