// Model-table lifecycle: parse, serialize, copy, patch, diff, transfer, prune,
// destroy, lookups, and the worst single override-erase cleanup turn.
// Usage: detent_model_lifecycle [--knobs N] [--long] [--random] [--cleanup-reps R]
// Prints one JSON object; output digests must match across container arms.
#include <detent/json_patch.hh>
#include <detent/json_schema.hh>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <ctime>
#include <new>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {
std::size_t allocations = 0, requested = 0, live = 0, peak = 0;
constexpr std::size_t kHeader = 16;

double cpu_ms() {
	timespec t{};
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
std::uint64_t fnv(std::string_view s) {
	std::uint64_t h = 1469598103934665603ull;
	for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
	return h;
}
} // namespace

void* operator new(std::size_t n) {
	auto* p = static_cast<char*>(std::malloc(n + kHeader));
	if (!p) throw std::bad_alloc();
	std::memcpy(p, &n, sizeof n);
	++allocations;
	requested += n;
	live += n;
	peak = std::max(peak, live);
	return p + kHeader;
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* q) noexcept {
	if (!q) return;
	auto* p = static_cast<char*>(q) - kHeader;
	std::size_t n;
	std::memcpy(&n, p, sizeof n);
	live -= n;
	std::free(p);
}
void operator delete[](void* q) noexcept { ::operator delete(q); }
void operator delete(void* q, std::size_t) noexcept { ::operator delete(q); }
void operator delete[](void* q, std::size_t) noexcept { ::operator delete(q); }

namespace {
bool long_names = false;

std::string knob(unsigned id, std::string_view prefix = "bench:") {
	auto name = std::string(long_names ? 200 : 16, 'k');
	name.replace(0, prefix.size(), prefix);
	auto digits = std::to_string(1000000 + id);
	name.replace(name.size() - digits.size(), digits.size(), digits);
	return name;
}
std::string group(unsigned g) {
	auto name = std::string(long_names ? 128 : 8, 'g');
	auto digits = std::to_string(1000 + g);
	name.replace(name.size() - digits.size(), digits.size(), digits);
	return name;
}
void definition(std::string& out, unsigned id, unsigned salt) {
	if (id % 4 == 3) {
		out += R"({"type":"int","default":)" + std::to_string(id + salt) + "}";
		return;
	}
	out += R"({"type":"bool","default":)";
	out += (id + salt) % 2 ? "true" : "false";
	out += R"(,"overrides":{")" + group(0) + R"(":true,")" + group(1) + R"(":{"rollout_pct":)" +
		   std::to_string(id % 100) + R"(},")" + group(2) + R"(":false}})";
}

struct Stage {
	const char* name;
	double cpu = 0;
	std::size_t allocs = 0, bytes = 0, peak_delta = 0;
	long long retained_delta = 0;
};
std::vector<Stage> stages;

template <class F>
auto measure(const char* name, F&& f) {
	Stage s{name};
	const auto a0 = allocations, r0 = requested, l0 = live;
	peak = live;
	const double t0 = cpu_ms();
	if constexpr (std::is_void_v<decltype(f())>) {
		f();
		s.cpu = cpu_ms() - t0;
		s.allocs = allocations - a0, s.bytes = requested - r0, s.peak_delta = peak - l0;
		s.retained_delta = static_cast<long long>(live) - static_cast<long long>(l0);
		stages.push_back(s);
	} else {
		auto result = f();
		s.cpu = cpu_ms() - t0;
		s.allocs = allocations - a0, s.bytes = requested - r0, s.peak_delta = peak - l0;
		s.retained_delta = static_cast<long long>(live) - static_cast<long long>(l0);
		stages.push_back(s);
		return result;
	}
}
[[noreturn]] void die(const char* what) {
	std::fprintf(stderr, "lifecycle bench: %s\n", what);
	std::exit(1);
}
} // namespace

