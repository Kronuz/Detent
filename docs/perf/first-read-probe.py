# First-read attribution (quest first-read-1ms A1): wall-clock marks inside the reader core's first read.
#   first-read-probe.py mark TREE HEADER   inserts the marks into TREE's sources (the caller restores them)
#   first-read-probe.py run BINARY PHASES STORE NAMESPACES stopped|running READS
#       fresh-process first reads as the gate runs them; each phase's median and p90 per cell, as JSON
import json, os, shutil, statistics, subprocess, sys
from pathlib import Path

HEADER = r"""#pragma once
// First-read probe marks, dumped to stderr at exit when FRP is set (docs/perf/first-read-probe.py).
#include <cstdio>
#include <cstdlib>
#include <mach/mach_time.h>
namespace frp {
struct M { const char *label; unsigned long long t; };
inline M marks[256];
inline int count = 0;
inline void mark(const char *label) {
	if (count < 256)
		marks[count++] = {label, mach_absolute_time()};
}
struct Dump {
	~Dump() {
		if (!std::getenv("FRP") || count < 2)
			return;
		mach_timebase_info_data_t b{};
		mach_timebase_info(&b);
		std::fprintf(stderr, "FRP {");
		for (int i = 1; i < count; ++i)
			std::fprintf(stderr, "%s\"%d:%s\":%llu", i > 1 ? "," : "", i, marks[i].label,
			             (marks[i].t - marks[i - 1].t) * b.numer / b.denom);
		std::fprintf(stderr, "}\n");
	}
};
inline Dump dump;
} // namespace frp
#define FRP(label) frp::mark(label)
"""


def mark(root, header):
    Path(header).write_text(HEADER)
    inc = f'#include "{Path(header).resolve()}"\n'

    def edit(path, pairs):
        p = root / path
        s = inc + p.read_text()
        for old, new in pairs:
            if old not in s:
                sys.exit(f"{path}: no anchor {old!r}")
            s = s.replace(old, new, 1)
        p.write_text(s)

    edit("src/detent_reader/core.cc", [
        ("detent_status detent_open(const detent_open_options *options, detent_reader **out) {\n",
         "detent_status detent_open(const detent_open_options *options, detent_reader **out) {\n	FRP(\"start\");\n"),
        ("	if (!install_atfork())\n", "	FRP(\"open.args\");\n	if (!install_atfork())\n"),
        ("	// Directory trust once, `change` and the table; nothing attached (D2). An empty store opens (Q6).\n",
         "	FRP(\"open.handle\");\n"),
        ("	register_handle(handle);\n	*out = handle;\n", "	register_handle(handle);\n	FRP(\"open.register\");\n	*out = handle;\n"),
        ("			handle.directory = x::open_publisher_directory(handle.store, handle.publisher);\n",
         "			FRP(\"refresh.enter\");\n			handle.directory = x::open_publisher_directory(handle.store, handle.publisher);\n			FRP(\"trust_walk\");\n"),
        ("	DETENT_WINDOW(ChangeMapped);\n", "	FRP(\"change_open\");\n"),
        ("		auto read = x::read_store_table(handle.directory, handle.publisher, handle.mappings);\n",
         "		FRP(\"sequences\");\n		auto read = x::read_store_table(handle.directory, handle.publisher, handle.mappings);\n		FRP(\"store_table\");\n"),
        ("			DETENT_WINDOW(Attaching);\n", "			FRP(\"get.to_attach\");\n"),
        ("			auto attached = x::PreparedReader::attach(", "			FRP(\"select_entry\");\n			auto attached = x::PreparedReader::attach("),
        ("			fresh.push_back(nullptr);\n", "			FRP(\"attach\");\n			fresh.push_back(nullptr);\n"),
        ("			return fresh.back();\n		};", "			FRP(\"generation\");\n			return fresh.back();\n		};"),
        ("	const auto h = optional_of(hash), sw = optional_of(switch_value);\n",
         "	FRP(\"get.args\");\n	const auto h = optional_of(hash), sw = optional_of(switch_value);\n"),
        ("			const auto definition = generation.state.definition(ns, key);\n",
         "			FRP(\"get.refreshed\");\n			const auto definition = generation.state.definition(ns, key);\n			FRP(\"definition\");\n"),
        ("			value = l::evaluate(*definition, ns, key, h, sw);\n", "			value = l::evaluate(*definition, ns, key, h, sw);\n			FRP(\"evaluate\");\n"),
    ])
    edit("src/detent_local/prepared_reader.cc", [
        ("		if (held.st_dev != named.st_dev || held.st_ino != named.st_ino)\n			continue;\n",
         "		if (held.st_dev != named.st_dev || held.st_ino != named.st_ino)\n			continue;\n		FRP(\"attach.lease\");\n"),
        ("		require(checksum(image) == descriptor.checksum);\n",
         "		FRP(\"attach.image_read\");\n		require(checksum(image) == descriptor.checksum);\n		FRP(\"attach.image_sha\");\n"),
        ("		const GenerationName metadata_name(", "		FRP(\"attach.decode_root\");\n		const GenerationName metadata_name("),
        ("		limits.bytes = metadata.physical_bytes;\n", "		FRP(\"attach.metadata\");\n		limits.bytes = metadata.physical_bytes;\n"),
    ])
    edit("src/detent_local/lazy_page_access.cc", [
        ("		const auto length = static_cast<std::size_t>(held.st_size);\n",
         "		FRP(\"seg.open_fstat\");\n		const auto length = static_cast<std::size_t>(held.st_size);\n"),
        ("		Mapping mapped;\n", "		FRP(\"seg.checks_acl\");\n		Mapping mapped;\n"),
        ("		mapped.bytes = {static_cast<const char *>(address), length};\n",
         "		FRP(\"seg.mmap\");\n		mapped.bytes = {static_cast<const char *>(address), length};\n"),
        ("		return mapped;\n	}", "		FRP(\"seg.fstatat\");\n		return mapped;\n	}"),
        ("		window(LazyPageWindow::Publish);\n", "		FRP(\"seg.bitmap\");\n		window(LazyPageWindow::Publish);\n"),
        ("			const auto &mapped = segment(reference.segment);\n",
         "			FRP(\"resolve.enter\");\n			const auto &mapped = segment(reference.segment);\n			FRP(\"resolve.segment\");\n"),
        ("				    DETENT_COUNT(pages_verified, 1);\n", "				    FRP(\"page.verify\");\n"),
        ("				    verifications.fetch_add(1, std::memory_order_relaxed);\n",
         "				    FRP(\"page.first_touch\");\n				    verifications.fetch_add(1, std::memory_order_relaxed);\n"),
        ("			return bytes;\n		} catch (const d::StoragePressure &)", "			FRP(\"resolve.done\");\n			return bytes;\n		} catch (const d::StoragePressure &)"),
    ])


