#!/usr/bin/env python3
"""Run the docs as the end-to-end suite.

Every ```sh and ```rest block on the pages, in sidebar order, runs as written against a build of
this checkout, in one bash session per run (the pages continue each other's shell). The hidden
`<!-- e2e:begin ... e2e:end -->` block after a block asserts on what it printed. A block with no
assertion block must be listed in e2e_exceptions.txt (`page#N -- reason`). See docs/dev/docs-e2e.md.

    run_e2e.py [--fresh-build | --build-dir DIR] [--root DIR] [--port N] [--page SLUG ...]
               [--keep] [--list]

Exit status: 0 every block passed and every block is asserted or excepted, 1 a block failed or a
block has neither, 2 the build or the run could not start.
"""

from __future__ import annotations

import argparse
import json
import os
import queue
import random
import re
import shutil
import signal
import socket
import ssl
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_ROOT = HERE.parents[1]
DEFAULT_PORT = "8317"
RUNNABLE = ("sh", "rest")
BLOCK_TIMEOUT = 120.0
BINARIES = ("detentd", "detent-agent", "detent")

FENCE = re.compile(
    r"^```(\w*)[^\n]*\n(.*?)^```[ \t]*(?:\n|\Z)(?:[ \t]*\n)*(<!-- e2e:begin\n(.*?)e2e:end -->)?",
    re.DOTALL | re.MULTILINE,
)


@dataclass
class Spec:
    items: list[tuple[str, str]]

    def all(self, key: str) -> list[str]:
        return [v for k, v in self.items if k == key]

    def first(self, key: str) -> str | None:
        found = self.all(key)
        return found[0] if found else None


@dataclass
class Block:
    page: str
    index: int
    lang: str
    text: str
    spec: Spec | None
    line: int

    @property
    def id(self) -> str:
        return f"{self.page}#{self.index}"


@dataclass
class Result:
    block: Block
    failures: list[str] = field(default_factory=list)
    stdout: str = ""
    stderr: str = ""


VALUE_KEYS = (
    "stdout",
    "stdout-contains",
    "stdout-matches",
    "stderr-contains",
    "run",
    "status",
    "body-contains",
)


def parse_spec(body: str, where: str) -> Spec:
    items: list[tuple[str, str]] = []
    for raw in body.splitlines():
        if not raw.strip():
            continue
        if raw[0] in " \t" and items:
            key, value = items[-1]
            items[-1] = (key, value + "\n" + raw.strip())
            continue
        key, sep, value = raw.partition(":")
        if not sep:
            raise ValueError(f"{where}: expected 'key: value', got {raw!r}")
        items.append((key.strip(), value.strip()))
    return Spec(items)


