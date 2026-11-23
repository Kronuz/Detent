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

    pub fn get_as<T: Knob>(
        &self,
        name: &str,
        fallback: T,
        hash: Option<&str>,
        switch_value: Option<&str>,
    ) -> T;

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
[standard precedence](/Detent/reference/evaluation/). `get_as` reads it as
`T`, as C++'s `get_as<T>`: `bool` (`false` for 0, `true` for 1) or a
fixed-width integer (`i8` to `i64`, `u8` to `u64`). Both return `fallback` on
any error, and `get_as` also for a value outside `T`'s range (for `bool`, other
than 0 or 1); `lookup` returns the status instead.
`Reader` is `Send + Sync` and not `Clone`: share one through `Arc`. The handle
closes on `Drop`, which can't overlap a lookup because lookups borrow `&self`.

```rust
let reader = detent::Reader::open("agent-state", agent_uid, &[], 0)?;
if reader.get_as("checkout:killswitch_new_path", false, None, None) {
    disable_new_checkout();
}
let max = reader.get("search:max_results", 25, None, None);
let on = reader.get_as("payments:new_processor", false, Some("user-42"), None);
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