def run(binary, phases, store_src, namespaces, publisher, reads):
    keys = [(j * 7919) % 10000 for j in range(40)]
    work = store_src.parent / f"probe-{store_src.name}-{publisher}"
    shutil.rmtree(work, ignore_errors=True)
    subprocess.run(["cp", "-cR", store_src, work], check=True)
    agent, rows = None, []
    try:
        if publisher == "running":
            agent = subprocess.Popen([phases, "--publish", work, str(namespaces)], stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, text=True)
            if agent.stdout.readline().strip() != "publishing":
                print(json.dumps({"namespaces": namespaces, "publisher": publisher,
                                  "skipped": "publisher did not start: " + agent.stderr.read().strip()}), flush=True)
                return
        for path in work.rglob("*"):
            if path.is_file():
                path.read_bytes()
        env = dict(os.environ, FRP="1")
        for r in range(5 + reads):
            i = keys[r % len(keys)]
            res = subprocess.run([binary, "--core", work, f"n{i % namespaces}:k{i}", str(3 * i + 1)],
                                 capture_output=True, text=True, env=env, timeout=10)
            if res.returncode:
                raise RuntimeError(res.stderr[-400:])
            if r < 5:
                continue
            row = json.loads(res.stdout.strip().splitlines()[-1])
            row["frp"] = {}
            for line in res.stderr.splitlines():
                if line.startswith("FRP "):
                    for k, ns in json.loads(line[4:]).items():
                        name = k.split(":", 1)[1]
                        row["frp"][name] = row["frp"].get(name, 0) + ns
            rows.append(row)
    finally:
        if agent:
            agent.kill()
            agent.wait()
        shutil.rmtree(work, ignore_errors=True)
    totals = sorted(r["total_ns"] for r in rows)
    names = list(dict.fromkeys(k for r in rows for k in r["frp"]))
    cell = {"namespaces": namespaces, "publisher": publisher, "reads": len(rows),
            "median_us": statistics.median(totals) / 1e3, "p99_us": totals[int(len(totals) * 0.99) - 1] / 1e3,
            "phases": {k: int(statistics.median(r[k] for r in rows)) for k in rows[0]
                       if k.endswith(("_ns", "_us", "_faults")) and k != "total_ns"},
            "marks_us": {}}
    for k in names:
        values = sorted(r["frp"].get(k, 0) for r in rows)
        cell["marks_us"][k] = [round(statistics.median(values) / 1e3, 1), round(values[int(len(values) * 0.9)] / 1e3, 1)]
    print(json.dumps(cell), flush=True)


if sys.argv[1] == "mark":
    mark(Path(sys.argv[2]), sys.argv[3])
elif sys.argv[1] == "run":
    run(Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve(), Path(sys.argv[4]).resolve(), int(sys.argv[5]),
        sys.argv[6], int(sys.argv[7]))
else:
    sys.exit("usage: first-read-probe.py mark TREE HEADER | run BINARY PHASES STORE NAMESPACES STATE READS")
