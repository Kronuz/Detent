// Real end-to-end proof, not a mock: constructs a real detent::Client
// through the cxx bridge, against a real defaults file, and checks the
// exact same scenarios the Python binding and the C++ test suite cover --
// bool/int reads, missing/wrong-type errors, and Section 8's hash_val/
// switch_val precedence.
use std::io::Write;

fn write_defaults() -> tempfile_path::TempFile {
    let contents = r#"{
        "schema_version": 1,
        "knobs": {
            "checkout:killswitch_new_path": { "type": "bool", "default": false },
            "search:max_results": { "type": "int", "default": 25 },
            "payments:new_processor": { "type": "bool", "default": false, "rollout_pct": 10, "overrides": { "internal_employees": true } }
        }
    }"#;
    use std::sync::atomic::{AtomicU64, Ordering};
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    // Rust's test harness runs tests in parallel within one process, so a
    // PID-only filename collides across tests running at the same time
    // (confirmed: caused a real "invalid JSON" flake from one test
    // truncating another's file mid-read). A per-call counter makes every
    // temp file unique regardless of concurrency.
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let path = std::env::temp_dir().join(format!("detent_rust_test_{}_{}.json", std::process::id(), n));
    let mut f = std::fs::File::create(&path).unwrap();
    f.write_all(contents.as_bytes()).unwrap();
    tempfile_path::TempFile(path)
}

mod tempfile_path {
    pub struct TempFile(pub std::path::PathBuf);
    impl Drop for TempFile {
        fn drop(&mut self) {
            let _ = std::fs::remove_file(&self.0);
        }
    }
}

#[test]
fn bool_and_int_reads() {
    let defaults = write_defaults();
    let client = detent::Client::new(defaults.0.to_str().unwrap(), None, vec![], 200).unwrap();

    assert_eq!(client.eval("checkout:killswitch_new_path", None, None).unwrap(), false);
    assert_eq!(client.get("search:max_results", None).unwrap(), 25);
}

#[test]
fn missing_and_wrong_type_errors() {
    let defaults = write_defaults();
    let client = detent::Client::new(defaults.0.to_str().unwrap(), None, vec![], 200).unwrap();

    let missing = client.eval("nope:not_here", None, None);
    assert!(missing.is_err());
    assert!(missing.unwrap_err().to_string().contains("MissingKnob"));

    let wrong_type = client.eval("search:max_results", None, None);
    assert!(wrong_type.is_err());
    assert!(wrong_type.unwrap_err().to_string().contains("WrongType"));
}

#[test]
fn hash_val_and_switch_val_precedence() {
    let defaults = write_defaults();
    let client = detent::Client::new(defaults.0.to_str().unwrap(), None, vec![], 200).unwrap();

    // No hash_val, no switch_val: rollout skipped entirely, plain default.
    assert_eq!(client.eval("payments:new_processor", None, None).unwrap(), false);

    // hash_val only: gated by the 10% rollout, stably bucketed -- these
    // exact (name, hash_val) pairs were already proven against the real
    // SHA-256 hash in test_hash_golden.cc and reconfirmed via the Python
    // binding and the CLI earlier in this same session.
    assert_eq!(client.eval("payments:new_processor", Some("user-1"), None).unwrap(), false);
    assert_eq!(client.eval("payments:new_processor", Some("user-42"), None).unwrap(), true);

    // switch_val naming a matching group: wins outright, even over a
    // hash_val that would otherwise fail the rollout gate.
    assert_eq!(
        client
            .eval("payments:new_processor", Some("user-2"), Some("internal_employees"))
            .unwrap(),
        true
    );

    // get() ignores switch_val (int model has no group overrides, Section 8).
    assert_eq!(client.get("search:max_results", Some("anything")).unwrap(), 25);
}

#[test]
fn health_snapshot() {
    let defaults = write_defaults();
    let client = detent::Client::new(defaults.0.to_str().unwrap(), None, vec![], 200).unwrap();
    let h = client.health();
    assert!(h.initialized);
    assert_eq!(h.accepted_version, 0); // baked defaults only, no source configured
    assert_eq!(h.bool_knobs, 2);
    assert_eq!(h.int_knobs, 1);
}

#[test]
fn invalid_defaults_path_is_an_error_not_a_panic() {
    let result = detent::Client::new("/nonexistent/path/defaults.json", None, vec![], 200);
    assert!(result.is_err());
}
