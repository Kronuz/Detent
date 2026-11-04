// reader_bench: warm lookup latency through libdetent_reader and, for an interleaved baseline, a
// pinned ReadView (reader.md T1, T4). The lead runs it; see docs/mechanisms/reader-core.md.
//
//   reader_bench --store DIR [--publisher UID] --keys FILE [--hot NAME] [--calls N] [--repetitions N]
//                [--threads N] [--seed N] [--operation int|bool|hashed] [--reader core|pinned|both]
//   reader_bench --store DIR --generate N   (writes N int and N bool knobs and a keys file, then exits)
//
// FILE holds one `ns:key` per line. Each case prints one JSON line: per-call p50/p99 in ns (each
// call timed with steady_clock, so the clock's own cost is included), heap allocations inside the
// timed loop, and after the last case the process's private dirty bytes.
#include "detent/reader.h"
#include "detent_testing/generation_writer.hh"
#include "prepared_reader.hh"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <random>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#ifdef __APPLE__
#include <mach/mach.h>
#endif

namespace {
thread_local bool counting = false; // only inside a timed loop
std::atomic<std::uint64_t> allocations{0};
} // namespace
void *operator new(std::size_t bytes) {
	if (counting)
		allocations.fetch_add(1, std::memory_order_relaxed);
	if (void *p = std::malloc(bytes ? bytes : 1))
		return p;
	throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }

