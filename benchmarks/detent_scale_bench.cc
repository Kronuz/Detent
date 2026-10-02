// Synthetic scaling baseline, with explicit bounds above the current default.
// Usage: detent_scale_bench [knobs=100000] [iterations=3]
#include "detent/json_schema.hh"
#include "detent/json_patch.hh"
#include "shm_writer.hh"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;
namespace {
std::size_t number(const char* text, std::size_t limit) {
	std::string s(text);
	std::size_t n = 0;
	auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), n);
	if (error != std::errc{} || end != s.data() + s.size() || n == 0 || n > limit) throw std::runtime_error("invalid benchmark size");
	return n;
}
template<class F> double median_ms(std::size_t repeats, F&& function) {
	std::vector<double> samples;
	for (std::size_t i = 0; i < repeats; ++i) {
		auto begin = Clock::now();
		function();
		samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - begin).count());
	}
	std::sort(samples.begin(), samples.end());
	return samples[samples.size() / 2];
}
}
int main(int argc, char** argv) {
	try {
		auto count = argc > 1 ? number(argv[1], 1'000'000) : 100'000;
		auto repeats = argc > 2 ? number(argv[2], 100) : 3;
		detent::Snapshot snapshot;
		snapshot.version = 1;
		for (std::size_t i = 0; i < count; ++i) {
			auto name = "namespace" + std::to_string(i % 100) + ":service/feature_" + std::to_string(i);
			if (i % 2) snapshot.knobs[name] = detent::IntDef{static_cast<std::int64_t>(i)};
			else {
				detent::BoolDef flag;
				flag.rollout_pct = 25;
				flag.overrides["internal_employees"] = true;
				flag.overrides["production"] = detent::GroupRollout{5};
				snapshot.knobs[name] = std::move(flag);
			}
		}
		std::string json;
		auto serialize_ms = median_ms(repeats, [&] { json = detent::serialize_snapshot(snapshot); });
		auto parse_ms = median_ms(repeats, [&] {
			auto parsed = detent::parse_source_snapshot(json, "scale", count, 256u * 1024 * 1024);
			if (!parsed || parsed.value().knobs.size() != count) throw std::runtime_error("parse failed");
		});
		detent::SnapshotPatch patch;
		patch.version = 2;
		patch.knobs[snapshot.knobs.begin()->first] = detent::BoolDef{};
		auto patch_ms = median_ms(repeats, [&] {
			auto applied = detent::apply_patch(snapshot, patch, count);
			if (!applied || applied.value().knobs.size() != count) throw std::runtime_error("patch failed");
		});
		std::vector<std::uint8_t> image;
		auto image_ms = median_ms(repeats, [&] { image = detent_agent::serialize_for_shm(snapshot); });
		auto default_parse = detent::parse_source_snapshot(json, "scale");
		std::printf("{\"knobs\":%zu,\"iterations\":%zu,\"json_bytes\":%zu,\"image_bytes\":%zu,\"single_knob_patch_bytes\":%zu,\"serialize_ms\":%.3f,\"parse_ms\":%.3f,\"single_knob_patch_ms\":%.3f,\"image_ms\":%.3f,\"fits_default_json_limits\":%s,\"fits_legacy_4mib_image\":%s}\n",
			count, repeats, json.size(), image.size(), detent::serialize_patch(patch).size(), serialize_ms, parse_ms, patch_ms, image_ms,
			default_parse ? "true" : "false", image.size() <= 4u * 1024 * 1024 ? "true" : "false");
	} catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
