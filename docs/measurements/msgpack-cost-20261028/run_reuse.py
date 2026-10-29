from __future__ import annotations

import hashlib
import json
import os
import subprocess
from datetime import UTC, datetime
from pathlib import Path

root = Path(__file__).resolve().parent
output = root / "paired-reuse.json"
if output.exists():
    raise ValueError("Refuse to replace a cost batch")
environment = {
    key: value
    for key, value in os.environ.items()
    if not key.startswith(("DETENT_", "DYLD_", "Malloc")) and key != "LD_PRELOAD"
}
environment["LC_ALL"] = "C"
rows: list[dict[str, object]] = []
report: dict[str, object] = {
    "status": "incomplete",
    "started_utc": datetime.now(UTC).isoformat(),
    "method": "Five rotating plain triplets (private codec, initial extraction, scoped-packer extraction) per snapshot × 34/200-byte key corpus × 37/4096-byte input fragments, 50 repeats/process. One additional observed pair per case, two repeats/process. Each process has its own matching warmup. Plain CPU is acceptance data; observed allocation events and per-step wall maxima include instrumentation overhead. Dedicated child accounts must fully refund. Encode verifies each output chunk against the canonical input; no frame compression or transport is timed.",
    "sources": {
        name: hashlib.sha256((root / name).read_bytes()).hexdigest()
        for name in ("production.cc", "observer.hh", "build.py", "run_reuse.py")
    },
    "binaries": {
        arm: hashlib.sha256((root / f"production-{arm}").read_bytes()).hexdigest()
        for arm in ("before", "initial", "after")
    },
    "candidate_patch": "diff --git a/src/detent_distribution/snapshot_encoder.cc b/src/detent_distribution/snapshot_encoder.cc\nindex c3368e9..625ad49 100644\n--- a/src/detent_distribution/snapshot_encoder.cc\n+++ b/src/detent_distribution/snapshot_encoder.cc\n@@ -43,19 +43,21 @@ struct SnapshotEncoder::Impl {\n \t\twire_length += bytes.size();\n \t}\n \tvoid namespace_row(m::InlineBytes<512> &out, std::uint64_t ordinal) const {\n-\t\tm::AppendPacker(out).pack_array(2);\n+\t\tm::AppendPacker pack(out);\n+\t\tpack.pack_array(2);\n \t\tconst auto row = source->name_space(ordinal);\n-\t\tm::AppendPacker(out).pack_str(row.name);\n-\t\tm::AppendPacker(out).pack_uint64(row.revision);\n+\t\tpack.pack_str(row.name);\n+\t\tpack.pack_uint64(row.revision);\n \t}\n \tstatic void override_row(m::InlineBytes<512> &out, std::string_view name, const GroupOverride &value) {\n \t\tdetail::wire::override_row(out, name, value);\n \t}\n \tstatic void record_row(m::InlineBytes<512> &out, const Record &row) {\n-\t\tm::AppendPacker(out).pack_array(4);\n-\t\tm::AppendPacker(out).pack_uint64(row.namespace_index);\n-\t\tm::AppendPacker(out).pack_str(row.key);\n-\t\tm::AppendPacker(out).pack_uint64(row.revision);\n+\t\tm::AppendPacker pack(out);\n+\t\tpack.pack_array(4);\n+\t\tpack.pack_uint64(row.namespace_index);\n+\t\tpack.pack_str(row.key);\n+\t\tpack.pack_uint64(row.revision);\n \t\tdetail::wire::definition(out, row.definition);\n \t}\n \tvoid count_one() {\n@@ -127,24 +129,25 @@ struct SnapshotEncoder::Impl {\n \t\t}\n \t}\n \tvoid header(m::InlineBytes<512> &out) const {\n-\t\tm::AppendPacker(out).pack_array(7);\n-\t\tm::AppendPacker(out).pack_uint64(1);\n-\t\tm::AppendPacker(out).pack_uint64(1);\n-\t\tm::AppendPacker(out).pack_array(2);\n-\t\tm::AppendPacker(out).pack_uint64(1);\n-\t\tm::AppendPacker(out).pack_uint64(1);\n+\t\tm::AppendPacker pack(out);\n+\t\tpack.pack_array(7);\n+\t\tpack.pack_uint64(1);\n+\t\tpack.pack_uint64(1);\n+\t\tpack.pack_array(2);\n+\t\tpack.pack_uint64(1);\n+\t\tpack.pack_uint64(1);\n \t\tconst auto &id = source->cursor();\n-\t\tm::AppendPacker(out).pack_array(5);\n+\t\tpack.pack_array(5);\n \t\tfor (auto *field : {&id.cluster, &id.group, &id.restore_epoch})\n-\t\t\tm::AppendPacker(out).pack_bin({field->data(), field->size()});\n-\t\tm::AppendPacker(out).pack_bin({id.scope.data(), id.scope.size()});\n-\t\tm::AppendPacker(out).pack_uint64(id.revision);\n-\t\tm::AppendPacker(out).pack_array(4);\n-\t\tm::AppendPacker(out).pack_uint64(source->namespace_count());\n-\t\tm::AppendPacker(out).pack_uint64(records);\n-\t\tm::AppendPacker(out).pack_uint64(overrides);\n-\t\tm::AppendPacker(out).pack_uint64(strings);\n-\t\tm::AppendPacker(out).pack_array(source->namespace_count());\n+\t\t\tpack.pack_bin({field->data(), field->size()});\n+\t\tpack.pack_bin({id.scope.data(), id.scope.size()});\n+\t\tpack.pack_uint64(id.revision);\n+\t\tpack.pack_array(4);\n+\t\tpack.pack_uint64(source->namespace_count());\n+\t\tpack.pack_uint64(records);\n+\t\tpack.pack_uint64(overrides);\n+\t\tpack.pack_uint64(strings);\n+\t\tpack.pack_array(source->namespace_count());\n \t}\n \tvoid emit_one() {\n \t\tif (phase == 2) {\n",
    "samples": rows,
}
try:
    index = 0
    for width in (34, 200):
        for kind, file in (("snapshot", "0.wire"),):
            corpus = root.parent / f"wire-before-{width}" / file
            corpus_hash = hashlib.sha256(corpus.read_bytes()).hexdigest()
            for fragment in (37, 4096):
                for mode, pairs, repeats in (("plain", 5, 50), ("observed", 1, 2)):
                    for pair in range(pairs):
                        order = ("before", "initial", "after")
                        offset = (index + pair) % len(order)
                        arms = order[offset:] + order[:offset]
                        for arm in arms:
                            command = [
                                str(root / f"production-{arm}"),
                                kind,
                                str(corpus),
                                str(repeats),
                                str(fragment),
                                mode,
                            ]
                            warm = command.copy()
                            warm[3] = "2"
                            subprocess.run(
                                warm,
                                env=environment,
                                check=True,
                                capture_output=True,
                                timeout=120,
                            )
                            result = subprocess.run(
                                command,
                                env=environment,
                                check=False,
                                capture_output=True,
                                text=True,
                                timeout=120,
                            )
                            if result.returncode:
                                report["failed_process"] = {
                                    "command": command,
                                    "returncode": result.returncode,
                                    "stdout": result.stdout,
                                    "stderr": result.stderr,
                                }
                                result.check_returncode()
                            value: dict[str, object] = json.loads(result.stdout)
                            if (
                                value["logical_bytes"] != corpus.stat().st_size
                                or value["repeats"] != repeats
                                or value["observed"] != (mode == "observed")
                            ):
                                raise RuntimeError("Cost fixture metadata differs")
                            value.update(
                                arm=arm,
                                pair=pair,
                                kind=kind,
                                key_bytes=width,
                                corpus_sha256=corpus_hash,
                            )
                            rows.append(value)
                index += 1
    report["status"] = "passed"
except BaseException as error:
    report.update(status="failed", error=repr(error))
    raise
finally:
    report["finished_utc"] = datetime.now(UTC).isoformat()
    output.write_text(json.dumps(report, indent=2) + "\n")