int main(int argc, char** argv) {
	unsigned count = 100000, cleanup_reps = 200;
	bool random = false;
	for (int i = 1; i < argc; ++i) {
		std::string_view a = argv[i];
		if (a == "--long") long_names = true;
		else if (a == "--random") random = true;
		else if (a == "--knobs" && i + 1 < argc) count = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10));
		else if (a == "--cleanup-reps" && i + 1 < argc) cleanup_reps = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10));
		else die("usage: [--knobs N] [--long] [--random] [--cleanup-reps R]");
	}
	if (count < 100 || count % 100) die("--knobs must be a positive multiple of 100");
	constexpr std::size_t kMaxBytes = std::size_t(1) << 30;

	std::vector<unsigned> order(count);
	std::iota(order.begin(), order.end(), 0u);
	if (random) std::shuffle(order.begin(), order.end(), std::mt19937(20261023));

	std::string full = R"({"schema_version":1,"version":1,"knobs":{)";
	for (unsigned i = 0; i < count; ++i) {
		if (i) full += ',';
		full += '"' + knob(order[i]) + "\":";
		definition(full, order[i], 0);
	}
	full += "}}";
	// 1% changed, 0.5% added, 0.5% deleted.
	std::string patch_json = R"({"schema_version":1,"version":2,"knobs":{)";
	bool first = true;
	for (unsigned i = 0; i < count; i += 100) {
		for (unsigned j = 0; j < 2; ++j) {
			if (!first) patch_json += ',';
			first = false;
			patch_json += '"' + knob(order[i + j]) + "\":";
			if (j == 0) definition(patch_json, order[i + j], 1);
			else patch_json += "null";
		}
		patch_json += ",\"" + knob(count + i, "added:") + "\":";
		definition(patch_json, count + i, 0);
	}
	patch_json += "}}";
	stages.reserve(32);
	const std::size_t base = live;

	std::uint64_t digest_snapshot, digest_patch, digest_diff;
	std::size_t live_after_parse = 0, coexisting_live = 0;
	{
		auto parsed = measure("parse_validate_construct", [&] { return detent::parse_source_snapshot(full, "bench", count, kMaxBytes); });
		if (!parsed) die("snapshot parse failed");
		detent::Snapshot live_snapshot = std::move(parsed).value();
		live_after_parse = live - base;
		auto bytes = measure("serialize_snapshot", [&] { return detent::serialize_snapshot(live_snapshot); });
		digest_snapshot = fnv(bytes);
		std::string().swap(bytes);
		auto copy = measure("copy_snapshot", [&] { return detent::Snapshot(live_snapshot); });
		auto patch = measure("parse_patch", [&] { return detent::parse_snapshot_patch(patch_json, "bench", count * 2, kMaxBytes); });
		if (!patch) die("patch parse failed");
		auto applied = measure("apply_patch", [&] { return detent::apply_patch(live_snapshot, patch.value(), count * 2); });
		if (!applied) die("patch apply failed");
		auto diff = measure("diff_snapshots", [&] { return detent::diff_snapshots(live_snapshot, applied.value()); });
		auto patch_bytes = measure("serialize_patch", [&] { return detent::serialize_patch(diff); });
		digest_patch = fnv(detent::serialize_patch(patch.value()));
		digest_diff = fnv(patch_bytes);
		auto composed = measure("compose_patches", [&] { return detent::compose_patches(patch.value(), diff); });
		coexisting_live = live - base;
		auto published = measure("transfer_snapshot", [&] { return detent::Snapshot(std::move(applied).value()); });
		measure("prune_half_positional", [&] {
			for (auto it = copy.knobs.begin(); it != copy.knobs.end();) {
				it = copy.knobs.erase(it);
				if (it != copy.knobs.end()) ++it;
			}
		});
		std::size_t hits = 0;
		std::vector<std::string> probes;
		probes.reserve(4096);
		std::mt19937 rng(7);
		for (unsigned i = 0; i < 2048; ++i) probes.push_back(knob(rng() % count));
		for (unsigned i = 0; i < 2048; ++i) probes.push_back(knob(rng() % count, "missed:"));
		measure("find_broad_100x4096", [&] {
			for (unsigned r = 0; r < 100; ++r)
				for (auto& p : probes) hits += live_snapshot.knobs.find(std::string_view(p)) != live_snapshot.knobs.end();
		});
		if (hits != 100 * 2048) die("lookup checksum mismatch");
		measure("destroy_pruned_copy", [&] { auto doomed = std::move(copy); });
		measure("destroy_published", [&] { auto doomed = std::move(published); });
		measure("destroy_live", [&] { auto doomed = std::move(live_snapshot); });
		(void)composed;
	}

	const std::size_t model_residue = live - base;

	// Worst cleanup turn: one erase of an override on a definition at the
	// 256-override limit. The authority drains erase the LAST override
	// (O(1) on every container); the begin variant is recorded as well, since
	// it shifts the whole tail on a contiguous container.
	auto drain = [&](bool from_begin, std::size_t& allocs_out) {
		std::vector<std::uint64_t> out;
		out.reserve(std::size_t(cleanup_reps) * 256);
		for (unsigned rep = 0; rep < cleanup_reps; ++rep) {
			detent::BoolDef def;
			std::vector<unsigned> groups(256);
			std::iota(groups.begin(), groups.end(), 0u);
			std::shuffle(groups.begin(), groups.end(), std::mt19937(rep));
			for (auto g : groups) def.overrides.try_emplace(group(g), detent::GroupOverride(bool(g & 1)));
			const auto a0 = allocations;
			while (!def.overrides.empty()) {
				const auto t0 = std::chrono::steady_clock::now();
				def.overrides.erase(from_begin ? def.overrides.begin() : std::prev(def.overrides.end()));
				out.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
			}
			allocs_out += allocations - a0;
		}
		std::sort(out.begin(), out.end());
		return out;
	};
	std::size_t cleanup_allocs = 0, cleanup_begin_allocs = 0;
	auto turns = drain(false, cleanup_allocs);
	auto begin_turns = drain(true, cleanup_begin_allocs);
	auto pct = [&](double p) { return turns[std::min(turns.size() - 1, std::size_t(p * turns.size()))]; };
	auto begin_pct = [&](double p) { return begin_turns[std::min(begin_turns.size() - 1, std::size_t(p * begin_turns.size()))]; };

	std::printf("{\"knobs\":%u,\"long_names\":%s,\"random\":%s,\"snapshot_object_bytes\":%zu,\"definition_object_bytes\":%zu,",
				count, long_names ? "true" : "false", random ? "true" : "false", sizeof(detent::Snapshot), sizeof(detent::BoolDef));
	std::printf("\"live_snapshot_requested_bytes\":%zu,\"coexisting_live_patch_history_requested_bytes\":%zu,", live_after_parse, coexisting_live);
	std::printf("\"digests\":{\"snapshot\":\"%016llx\",\"patch\":\"%016llx\",\"diff\":\"%016llx\"},",
				(unsigned long long)digest_snapshot, (unsigned long long)digest_patch, (unsigned long long)digest_diff);
	std::printf("\"stages\":{");
	for (std::size_t i = 0; i < stages.size(); ++i) {
		auto& s = stages[i];
		std::printf("%s\"%s\":{\"cpu_ms\":%.3f,\"allocations\":%zu,\"requested_bytes\":%zu,\"peak_delta_bytes\":%zu,\"retained_delta_bytes\":%lld}",
					i ? "," : "", s.name, s.cpu, s.allocs, s.bytes, s.peak_delta, s.retained_delta);
	}
	std::printf("},\"cleanup_turn_ns\":{\"samples\":%zu,\"allocations\":%zu,\"p50\":%llu,\"p99\":%llu,\"p999\":%llu,\"max\":%llu},\"cleanup_begin_turn_ns\":{\"allocations\":%zu,\"p50\":%llu,\"p99\":%llu,\"max\":%llu},\"model_residue_bytes\":%zu}\n",
				turns.size(), cleanup_allocs, (unsigned long long)pct(0.5), (unsigned long long)pct(0.99), (unsigned long long)pct(0.999),
				(unsigned long long)turns.back(), cleanup_begin_allocs, (unsigned long long)begin_pct(0.5),
				(unsigned long long)begin_pct(0.99), (unsigned long long)begin_turns.back(), model_residue);
	return 0;
}