def sidebar_order(root: Path) -> list[str]:
    script = (
        "import {sidebar} from process.argv[1]; console.log(JSON.stringify(sidebar));"
    )
    module = (root / "website" / "src" / "lib" / "sidebar.mjs").as_uri()
    out = subprocess.run(
        [
            "node",
            "--input-type=module",
            "-e",
            script.replace("process.argv[1]", json.dumps(module)),
        ],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    order: list[str] = []

    def walk(items: list[dict[str, object]]) -> None:
        for item in items:
            slug = item.get("slug")
            link = item.get("link")
            if isinstance(slug, str):
                order.append(slug)
            elif isinstance(link, str):
                order.append(link.strip("/"))
            children = item.get("items")
            if isinstance(children, list):
                walk(children)

    walk(json.loads(out))
    seen: set[str] = set()
    return [s for s in order if not (s in seen or seen.add(s))]


def page_path(docs: Path, slug: str) -> Path | None:
    for suffix in (".md", ".mdx"):
        if (docs / (slug + suffix)).exists():
            return docs / (slug + suffix)
    return None


def extract(root: Path) -> list[Block]:
    docs = root / "website" / "src" / "content" / "docs"
    blocks: list[Block] = []
    for slug in sidebar_order(root):
        path = page_path(docs, slug)
        if path is None:
            continue
        text = path.read_text()
        index = 0
        for m in FENCE.finditer(text):
            lang = m.group(1)
            if lang not in RUNNABLE:
                continue
            index += 1
            line = text.count("\n", 0, m.start()) + 1
            spec = parse_spec(m.group(4), f"{slug}#{index}") if m.group(3) else None
            blocks.append(Block(slug, index, lang, m.group(2), spec, line))
    return blocks


def load_exceptions(path: Path) -> dict[str, str]:
    listed: dict[str, str] = {}
    if not path.exists():
        return listed
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        head, _, reason = line.partition(" -- ")
        if not re.fullmatch(r"[\w./-]+#\d+", head.strip()) or not reason.strip():
            raise ValueError(f"{path.name}:{number}: expected 'page#N -- reason'")
        listed[head.strip()] = reason.strip()
    return listed


def validate(blocks: list[Block], exceptions: dict[str, str]) -> list[str]:
    errors: list[str] = []
    by_id = {b.id: b for b in blocks}
    for b in blocks:
        excepted = b.id in exceptions
        if b.spec is None and not excepted:
            errors.append(
                f"{b.page}.md:{b.line}: {b.id} has no e2e assertion block and is not in e2e_exceptions.txt"
            )
        if b.spec is not None and excepted:
            errors.append(
                f"{b.id}: has an assertion block and is still listed in e2e_exceptions.txt"
            )
        if b.spec is not None:
            keys = {k for k, _ in b.spec.items}
            if not keys & set(VALUE_KEYS) and not any(
                k.startswith("json.") for k in keys
            ):
                errors.append(
                    f"{b.id}: assertion block asserts no value, only the exit status"
                )
    for name in exceptions:
        if name not in by_id:
            errors.append(f"{name}: listed in e2e_exceptions.txt but no such block")
    return errors


# ------------------------------------------------------------------ run environment


def pick_port(scratch: Path) -> tuple[int, Path]:
    scratch.mkdir(parents=True, exist_ok=True)
    for _ in range(200):
        port = random.randint(20000, 60000)
        lock = scratch / f"port-{port}.lock"
        try:
            fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        except FileExistsError:
            continue
        os.close(fd)
        try:
            with socket.socket() as s:
                s.bind(("127.0.0.1", port))
        except OSError:
            lock.unlink()
            continue
        return port, lock
    raise RuntimeError("no free port")


def build(root: Path, build_dir: Path, deps_from: Path | None) -> None:
    configure = [
        "cmake", "-S", str(root), "-B", str(build_dir),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DDETENT_BUILD_TESTS=OFF", "-DDETENT_BUILD_EXAMPLES=OFF", "-DDETENT_BUILD_BENCHMARKS=OFF",
    ]  # fmt: skip
    if deps_from is not None:
        for src in sorted((deps_from / "_deps").glob("*-src")):
            configure.append(
                f"-DFETCHCONTENT_SOURCE_DIR_{src.name[: -len('-src')].upper()}={src}"
            )
    subprocess.run(configure, check=True)
    targets = ["detentd", "detent-agent", "detent-reader"]
    subprocess.run(
        ["cmake", "--build", str(build_dir), "-j", "4", "--target", *targets],
        check=True,
    )


class Session:
    """One bash process that runs each block with `source`, so functions and variables persist."""

    def __init__(self, cwd: Path, env: dict[str, str]) -> None:
        self.cwd = cwd
        (cwd / ".e2e").mkdir(exist_ok=True)
        self.proc = subprocess.Popen(
            ["bash", "--noprofile", "--norc"],
            cwd=cwd,
            env=env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            start_new_session=True,
        )
        self.lines: queue.Queue[str] = queue.Queue()
        threading.Thread(target=self._pump, daemon=True).start()
        self.count = 0

    def _pump(self) -> None:
        assert self.proc.stdout is not None
        for line in self.proc.stdout:
            self.lines.put(line)
        self.lines.put("")

    def run(self, text: str, timeout: float) -> tuple[int | None, str, str]:
        self.count += 1
        stem = self.cwd / ".e2e" / f"b{self.count}"
        stem.with_suffix(".sh").write_text(text if text.endswith("\n") else text + "\n")
        assert self.proc.stdin is not None
        out, err = stem.with_suffix(".out"), stem.with_suffix(".err")
        try:
            self.proc.stdin.write(
                f'source "{stem}.sh" >"{out}" 2>"{err}" </dev/null; echo "__E2E_RC__ $?"\n'
            )
            self.proc.stdin.flush()
        except BrokenPipeError:
            return None, "", ""
        deadline = time.monotonic() + timeout
        rc: int | None = None
        while True:
            try:
                line = self.lines.get(timeout=max(0.0, deadline - time.monotonic()))
            except queue.Empty:
                self.kill()
                break
            if line.startswith("__E2E_RC__"):
                rc = int(line.split()[1])
                break
            if line == "":
                break
        return (
            rc,
            out.read_text(errors="replace") if out.exists() else "",
            err.read_text(errors="replace") if err.exists() else "",
        )

    def kill(self) -> None:
        try:
            os.killpg(self.proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass

    def close(self) -> None:
        try:
            if self.proc.stdin:
                self.proc.stdin.close()
        except BrokenPipeError:
            pass
        self.kill()


# ------------------------------------------------------------------ assertions


def substitute(text: str, port: str) -> str:
    return text.replace(DEFAULT_PORT, port)


def check_shell(
    b: Block,
    spec: Spec,
    session: Session,
    result: Result,
    rc: int | None,
    out: str,
    err: str,
    port: str,
) -> None:
    want = int(spec.first("exit") or 0)
    if rc is None:
        result.failures.append("timed out or the session died")
        return
    if rc != want:
        result.failures.append(f"exit status {rc}, want {want}")
    check_text(spec, result, out, err, port)
    for snippet in spec.all("run"):
        hook_rc, _, hook_err = session.run(substitute(snippet, port), 60)
        if hook_rc != 0:
            result.failures.append(
                f"run: {snippet!r} exited {hook_rc}: {hook_err.strip()[:200]}"
            )


def check_text(spec: Spec, result: Result, out: str, err: str, port: str) -> None:
    for want in spec.all("stdout"):
        if out.strip() != substitute(want, port).strip():
            result.failures.append(
                f"stdout is {out.strip()!r}, want {substitute(want, port).strip()!r}"
            )
    for want in spec.all("stdout-contains"):
        if substitute(want, port) not in out:
            result.failures.append(f"stdout lacks {substitute(want, port)!r}")
    for want in spec.all("stdout-matches"):
        if not re.search(substitute(want, port), out, re.MULTILINE):
            result.failures.append(f"stdout does not match /{want}/")
    for want in spec.all("stderr-contains"):
        if substitute(want, port) not in err:
            result.failures.append(f"stderr lacks {substitute(want, port)!r}")


def run_rest(b: Block, spec: Spec, sandbox: Path, port: str, result: Result) -> None:
    head, _, rest = substitute(b.text, port).partition("\n")
    method, path = head.split()[:2]
    headers_text, _, body = rest.partition("\n\n")
    headers = dict(h.split(":", 1) for h in headers_text.splitlines() if ":" in h)
    ca = spec.first("ca")
    context = ssl.create_default_context(cafile=str(sandbox / ca)) if ca else None
    url = f"{'https' if ca else 'http'}://localhost:{port}{path}"
    request = urllib.request.Request(
        url, data=body.encode() if body.strip() else None, method=method
    )
    for name, value in headers.items():
        request.add_header(name.strip(), value.strip())
    try:
        with urllib.request.urlopen(request, timeout=30, context=context) as response:
            status, text = response.status, response.read().decode(errors="replace")
    except urllib.error.HTTPError as e:
        status, text = e.code, e.read().decode(errors="replace")
    except OSError as e:
        result.failures.append(f"request failed: {e}")
        return
    result.stdout = text
    if status != int(spec.first("status") or 200):
        result.failures.append(f"status {status}, want {spec.first('status') or 200}")
    for want in spec.all("body-contains"):
        if substitute(want, port) not in text:
            result.failures.append(f"body lacks {substitute(want, port)!r}")
    for key, want in spec.items:
        if not key.startswith("json."):
            continue
        try:
            value: object = json.loads(text)
            for part in key[len("json.") :].split("."):
                value = value[part] if isinstance(value, dict) else None
            if value != json.loads(substitute(want, port)):
                result.failures.append(f"{key} is {value!r}, want {want}")
        except (ValueError, TypeError, KeyError):
            result.failures.append(f"{key}: body is not the JSON the assertion needs")


# ------------------------------------------------------------------ driver


def stop_own(sandbox: Path, port: str) -> None:
    """Stop what this run started: pid files under its sandbox and the listener on its port."""
    for pidfile in sandbox.rglob("*.pid"):
        try:
            os.kill(int(pidfile.read_text().split()[0]), signal.SIGTERM)
        except (ValueError, OSError, IndexError):
            pass
    listeners = subprocess.run(
        ["lsof", "-ti", f"tcp:{port}", "-sTCP:LISTEN"],
        capture_output=True,
        text=True,
        check=False,
    ).stdout.split()
    for pid in listeners:
        try:
            os.kill(int(pid), signal.SIGTERM)
        except OSError:
            pass
    time.sleep(0.5)
    for pid in subprocess.run(
        ["lsof", "-ti", f"tcp:{port}", "-sTCP:LISTEN"],
        capture_output=True,
        text=True,
        check=False,
    ).stdout.split():
        try:
            os.kill(int(pid), signal.SIGKILL)
        except OSError:
            pass


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument(
        "--root",
        type=Path,
        default=DEFAULT_ROOT,
        help="tree holding the pages, tests and source",
    )
    ap.add_argument(
        "--fresh-build", action="store_true", help="configure and build the tree first"
    )
    ap.add_argument(
        "--build-dir", type=Path, help="use the binaries of this existing build"
    )
    ap.add_argument(
        "--deps-from",
        type=Path,
        help="reuse the dependency sources of this build on --fresh-build",
    )
    ap.add_argument("--port", type=int, help="port for this run (default: a free one)")
    ap.add_argument(
        "--page",
        action="append",
        default=[],
        help="run only this page's blocks (needs the earlier pages' state)",
    )
    ap.add_argument(
        "--keep",
        action="store_true",
        help="keep the sandbox (and the build on --fresh-build)",
    )
    ap.add_argument("--list", action="store_true", help="list blocks and exit")
    args = ap.parse_args()
    root: Path = args.root.resolve()
    exceptions = load_exceptions(HERE / "e2e_exceptions.txt")
    blocks = extract(root)
    errors = validate(blocks, exceptions)
    if args.list:
        for b in blocks:
            state = (
                "excepted"
                if b.id in exceptions
                else "asserted"
                if b.spec
                else "MISSING"
            )
            print(f"{b.id}\t{b.lang}\t{state}")
        return 0
    for e in errors:
        print("ERROR", e)
    scratch = root / ".scratch" / "docs-e2e"
    port_n, lock = (
        pick_port(scratch)
        if args.port is None
        else (args.port, scratch / f"port-{args.port}.lock")
    )
    port = str(port_n)
    sandbox = scratch / f"run-{port}"
    build_dir: Path | None = args.build_dir.resolve() if args.build_dir else None
    fresh = scratch / f"build-{port}"
    try:
        if args.fresh_build:
            build_dir = fresh
            build(root, fresh, args.deps_from.resolve() if args.deps_from else None)
        if build_dir is None:
            print("need --fresh-build or --build-dir")
            return 2
        bindir = build_dir / "src"
        missing = [n for n in BINARIES if not (bindir / n).exists()]
        if missing:
            print(f"missing binaries in {bindir}: {', '.join(missing)}")
            return 2
        return execute(
            root,
            blocks,
            exceptions,
            errors,
            bindir,
            sandbox,
            port,
            args.page,
            args.keep,
        )
    except subprocess.CalledProcessError as e:
        print(f"build failed: {e}")
        return 2
    finally:
        stop_own(sandbox, port)
        if not args.keep:
            shutil.rmtree(sandbox, ignore_errors=True)
            shutil.rmtree(fresh, ignore_errors=True)
        lock.unlink(missing_ok=True)


def execute(
    root: Path, blocks: list[Block], exceptions: dict[str, str], errors: list[str],
    bindir: Path, sandbox: Path, port: str, pages: list[str], keep: bool,
) -> int:  # fmt: skip
    shutil.rmtree(sandbox, ignore_errors=True)
    (sandbox / "build").mkdir(parents=True)
    (sandbox / "home").mkdir()
    (sandbox / "tmp").mkdir()
    (sandbox / "build" / "src").symlink_to(bindir)
    (sandbox / "tests").symlink_to(root / "tests")
    env = {
        "PATH": f"{bindir}:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin",
        "HOME": str(sandbox / "home"),
        "TMPDIR": str(sandbox / "tmp"),
        "XDG_CONFIG_HOME": str(sandbox / "home" / "config"),
        "XDG_STATE_HOME": str(sandbox / "home" / "state"),
        "XDG_RUNTIME_DIR": str(sandbox / "home" / "run"),
        "DETENTD_PORT": port,
        "DETENT_E2E_PORT": port,
        "NO_COLOR": "1",
        "LANG": "C",
    }
    session = Session(sandbox, env)
    results: list[Result] = []
    print(f"run port {port}, sandbox {sandbox}")
    try:
        for b in blocks:
            if b.spec is None or (pages and b.page not in pages):
                continue
            result = Result(b)
            spec = b.spec
            timeout = float(spec.first("timeout") or BLOCK_TIMEOUT)
            if b.lang == "sh":
                text = substitute(b.text, port)
                for rule in spec.all("rewrite"):
                    old, _, new = rule.partition(" => ")
                    text = text.replace(substitute(old, port), substitute(new, port))
                rc, out, err = session.run(text, timeout)
                result.stdout, result.stderr = out, err
                check_shell(b, spec, session, result, rc, out, err, port)
            else:
                run_rest(b, spec, sandbox, port, result)
            results.append(result)
            if result.failures:
                print(f"FAIL {b.id}")
                for f in result.failures:
                    print(f"    {f}")
                print(
                    "    --- stdout ---\n"
                    + indent(result.stdout)
                    + "\n    --- stderr ---\n"
                    + indent(result.stderr)
                )
            else:
                print(f"PASS {b.id}")
    finally:
        session.close()
    failed = [r for r in results if r.failures]
    if failed:
        for log in sorted(sandbox.rglob("*.log")):
            print(
                f"--- {log.relative_to(sandbox)} ---\n"
                + indent(log.read_text(errors="replace")[-2000:])
            )
    print(
        f"e2e: {len(results)} blocks run, {len(failed)} failed, {len(exceptions)} excepted, "
        f"{len(errors)} page errors"
    )
    return 1 if failed or errors else 0


def indent(text: str) -> str:
    return "\n".join("    " + line for line in text.strip().splitlines()[-30:])


if __name__ == "__main__":
    sys.exit(main())
