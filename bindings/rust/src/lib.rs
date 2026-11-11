// The Rust binding, via the `cxx` crate: a thin bridge over detent::Client (see
// src/shim.hh/.cc for the C++ side) -- get()/get_bool() carry the exact same
// precedence chain and strict parsing the C++ API,
// Python binding, and detent-agent's CLI all use, reused unchanged.
//
// Scope: file-source only, same as the Python binding -- see
// bindings/python/detent_python.cc's header comment for the rationale
// (use detent-agent's shared-memory channel for managed updates).

#[cxx::bridge(namespace = "detent_rust")]
mod ffi {
    struct HealthInfo {
        initialized: bool,
        source_connected: bool,
        accepted_version: u64,
        staleness_seconds: f64,
        knobs: usize,
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

        fn get(
            client: &ClientHandle,
            name: &str,
            has_hash: bool,
            hash_val: &str,
            has_switch: bool,
            switch_val: &str,
        ) -> Result<i64>;

        fn get_bool(
            client: &ClientHandle,
            name: &str,
            has_hash: bool,
            hash_val: &str,
            has_switch: bool,
            switch_val: &str,
        ) -> Result<bool>;

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

/// Everything a `KnobError` can report: which failure
/// category, the offending knob name, and a human-readable detail.
/// Never carries `hash_val`/`switch_val` (they may be user/
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

    /// Reads a knob's int64 value. Precedence: CLI override -> `switch_val`
    /// group match (a forced value, or `rollout_value` for a hash inside the
    /// group's percentage) -> `rollout_pct` (`rollout_value` for a hash
    /// inside it) -> default.
    pub fn get(
        &self,
        name: &str,
        hash_val: Option<&str>,
        switch_val: Option<&str>,
    ) -> Result<i64, DetentError> {
        ffi::get(
            &self.inner,
            name,
            hash_val.is_some(),
            hash_val.unwrap_or(""),
            switch_val.is_some(),
            switch_val.unwrap_or(""),
        )
        .map_err(|e| DetentError(e.to_string()))
    }

    /// `get()`, then 0 is `false` and 1 is `true`; any other value is a
    /// `ConversionFailed` error.
    pub fn get_bool(
        &self,
        name: &str,
        hash_val: Option<&str>,
        switch_val: Option<&str>,
    ) -> Result<bool, DetentError> {
        ffi::get_bool(
            &self.inner,
            name,
            hash_val.is_some(),
            hash_val.unwrap_or(""),
            switch_val.is_some(),
            switch_val.unwrap_or(""),
        )
        .map_err(|e| DetentError(e.to_string()))
    }

    /// Diagnostic snapshot.
    pub fn health(&self) -> ffi::HealthInfo {
        ffi::health(&self.inner)
    }
}
