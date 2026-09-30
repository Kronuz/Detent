"""Real end-to-end proof, not a mock: constructs a real detent::Client
through the pybind11 binding, against a real defaults file, and checks the
exact same scenarios the Rust binding and the C++ test suite cover --
bool/int reads, missing/wrong-type errors, and Section 8's hash_val/
switch_val precedence. Run with `python3 -m pytest bindings/python/tests`
(or plain `python3 test_detent.py`, since every check is a bare assert)
after building the `detent_python` CMake target and putting the resulting
`detent*.so` on PYTHONPATH.
"""
import json
import os
import tempfile

import detent

DEFAULTS = {
    "schema_version": 1,
    "knobs": {
        "checkout:killswitch_new_path": {"type": "bool", "default": False},
        "search:max_results": {"type": "int", "default": 25},
        "payments:new_processor": {
            "type": "bool",
            "default": False,
            "rollout_pct": 10,
            "overrides": {"internal_employees": True},
        },
    },
}


def make_client():
    fd, path = tempfile.mkstemp(suffix=".json")
    with os.fdopen(fd, "w") as f:
        json.dump(DEFAULTS, f)
    return detent.Client(path), path


def test_bool_and_int_reads():
    client, path = make_client()
    try:
        assert client.eval("checkout:killswitch_new_path") is False
        assert client.get("search:max_results") == 25
    finally:
        os.remove(path)


def test_missing_and_wrong_type_errors():
    client, path = make_client()
    try:
        try:
            client.eval("nope:not_here")
            assert False, "expected DetentError"
        except detent.DetentError as e:
            assert "MissingKnob" in str(e)

        try:
            client.eval("search:max_results")  # int knob, bool read
            assert False, "expected DetentError"
        except detent.DetentError as e:
            assert "WrongType" in str(e)
    finally:
        os.remove(path)


def test_hash_val_and_switch_val_precedence():
    client, path = make_client()
    try:
        # No hash_val, no switch_val: rollout skipped entirely, plain default.
        assert client.eval("payments:new_processor") is False

        # hash_val only: gated by the 10% rollout, stably bucketed -- these
        # exact (name, hash_val) pairs were already proven against the real
        # SHA-256 hash in test_hash_golden.cc and reconfirmed via the Rust
        # binding and the CLI earlier in this same session.
        assert client.eval("payments:new_processor", hash_val="user-1") is False
        assert client.eval("payments:new_processor", hash_val="user-42") is True

        # switch_val naming a matching group: wins outright, even over a
        # hash_val that would otherwise fail the rollout gate.
        assert (
            client.eval("payments:new_processor", hash_val="user-2", switch_val="internal_employees")
            is True
        )

        # get() ignores switch_val (int model has no group overrides, Section 8).
        assert client.get("search:max_results", switch_val="anything") == 25
    finally:
        os.remove(path)


def test_health_snapshot():
    client, path = make_client()
    try:
        h = client.health()
        assert h["initialized"] is True
        assert h["accepted_version"] == 0  # baked defaults only, no source configured
        assert h["bool_knobs"] == 2
        assert h["int_knobs"] == 1
    finally:
        os.remove(path)


def test_invalid_defaults_path_raises_not_crashes():
    try:
        detent.Client("/nonexistent/path/defaults.json")
        assert False, "expected DetentError"
    except detent.DetentError:
        pass


if __name__ == "__main__":
    tests = [
        test_bool_and_int_reads,
        test_missing_and_wrong_type_errors,
        test_hash_val_and_switch_val_precedence,
        test_health_snapshot,
        test_invalid_defaults_path_raises_not_crashes,
    ]
    for t in tests:
        t()
        print(f"{t.__name__} OK")
    print("all detent python binding tests passed")
