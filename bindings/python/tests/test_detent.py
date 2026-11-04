"""The Python binding over libdetent_reader, against real stores.

Every check publishes real generations through the shared test writer (libdetent_testing, loaded with
ctypes) into a store under DETENT_TEST_ROOT, and reads them back through detent.Reader. Run by CTest as
`detent_python_test`, or directly with DETENT_TESTING_LIB, DETENT_TEST_ROOT, DETENT_SCENARIOS and
PYTHONPATH set (plain `python3 test_detent.py` or pytest).

Rows (docs/design/client-local-source.md): T1 parity with the shared scenarios.json, T2 store states,
T3 threads, T4 lifetime, and the Python-only type checks.
"""
import ctypes
import json
import os
import shutil
import threading
import time
from pathlib import Path

import detent

LIB = ctypes.CDLL(os.environ["DETENT_TESTING_LIB"])
LIB.detent_testing_open.restype = ctypes.c_void_p
LIB.detent_testing_open.argtypes = [ctypes.c_char_p]
LIB.detent_testing_open_error.restype = ctypes.c_char_p
LIB.detent_testing_publish.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t,
                                       ctypes.POINTER(ctypes.c_uint64)]
LIB.detent_testing_error.restype = ctypes.c_char_p
LIB.detent_testing_error.argtypes = [ctypes.c_void_p]
LIB.detent_testing_close.argtypes = [ctypes.c_void_p]

ROOT = Path(os.environ.get("DETENT_TEST_ROOT", Path.cwd() / ".scratch")) / f"python-{os.getpid()}"
UID = os.getuid()
SCENARIOS = json.load(open(os.environ["DETENT_SCENARIOS"]))


class Writer:
    """The test generation writer: publish(knobs) writes revision N+1 into the store."""

    def __init__(self, store):
        self.handle = LIB.detent_testing_open(os.fsencode(store))
        assert self.handle, LIB.detent_testing_open_error()

    def publish(self, document):
        text = json.dumps(document).encode()
        revision = ctypes.c_uint64()
        if LIB.detent_testing_publish(self.handle, text, len(text), ctypes.byref(revision)) != 0:
            raise AssertionError(LIB.detent_testing_error(self.handle))
        return revision.value

    def close(self):
        LIB.detent_testing_close(self.handle)
        self.handle = None


def fresh(name):
    path = ROOT / name
    shutil.rmtree(path, ignore_errors=True)
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def knobs(**values):
    return {"schema_version": 1, "knobs": {f"t:{key}": {"type": "int", "default": value}
                                           for key, value in values.items()}}


def descriptors():
    return len(os.listdir("/dev/fd"))


# T1: the table the C++ and Rust tests also run.
def test_scenarios():
    store = fresh("scenarios")
    writer = Writer(store)
    reader = None
    checked = 0
    for number, document in enumerate(SCENARIOS["publishes"], start=1):
        writer.publish(document)
        if reader is None:
            reader = detent.Reader(store, UID, SCENARIOS["namespaces"])
        for case in SCENARIOS["cases"]:
            if case["after"] != number:
                continue
            check_case(reader, case)
            checked += 1
    assert checked == len(SCENARIOS["cases"])
    writer.close()


def check_case(reader, case):
    op, want = case["op"], case["value"]
    where = json.dumps(case)
    if op == "revision":
        assert reader.revision(case["namespace"]) == want, where
        return
    hash_, switch = case.get("hash"), case.get("switch")
    if op == "eval":
        got = reader.eval(case["name"], case["fallback"], hash=hash_, switch_value=switch)
        assert got is want, where
    elif op == "get":
        got = reader.get(case["name"], case["fallback"])
        assert got == want and type(got) is int, where
    else:
        got = reader.lookup(case["name"], hash=hash_, switch_value=switch)
        assert got.status.name.lower() == case["status"], (where, got)
        assert bool(got) == (case["status"] == "ok"), where
        assert got.value == want and type(got.value) is type(want), (where, got)


# T2: an absent store opens and answers UNAVAILABLE until a generation appears; an untrusted store refuses.
def test_store_states():
    store = fresh("states")
    reader = detent.Reader(store, UID)
    miss = reader.lookup("t:rev")
    assert miss.status == detent.Status.UNAVAILABLE and miss.value is None and not miss, miss
    assert reader.eval("t:flag", True) is True and reader.get("t:rev", -9) == -9
    assert reader.revision("t") is None
    writer = Writer(store)
    writer.publish(knobs(rev=1))
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:  # the core retries an absent store at most once per 200 ms
        if reader.lookup("t:rev"):
            break
        time.sleep(0.05)
    assert reader.lookup("t:rev").value == 1
    assert reader.refresh() == detent.Status.OK

    os.chmod(store, 0o777)
    for args in ((store, UID), ):
        try:
            detent.Reader(*args)
            raise AssertionError("a world-writable store opened")
        except detent.ReaderError as error:
            assert error.status == detent.Status.UNTRUSTED, error
            assert str(error) == "detent_open: untrusted", error
    os.chmod(store, 0o700)
    try:
        detent.Reader(store, UID + 1)
        raise AssertionError("a store owned by another uid opened")
    except detent.ReaderError as error:
        assert error.status == detent.Status.UNTRUSTED
    writer.close()


