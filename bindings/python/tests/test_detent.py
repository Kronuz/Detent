"""Real end-to-end proof, not a mock: constructs a real detent::Client
through the pybind11 binding, against a real defaults file, and checks the
exact same scenarios the Rust binding and the C++ test suite cover --
int64 reads, get_bool's checked conversion, missing-knob errors, and
hash_val/switch_val precedence. Run with `python3 -m pytest bindings/python/tests`
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
        "checkout:killswitch_new_path": {"default": 0},
        "search:max_results": {"default": 25},
        "search:two": {"default": 2},
        "search:minus_one": {"default": -1},
        "payments:new_processor": {
            "default": 0,
            "rollout_pct": 10,
            "rollout_value": 1,
            "overrides": {"internal_employees": 1},
        },
        "search:batch_size": {
            "default": 25,
            "rollout_pct": 100,
            "rollout_value": 50,
            "overrides": {"beta": 7, "canary": {"rollout_pct": 0}},
        },
    },
}


def make_client():
    fd, path = tempfile.mkstemp(suffix=".json")
    with os.fdopen(fd, "w") as f:
        json.dump(DEFAULTS, f)
    return detent.Client(path), path


def test_int_and_bool_reads():
    client, path = make_client()
    try:
        assert client.get("checkout:killswitch_new_path") == 0
        assert client.get_bool("checkout:killswitch_new_path") is False
        assert client.get("search:max_results") == 25
    finally:
        os.remove(path)


def test_missing_and_conversion_errors():
    client, path = make_client()
    try:
        try:
            client.get("nope:not_here")
            assert False, "expected DetentError"
        except detent.DetentError as e:
            assert "MissingKnob" in str(e)

        for name in ("search:max_results", "search:two", "search:minus_one"):  # none is 0 or 1
            try:
                client.get_bool(name)
                assert False, "expected DetentError"
            except detent.DetentError as e:
                assert "ConversionFailed" in str(e)
    finally:
        os.remove(path)


def test_hash_val_and_switch_val_precedence():
    client, path = make_client()
    try:
        # No hash_val, no switch_val: rollout skipped entirely, plain default.
        assert client.get_bool("payments:new_processor") is False

        # hash_val only: gated by the 10% rollout, stably bucketed -- these
        # exact (name, hash_val) pairs are pinned against SHA-256 in
        # test_hash_golden.cc and reconfirmed via the Rust binding.
        assert client.get_bool("payments:new_processor", hash_val="user-1") is False
        assert client.get_bool("payments:new_processor", hash_val="user-42") is True

        # switch_val naming a matching group: wins outright, even over a
        # hash_val that would otherwise fail the rollout gate.
        assert (
            client.get_bool("payments:new_processor", hash_val="user-2", switch_val="internal_employees")
            is True
        )
    finally:
        os.remove(path)


def test_integer_targeting():
    client, path = make_client()
    try:
        # A 100% rollout serves rollout_value to every hash; no hash gets the default.
        assert client.get("search:batch_size") == 25
        assert client.get("search:batch_size", hash_val="user-1") == 50
        # A forced group wins; a 0% group serves the default to every hash.
        assert client.get("search:batch_size", hash_val="user-1", switch_val="beta") == 7
        assert client.get("search:batch_size", hash_val="user-1", switch_val="canary") == 25
        # An unknown group falls through to the knob-wide rollout.
        assert client.get("search:batch_size", hash_val="user-1", switch_val="other") == 50
    finally:
        os.remove(path)


def test_health_snapshot():
    client, path = make_client()
    try:
        h = client.health()
        assert h["initialized"] is True
        assert h["accepted_version"] == 0  # baked defaults only, no source configured
        assert h["knobs"] == 6
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
        test_int_and_bool_reads,
        test_missing_and_conversion_errors,
        test_hash_val_and_switch_val_precedence,
        test_integer_targeting,
        test_health_snapshot,
        test_invalid_defaults_path_raises_not_crashes,
    ]
    for t in tests:
        t()
        print(f"{t.__name__} OK")
    print("all detent python binding tests passed")
