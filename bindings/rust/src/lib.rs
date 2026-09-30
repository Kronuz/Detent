// Section 5's "API surface (bindings and injection)" made concrete for
// Rust, via the `cxx` crate: a thin bridge over detent::Client (see
// src/shim.hh/.cc for the C++ side) -- eval()/get() carry the exact same
// Section 8 precedence chain and Section 4 strict parsing the C++ API,
// Python binding, and detent-agent's CLI all use, reused unchanged.
//
// Scope: file-source only, same as the Python binding -- see
// bindings/python/detent_python.cc's header comment for the rationale
// (HTTP-source polling is not wired in here to avoid pulling detent_http's
// own dependency chain into every Rust build; use detent-agent's
// shared-memory channel for live HTTP-distributed updates instead).

#[cxx::bridge(namespace = "detent_rust")]
mod ffi {
    struct HealthInfo {
        initialized: bool,
        source_connected: bool,
        accepted_version: u64,
        staleness_seconds: f64,
        bool_knobs: usize,
        int_knobs: usize,
        cli_overrides: usize,
    }

    unsafe extern "C++" {
        include!("detent-rust/src/shim.hh");

        type ClientHandle;

        fn new_client(
            defaults_path: &str,
            has_source_path: bool,
            source_path: &str,
            cli_overrides: Vec<String>,
            poll_interval_ms: i32,
        ) -> Result<UniquePtr<ClientHandle>>;

        fn eval(
            client: &ClientHandle,
            name: &str,
            has_hash: bool,
            hash_val: &str,
            has_switch: bool,
            switch_val: &str,
        ) -> Result<bool>;

        fn get_int(client: &ClientHandle, name: &str, has_switch: bool, switch_val: &str) -> Result<i64>;

        fn health(client: &ClientHandle) -> HealthInfo;
    }
}

use cxx::UniquePtr;

/// A live, warm configuration client -- construction validates the baked
/// defaults file (and, if given, an initial read of the source file)
/// synchronously and returns an error rather than panicking (unlike the
/// C++ API's `Client` constructor, which throws -- Rust's idiom is
/// `Result`, not an exception).
pub struct Client {
    inner: UniquePtr<ffi::ClientHandle>,
}

/// Everything a `KnobError` can report: which of Section 8's failure
/// categories, the offending knob name, and a human-readable detail.
/// Never carries `hash_val`/`switch_val` (Section 12: they may be user/
/// tenant identifiers and must never be logged or surfaced as a label).
#[derive(Debug, Clone)]
pub struct DetentError(pub String);

impl std::fmt::Display for DetentError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}
impl std::error::Error for DetentError {}

impl Client {
    /// Builds a client from a baked-defaults file, optionally polling a
    /// local snapshot file for live updates (`source_path`). Mirrors the
    /// C++ `InitOptions`/`Client` constructor's fail-fast contract (Section
    /// 9): an invalid/missing defaults file is an `Err`, not a panic.
    pub fn new(
        defaults_path: &str,
        source_path: Option<&str>,
        cli_overrides: Vec<String>,
        poll_interval_ms: i32,
    ) -> Result<Self, DetentError> {
        let inner = ffi::new_client(
            defaults_path,
            source_path.is_some(),
            source_path.unwrap_or(""),
            cli_overrides,
            poll_interval_ms,
        )
        .map_err(|e| DetentError(e.to_string()))?;
        Ok(Client { inner })
    }

    /// Evaluates a bool knob. Section 8 precedence: CLI override ->
    /// `switch_val` group match -> `rollout_pct` hash gate (if
    /// `rollout_pct` is set and `hash_val` is given) -> default.
    pub fn eval(
        &self,
        name: &str,
        hash_val: Option<&str>,
        switch_val: Option<&str>,
    ) -> Result<bool, DetentError> {
        ffi::eval(
            &self.inner,
            name,
            hash_val.is_some(),
            hash_val.unwrap_or(""),
            switch_val.is_some(),
            switch_val.unwrap_or(""),
        )
        .map_err(|e| DetentError(e.to_string()))
    }

    /// Reads an int knob's value. `switch_val` is accepted for signature
    /// symmetry with `eval()` but ignored (Section 8: the int model has no
    /// per-group overrides in v1).
    pub fn get(&self, name: &str, switch_val: Option<&str>) -> Result<i64, DetentError> {
        ffi::get_int(&self.inner, name, switch_val.is_some(), switch_val.unwrap_or(""))
            .map_err(|e| DetentError(e.to_string()))
    }

    /// Section 12 diagnostic snapshot.
    pub fn health(&self) -> ffi::HealthInfo {
        ffi::health(&self.inner)
    }
}
