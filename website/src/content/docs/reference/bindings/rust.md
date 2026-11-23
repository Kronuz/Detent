---
title: "Rust API"
---

The `detent` library in `bindings/rust/` (package `detent-rust`) declares
`libdetent_reader`'s C ABI with `extern "C"` and links the shared library CMake
builds; cargo compiles no C++. Build CMake first with `DETENT_BUILD_RUST=ON`,
which also runs `cargo build`; set `DETENT_BUILD_DIR` when the build directory
isn't `<repo>/build`.

---

## `detent::Reader`

```rust
impl Reader {
    pub fn open(
        store: impl AsRef<Path>,
        publisher_uid: u32,
        namespaces: &[&str],
        mapping_bytes: u64,
    ) -> Result<Reader, ReaderError>;

    pub fn get(
        &self,
        name: &str,
        fallback: i64,
        hash: Option<&str>,
        switch_value: Option<&str>,
    ) -> i64;

    pub fn get_bool(
        &self,
        name: &str,
        fallback: bool,
        hash: Option<&str>,
        switch_value: Option<&str>,
    ) -> bool;

    pub fn lookup(
        &self,
        name: &str,
        fallback: i64,
        hash: Option<&str>,
        switch_value: Option<&str>,
    ) -> Result<i64, Status>;

    pub fn refresh(&self) -> Result<(), Status>;

    pub fn revision(&self, namespace: &str) -> Option<u64>;
}
```

`open` takes the agent's store directory, the uid the agent runs as, the
namespaces to limit the reader to (empty means all, including later ones), and
a cap on the bytes it maps (0 means 512 MiB). The path is passed as raw bytes,
so non-UTF-8 paths work. A store that doesn't exist yet opens, and lookups
return the fallback with `Status::Unavailable`.

`get` returns a knob's `i64` value with the
[standard precedence](/Detent/reference/evaluation/). `get_bool` returns
`false` for 0 and `true` for 1. Both return `fallback` on any error, and
`get_bool` also for any other value; `lookup` returns the status instead.
`Reader` is `Send + Sync` and not `Clone`: share one through `Arc`. The handle
closes on `Drop`, which can't overlap a lookup because lookups borrow `&self`.

```rust
let reader = detent::Reader::open("agent-state", agent_uid, &[], 0)?;
if reader.get_bool("checkout:killswitch_new_path", false, None, None) {
    disable_new_checkout();
}
let max = reader.get("search:max_results", 25, None, None);
let on = reader.get_bool("payments:new_processor", false, Some("user-42"), None);
```

---

## `Status`

```rust
#[non_exhaustive]
pub enum Status {
    Missing, InvalidName, NotOpened, SourceFailure,
    Unavailable, Untrusted, InvalidArgument, NoMemory, Io,
    Unknown(u32),
}
```

`Status::name()` returns the core's name for the status (`"missing"`,
`"untrusted"`). `Unknown` keeps a status newer than the binding. The
[Reader reference](/Detent/reference/cpp-api/reader/#lookups) says what each
one means.

---

## `ReaderError`

```rust
pub struct ReaderError { pub status: Status }
```

The only error `open` returns. It implements `std::error::Error` and displays
as `detent_open: <status name>`. An ABI version mismatch is
`Status::InvalidArgument`.

:::hint[Not yet in the Rust binding]{.unimplemented}
A crate published to crates.io.
:::
