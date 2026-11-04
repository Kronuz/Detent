//! The Rust binding over libdetent_reader, against real stores: T1 (the shared scenarios.json),
//! T2 (store states) and T3 (threads) of docs/design/client-local-source.md. T4 (lifetime) is in
//! lifetime.rs, which needs a process of its own to count descriptors.
mod common;

use common::{fresh, knobs, uid, Writer};
use detent::{Reader, Status, Value};
use serde_json::Value as Json;
use std::os::unix::fs::PermissionsExt;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant};

fn status_of(name: &str) -> Status {
    match name {
        "missing" => Status::Missing,
        "wrong_type" => Status::WrongType,
        "invalid_name" => Status::InvalidName,
        "not_opened" => Status::NotOpened,
        "source_failure" => Status::SourceFailure,
        "unavailable" => Status::Unavailable,
        "untrusted" => Status::Untrusted,
        other => panic!("scenarios.json names an unknown status {other}"),
    }
}

fn run(reader: &Reader, case: &Json) {
    let want = &case["value"];
    let hash = case["hash"].as_str();
    let switch = case["switch"].as_str();
    match case["op"].as_str().unwrap() {
        "revision" => {
            let got = reader.revision(case["namespace"].as_str().unwrap());
            assert_eq!(got, want.as_u64(), "{case}");
        }
        "eval" => {
            let got = reader.eval(case["name"].as_str().unwrap(), case["fallback"].as_bool().unwrap(), hash, switch);
            assert_eq!(got, want.as_bool().unwrap(), "{case}");
        }
        "get" => {
            let got = reader.get(case["name"].as_str().unwrap(), case["fallback"].as_i64().unwrap());
            assert_eq!(got, want.as_i64().unwrap(), "{case}");
        }
        "lookup" => {
            let got = reader.lookup(case["name"].as_str().unwrap(), hash, switch);
            let status = case["status"].as_str().unwrap();
            match (status, got) {
                ("ok", Ok(Value::Bool(b))) => assert_eq!(Some(b), want.as_bool(), "{case}"),
                ("ok", Ok(Value::Int(i))) => assert_eq!(Some(i), want.as_i64(), "{case}"),
                (name, Err(got)) => {
                    assert_eq!(got, status_of(name), "{case}");
                    assert!(want.is_null(), "{case}");
                }
                (_, got) => panic!("{case}: got {got:?}"),
            }
        }
        other => panic!("unknown op {other}"),
    }
}

#[test]
fn scenarios() {
    let path = std::env::var("DETENT_SCENARIOS").expect("DETENT_SCENARIOS names bindings/tests/scenarios.json");
    let table: Json = serde_json::from_str(&std::fs::read_to_string(path).unwrap()).unwrap();
    let namespaces: Vec<&str> = table["namespaces"].as_array().unwrap().iter().map(|n| n.as_str().unwrap()).collect();
    let store = fresh("scenarios");
    let writer = Writer::open(&store);
    let mut reader = None;
    let mut checked = 0;
    for (index, document) in table["publishes"].as_array().unwrap().iter().enumerate() {
        writer.publish(&document.to_string());
        let reader = reader.get_or_insert_with(|| Reader::open(&store, uid(), &namespaces).unwrap());
        for case in table["cases"].as_array().unwrap() {
            if case["after"].as_u64().unwrap() == index as u64 + 1 {
                run(reader, case);
                checked += 1;
            }
        }
    }
    assert_eq!(checked, table["cases"].as_array().unwrap().len());
}

#[test]
fn store_states() {
    let store = fresh("states");
    let reader = Reader::open(&store, uid(), &[]).expect("an absent store opens");
    assert_eq!(reader.lookup("t:rev", None, None), Err(Status::Unavailable));
    assert!(reader.eval("t:flag", true, None, None));
    assert_eq!(reader.get("t:rev", -9), -9);
    assert_eq!(reader.revision("t"), None);

    let writer = Writer::open(&store);
    writer.publish(&knobs(1));
    let deadline = Instant::now() + Duration::from_secs(5);
    while Instant::now() < deadline && reader.lookup("t:rev", None, None).is_err() {
        std::thread::sleep(Duration::from_millis(50)); // an absent store is retried at most once per 200 ms
    }
    assert_eq!(reader.lookup("t:rev", None, None), Ok(Value::Int(1)));
    assert_eq!(reader.refresh(), Ok(()));

    std::fs::set_permissions(&store, std::fs::Permissions::from_mode(0o777)).unwrap();
    let error = Reader::open(&store, uid(), &[]).err().expect("a world-writable store is refused");
    assert_eq!(error.status, Status::Untrusted);
    assert_eq!(error.to_string(), "detent_open: untrusted");
    std::fs::set_permissions(&store, std::fs::Permissions::from_mode(0o700)).unwrap();
    assert_eq!(Reader::open(&store, uid() + 1, &[]).err().map(|e| e.status), Some(Status::Untrusted));
}

#[test]
fn threads() {
    let store = fresh("threads");
    let writer = Writer::open(&store);
    writer.publish(&knobs(1));
    let reader = Reader::open(&store, uid(), &[]).unwrap();
    let last = 100;
    let done = AtomicBool::new(false);
    std::thread::scope(|scope| {
        let readers: Vec<_> = (0..8)
            .map(|_| {
                scope.spawn(|| {
                    let mut seen = 0;
                    loop {
                        match reader.lookup("t:rev", None, None) {
                            Ok(Value::Int(value)) => {
                                assert!(value >= seen, "revision went backwards: {value} after {seen}");
                                seen = value;
                            }
                            other => panic!("lookup during publication returned {other:?}"),
                        }
                        if done.load(Ordering::Acquire) && seen == last {
                            return;
                        }
                    }
                })
            })
            .collect();
        for revision in 2..=last {
            assert_eq!(writer.publish(&knobs(revision)), revision as u64);
        }
        done.store(true, Ordering::Release);
        for reader in readers {
            reader.join().unwrap();
        }
    });
}

#[test]
fn status_names() {
    assert_eq!(Status::Unknown(99).name(), "unknown");
    assert_eq!(Status::Untrusted.name(), "untrusted");
}