# T3: eight threads read while the writer publishes 100 revisions.
def test_threads():
    store = fresh("threads")
    writer = Writer(store)
    writer.publish(knobs(rev=1))
    reader = detent.Reader(store, UID)
    last = 100
    failures = []
    done = threading.Event()

    def read():
        seen = 0
        try:
            while True:
                result = reader.lookup("t:rev")
                if result.status != detent.Status.OK or result.value < seen:
                    failures.append((result, seen))
                    return
                seen = result.value
                if done.is_set() and seen == last:
                    return
        except Exception as error:  # a thread must never die quietly
            failures.append(error)

    threads = [threading.Thread(target=read) for _ in range(8)]
    for thread in threads:
        thread.start()
    for revision in range(2, last + 1):
        assert writer.publish(knobs(rev=revision)) == revision
    done.set()
    for thread in threads:
        thread.join(30)
        assert not thread.is_alive()
    assert not failures, failures
    writer.close()


# T4: opening, looking up and dropping a reader leaves no descriptor or thread behind; a forked child
# keeps a working handle and sees the next publish.
def test_lifetime():
    store = fresh("lifetime")
    writer = Writer(store)
    writer.publish(knobs(rev=1))
    threads = threading.active_count()
    before = descriptors()
    for _ in range(20):
        reader = detent.Reader(store, UID)
        assert reader.get("t:rev", -1) == 1
        assert reader.revision("t") == 1
        del reader
    assert descriptors() == before and threading.active_count() == threads

    reader = detent.Reader(store, UID)
    assert reader.get("t:rev", -1) == 1
    child = os.fork()
    if child == 0:
        status = 1
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                if reader.get("t:rev", -1) == 2:
                    status = 0
                    break
                time.sleep(0.01)
        finally:
            os._exit(status)
    writer.publish(knobs(rev=2))
    _, wait = os.waitpid(child, 0)
    assert os.waitstatus_to_exitcode(wait) == 0, "the forked child never saw the next publish"
    writer.close()


# Python only: argument types, keywords, path types and no subclassing.
def test_arguments():
    store = fresh("arguments")
    writer = Writer(store)
    writer.publish(knobs(rev=7))
    for opened in (detent.Reader(store, UID), detent.Reader(os.fsencode(store), UID),
                   detent.Reader(str(store), UID, ["t"]), detent.Reader(store, UID, ("t",))):
        assert opened.get("t:rev", 0) == 7

    reader = detent.Reader(store, UID)
    for bad in (lambda: reader.eval("t:flag", 1), lambda: reader.eval("t:flag", "yes"),
                lambda: reader.get("t:rev", True), lambda: reader.get("t:rev", 1.5),
                lambda: reader.get(b"t:rev", 0), lambda: reader.eval("t:flag", False, hash=1),
                lambda: reader.lookup(None), lambda: reader.revision(3),
                lambda: reader.eval("t:flag"), lambda: reader.get("t:rev", 0, 1),
                lambda: detent.Reader(store, "0"), lambda: detent.Reader(store, UID, "t"),
                lambda: detent.Reader(store, UID, [3]), lambda: detent.Reader()):
        try:
            bad()
            raise AssertionError("expected TypeError")
        except TypeError:
            pass
    for bad in (lambda: reader.get("t:rev", 2 ** 63), lambda: reader.get("t:rev", -2 ** 63 - 1),
                lambda: detent.Reader(store, 2 ** 32), lambda: detent.Reader(store, -1)):
        try:
            bad()
            raise AssertionError("expected OverflowError")
        except OverflowError:
            pass
    assert reader.get("t:rev", -2 ** 63) == 7 and reader.get("t:missing", 2 ** 63 - 1) == 2 ** 63 - 1

    try:
        class Subclass(detent.Reader):
            pass
        raise AssertionError("Reader accepted a subclass")
    except TypeError:
        pass
    assert not hasattr(reader, "close")
    # An unknown C status keeps its integer rather than failing.
    unknown = detent.Status(99)
    assert int(unknown) == 99 and unknown.name == "UNKNOWN_99"
    writer.close()


def run():
    shutil.rmtree(ROOT, ignore_errors=True)
    try:
        for name, test in sorted((n, f) for n, f in globals().items() if n.startswith("test_") and callable(f)):
            test()
            print(f"{name} OK")
    finally:
        shutil.rmtree(ROOT, ignore_errors=True)
    print("all detent python binding tests passed")


if __name__ == "__main__":
    run()
