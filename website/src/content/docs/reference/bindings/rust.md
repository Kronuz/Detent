---
title: "Rust API"
---

The `detent` crate in `bindings/rust/` wraps the C++ client with `cxx`. It's
built when `DETENT_BUILD_RUST=ON`.

---

## `detent::Client`

```rust
impl Client {
    pub fn new(
        defaults_path: &str,
        source_path: Option<&str>,
        cli_overrides: Vec<String>,
        poll_interval_ms: i32,
    ) -> Result<Self, DetentError>;

    pub fn eval(
        &self,
        name: &str,
        hash_val: Option<&str>,
        switch_val: Option<&str>,
    ) -> Result<bool, DetentError>;

    pub fn get(&self, name: &str, switch_val: Option<&str>) -> Result<i64, DetentError>;

    pub fn health(&self) -> HealthInfo;
}
```

```rust
let client = detent::Client::new("/etc/myapp/just_knobs.json", None, vec![], 200)?;
if client.eval("checkout:killswitch_new_path", None, None)? {
    new_checkout();
}
```

---

## `HealthInfo`

```rust
pub struct HealthInfo {
    pub initialized: bool,
    pub source_connected: bool,
    pub accepted_version: u64,
    pub staleness_seconds: f64,
    pub bool_knobs: usize,
    pub int_knobs: usize,
    pub cli_overrides: usize,
}
```

---

## `DetentError`

```rust
pub struct DetentError(pub String);
```

Implements `std::error::Error` and `Display`; the message starts with the error
category, e.g. `MissingKnob: ...`.

:::hint[Not yet in the Rust binding]{.unimplemented}
1. The HTTP distributor source and managed generation source.
2. `read_view()`, a generic `get_as::<T>()`, and panicking helpers.
3. A structured error enum mirroring `ErrorCategory` instead of a string.
4. A process-wide adapter and a `TestKnobs`-style guard for tests.
5. A crate published to crates.io.
:::