namespace {
namespace x = detent::local::detail;
using Clock = std::chrono::steady_clock;
struct Options {
	std::string store, keys, hot, operation = "int", reader = "both";
	uid_t publisher = ::getuid();
	std::size_t calls = 1'000'000, repetitions = 4, threads = 1, generate = 0;
	std::uint64_t seed = 0x524541444552;
};
Options parse(int argc, char **argv) {
	Options o;
	for (int i = 1; i + 1 < argc; i += 2) {
		const std::string flag = argv[i], value = argv[i + 1];
		if (flag == "--store") o.store = value;
		else if (flag == "--keys") o.keys = value;
		else if (flag == "--hot") o.hot = value;
		else if (flag == "--operation") o.operation = value;
		else if (flag == "--reader") o.reader = value;
		else if (flag == "--publisher") o.publisher = uid_t(std::stoul(value));
		else if (flag == "--calls") o.calls = std::stoull(value);
		else if (flag == "--repetitions") o.repetitions = std::stoull(value);
		else if (flag == "--threads") o.threads = std::stoull(value);
		else if (flag == "--seed") o.seed = std::stoull(value, nullptr, 0);
		else if (flag == "--generate") o.generate = std::stoull(value);
		else throw std::invalid_argument("unknown flag " + flag);
	}
	if (o.store.empty())
		throw std::invalid_argument("--store is required");
	return o;
}
// Seeded reservoir of `count` names from the keys file (as client-read-5m sampled its queries).
std::vector<std::string> sample(const Options &o, std::size_t count) {
	std::ifstream in(o.keys);
	if (!in)
		throw std::runtime_error("cannot read --keys " + o.keys);
	std::mt19937_64 random(o.seed);
	std::vector<std::string> chosen;
	std::size_t seen = 0;
	for (std::string line; std::getline(in, line); ++seen) {
		if (chosen.size() < count)
			chosen.push_back(line);
		else if (const auto j = random() % (seen + 1); j < count)
			chosen[j] = line;
	}
	if (chosen.empty())
		throw std::runtime_error("empty keys file");
	return chosen;
}
std::uint64_t private_dirty() {
#ifdef __APPLE__
	task_vm_info_data_t info{};
	mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
	return task_info(mach_task_self(), TASK_VM_INFO, task_info_t(&info), &count) == KERN_SUCCESS
	           ? info.phys_footprint
	           : 0;
#else
	std::ifstream in("/proc/self/smaps_rollup");
	for (std::string line; std::getline(in, line);)
		if (line.rfind("Private_Dirty:", 0) == 0)
			return std::stoull(line.substr(14)) * 1024;
	return 0;
#endif
}
struct Split {
	std::string ns, key;
};
Split split(const std::string &name) {
	const auto colon = name.find(':');
	return {name.substr(0, colon), name.substr(colon + 1)};
}
// One thread's timed loop; returns per-call nanoseconds.
template <class Call>
void run(const std::vector<std::string> &names, std::vector<std::uint32_t> &ns, Call call) {
	const auto calls = ns.size();
	counting = true;
	for (std::size_t i = 0; i != calls; ++i) {
		const auto &name = names[i % names.size()];
		const auto start = Clock::now();
		call(name);
		ns[i] = std::uint32_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
	}
	counting = false;
}
void report(const std::string &label, std::size_t threads, std::vector<std::vector<std::uint32_t>> &per_thread,
            std::uint64_t allocated) {
	std::cout << "{\"case\":\"" << label << "\",\"threads\":" << threads << ",\"per_thread\":[";
	for (std::size_t t = 0; t != per_thread.size(); ++t) {
		auto &v = per_thread[t];
		std::sort(v.begin(), v.end());
		std::cout << (t ? "," : "") << "{\"p50_ns\":" << v[v.size() / 2] << ",\"p99_ns\":" << v[v.size() * 99 / 100]
		          << "}";
	}
	std::cout << "],\"allocations\":" << allocated << "}\n";
}
int generate(const Options &o) {
	detent::testing::GenerationWriter writer(o.store);
	std::string json = R"({"schema_version":1,"knobs":{)";
	std::ofstream keys(o.store + ".keys");
	for (std::size_t i = 0; i != o.generate; ++i) {
		const auto n = std::to_string(i);
		json += (i ? "," : "") + std::string("\"n") + std::to_string(i % 64) + ":i" + n +
		        R"(":{"type":"int","default":)" + n + "},\"n" + std::to_string(i % 64) + ":b" + n +
		        R"(":{"type":"bool","default":false,"rollout_pct":50})";
		keys << 'n' << i % 64 << ":i" << n << '\n';
	}
	json += "}}";
	writer.publish_json(json);
	std::cout << "generated " << o.generate * 2 << " knobs; keys in " << o.store << ".keys\n";
	return 0;
}
} // namespace
int main(int argc, char **argv) {
	const auto o = parse(argc, argv);
	if (o.generate)
		return generate(o);
	const auto scattered = sample(o, o.calls);
	const std::vector<std::string> hot{o.hot.empty() ? scattered.front() : o.hot};
	const bool boolean = o.operation != "int", hashed = o.operation == "hashed";
	const std::string hash = "benchmark-user";

	detent_reader *core = nullptr;
	const detent_open_options options{sizeof options, {o.store.data(), o.store.size()}, nullptr, 0,
	                                  std::uint32_t(o.publisher)};
	if (detent_open(&options, &core) != DETENT_OK)
		throw std::runtime_error("detent_open failed");
	auto pinned = x::PreparedReader::open(o.store, o.publisher, detent::local::ViewLimits::prepared());
	auto core_call = [&](const std::string &name) {
		if (boolean) {
			bool out;
			detent_eval(core, {name.data(), name.size()}, hashed ? detent_str{hash.data(), hash.size()} : detent_str{},
			            {}, false, &out);
		} else {
			std::int64_t out;
			detent_get(core, {name.data(), name.size()}, 0, &out);
		}
	};
	auto pinned_call = [&](const std::string &name) {
		const auto [ns, key] = split(name);
		if (boolean)
			(void)pinned.view.try_eval(ns, key, hashed ? std::optional<std::string_view>(hash) : std::nullopt);
		else
			(void)pinned.view.try_get(ns, key);
	};
	for (std::size_t rep = 0; rep != o.repetitions; ++rep)
		for (const auto *set : {&hot, &scattered})
			for (const std::string reader : {"core", "pinned"}) {
				if (o.reader != "both" && o.reader != reader)
					continue;
				// Untimed warmup of the working set, then the timed loop on every thread.
				for (const auto &name : *set)
					reader == "core" ? core_call(name) : pinned_call(name);
				std::vector<std::vector<std::uint32_t>> results(o.threads, std::vector<std::uint32_t>(o.calls));
				allocations = 0;
				std::vector<std::thread> workers;
				for (std::size_t t = 0; t != o.threads; ++t)
					workers.emplace_back([&, t] {
						if (reader == "core")
							run(*set, results[t], core_call);
						else
							run(*set, results[t], pinned_call);
					});
				for (auto &worker : workers)
					worker.join();
				report(o.operation + "/" + reader + "/" + (set == &hot ? "hot" : "scattered") + "/rep" +
				           std::to_string(rep),
				       o.threads, results, allocations.load());
			}
	std::cout << "{\"private_dirty_bytes\":" << private_dirty() << "}\n";
	detent_close(core);
	return 0;
}
