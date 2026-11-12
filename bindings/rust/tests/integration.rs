// Real end-to-end proof, not a mock: constructs a real detent::Client
// through the cxx bridge, against a real defaults file, and checks the
// exact same scenarios the Python binding and the C++ test suite cover --
// int64 reads, get_bool's checked conversion, missing-knob errors, and
// hash_val/switch_val precedence.
use std::io::Write;

fn write_defaults() -> tempfile_path::TempFile {
    let contents = r#"{
        "schema_version": 1,
        "knobs": {
            "checkout:killswitch_new_path": { "default": 0 },
            "search:max_results": { "default": 25 },
            "search:two": { "default": 2 },
            "search:minus_one": { "default": -1 },
            "payments:new_processor": { "default": 0, "rollout_pct": 10, "rollout_value": 1, "overrides": { "internal_employees": 1 } },
            "search:batch_size": { "default": 25, "rollout_pct": 100, "rollout_value": 50, "overrides": { "beta": 7, "canary": { "rollout_pct": 0 } } }
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
fn int_and_bool_reads() {
    let defaults = write_defaults();
    let client = detent::Client::new(defaults.0.to_str().unwrap(), None, vec![], 200).unwrap();

    assert_eq!(client.get("checkout:killswitch_new_path", None, None).unwrap(), 0);
    assert_eq!(client.get_bool("checkout:killswitch_new_path", None, None).unwrap(), false);
    assert_eq!(client.get("search:max_results", None, None).unwrap(), 25);
}

#[test]
fn missing_and_conversion_errors() {
    let defaults = write_defaults();
    let client = detent::Client::new(defaults.0.to_str().unwrap(), None, vec![], 200).unwrap();

    let missing = client.get("nope:not_here", None, None);
    assert!(missing.is_err());
    assert!(missing.unwrap_err().to_string().contains("MissingKnob"));

    // None of 25, 2 and -1 is 0 or 1.
    for name in ["search:max_results", "search:two", "search:minus_one"] {
        let not_bool = client.get_bool(name, None, None);
        assert!(not_bool.is_err());
        assert!(not_bool.unwrap_err().to_string().contains("ConversionFailed"));
    }
}

#[test]
fn hash_val_and_switch_val_precedence() {
    let defaults = write_defaults();
    let client = detent::Client::new(defaults.0.to_str().unwrap(), None, vec![], 200).unwrap();

    // No hash_val, no switch_val: rollout skipped entirely, plain default.
    assert_eq!(client.get_bool("payments:new_processor", None, None).unwrap(), false);

    // hash_val only: gated by the 10% rollout, stably bucketed -- these
    // exact (name, hash_val) pairs are pinned against SHA-256 in
    // test_hash_golden.cc and reconfirmed via the Python binding.
    assert_eq!(client.get_bool("payments:new_processor", Some("user-1"), None).unwrap(), false);
    assert_eq!(client.get_bool("payments:new_processor", Some("user-42"), None).unwrap(), true);

    // switch_val naming a matching group: wins outright, even over a
    // hash_val that would otherwise fail the rollout gate.
    assert_eq!(
        client
            .get_bool("payments:new_processor", Some("user-2"), Some("internal_employees"))
            .unwrap(),
        true
    );
}

#[test]
fn integer_targeting() {
    let defaults = write_defaults();
    let client = detent::Client::new(defaults.0.to_str().unwrap(), None, vec![], 200).unwrap();

    // A 100% rollout serves rollout_value to every hash; no hash gets the default.
    assert_eq!(client.get("search:batch_size", None, None).unwrap(), 25);
    assert_eq!(client.get("search:batch_size", Some("user-1"), None).unwrap(), 50);
    // A forced group wins; a 0% group serves the default to every hash.
    assert_eq!(client.get("search:batch_size", Some("user-1"), Some("beta")).unwrap(), 7);
    assert_eq!(client.get("search:batch_size", Some("user-1"), Some("canary")).unwrap(), 25);
    // An unknown group falls through to the knob-wide rollout.
    assert_eq!(client.get("search:batch_size", Some("user-1"), Some("other")).unwrap(), 50);
}

#[test]
fn health_snapshot() {
    let defaults = write_defaults();
    let client = detent::Client::new(defaults.0.to_str().unwrap(), None, vec![], 200).unwrap();
    let h = client.health();
    assert!(h.initialized);
    assert_eq!(h.accepted_version, 0); // baked defaults only, no source configured
    assert_eq!(h.knobs, 4);
}

#[test]
fn invalid_defaults_path_is_an_error_not_a_panic() {
    let result = detent::Client::new("/nonexistent/path/defaults.json", None, vec![], 200);
    assert!(result.is_err());
}
