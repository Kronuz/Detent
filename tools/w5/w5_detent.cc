// Detent's own surfaces over the old and the new pins: snapshot JSON (parse, merge, serialize, diff, patch) and
// rollout buckets (sha256 and endian), so snapshot JSON bytes and bucket values can be compared between builds.
#include "common.hh"

#include "detent/hashing.hh"
#include "detent/json_patch.hh"
#include "detent/json_schema.hh"

#include <random>
#include <string>
#include <vector>

namespace {

std::string knobs_json(unsigned seed, std::size_t count, bool source, std::uint64_t version) {
	std::mt19937_64 rng(seed);
	std::string out = "{\"schema_version\":1,";
	if (source) out += "\"version\":" + std::to_string(version) + ",\"published_at\":\"2026-11-04T00:00:00Z\",";
	out += "\"knobs\":{";
	for (std::size_t i = 0; i < count; ++i) {
		if (i) out += ',';
		out += "\"ns" + std::to_string(i % 7) + ":knob_" + std::to_string(i) + "\":";
		switch (rng() % 3) {
		case 0:
			out += "{\"type\":\"bool\",\"default\":" + std::string(rng() & 1 ? "true" : "false") + "}";
			break;
		case 1:
			out += "{\"type\":\"bool\",\"default\":false,\"rollout_pct\":" + std::to_string(rng() % 101) + ",\"overrides\":{\"internal\":true,\"beta\":{\"rollout_pct\":" +
				   std::to_string(rng() % 101) + "}}}";
			break;
		default:
			out += "{\"type\":\"int\",\"default\":" + std::to_string(static_cast<std::int64_t>(rng() % 2000000) - 1000000 + static_cast<std::int64_t>(rng() >> 20)) + "}";
		}
	}
	return out + "}}";
}

std::string result_text(const detent::UpdateError& e) {
	return std::to_string(static_cast<int>(e.reason)) + ": " + e.detail;
}

} // namespace

int main() {
	using namespace detent;
	// 1. Snapshot JSON: parse, serialize and compare the bytes.
	for (std::size_t count : {0u, 1u, 7u, 100u, 5000u}) {
		for (unsigned seed : {1u, 2u}) {
			auto text = knobs_json(seed, count, true, 10 + seed);
			auto parsed = parse_source_snapshot(text, "src");
			std::string id = "snapshot n=" + std::to_string(count) + " seed=" + std::to_string(seed);
			if (!parsed) {
				w5::line(id, "rejected: " + result_text(parsed.error()));
				continue;
			}
			auto json = serialize_snapshot(parsed.value());
			w5::line(id, "serialized " + std::to_string(json.size()) + " " + w5::hex(w5::fnv(json)) + " input " + w5::hex(w5::fnv(text)));
			// A second snapshot, the diff between them, its patch bytes, and the patch applied back.
			auto next = parse_source_snapshot(knobs_json(seed + 10, count, true, 20 + seed), "src");
			if (next) {
				auto patch = diff_snapshots(parsed.value(), next.value());
				auto patch_json = serialize_patch(patch);
				auto applied = apply_patch(parsed.value(), patch);
				w5::line(id + " diff", "patch " + std::to_string(patch_json.size()) + " " + w5::hex(w5::fnv(patch_json)) + " applied " +
											  (applied ? w5::hex(w5::fnv(serialize_snapshot(applied.value()))) : "rejected"));
			}
		}
	}
	// 2. Malformed snapshots: the error text.
	std::vector<std::pair<std::string, std::string>> bad = {
		{"empty", ""}, {"not-json", "{"}, {"array", "[]"}, {"no-version", "{\"schema_version\":1,\"knobs\":{}}"},
		{"version-zero", "{\"schema_version\":1,\"version\":0,\"knobs\":{}}"}, {"version-negative", "{\"schema_version\":1,\"version\":-1,\"knobs\":{}}"},
		{"version-big", "{\"schema_version\":1,\"version\":18446744073709551615,\"knobs\":{}}"}, {"version-huge", "{\"schema_version\":1,\"version\":18446744073709551616,\"knobs\":{}}"},
		{"version-float", "{\"schema_version\":1,\"version\":1.5,\"knobs\":{}}"}, {"schema-2", "{\"schema_version\":2,\"version\":1,\"knobs\":{}}"},
		{"bad-type", "{\"schema_version\":1,\"version\":1,\"knobs\":{\"a:b\":{\"type\":\"str\",\"default\":1}}}"},
		{"int-overflow", "{\"schema_version\":1,\"version\":1,\"knobs\":{\"a:b\":{\"type\":\"int\",\"default\":9223372036854775808}}}"},
		{"int-min", "{\"schema_version\":1,\"version\":1,\"knobs\":{\"a:b\":{\"type\":\"int\",\"default\":-9223372036854775808}}}"},
		{"int-float", "{\"schema_version\":1,\"version\":1,\"knobs\":{\"a:b\":{\"type\":\"int\",\"default\":1.0}}}"},
		{"rollout-101", "{\"schema_version\":1,\"version\":1,\"knobs\":{\"a:b\":{\"type\":\"bool\",\"default\":false,\"rollout_pct\":101}}}"},
		{"rollout-float", "{\"schema_version\":1,\"version\":1,\"knobs\":{\"a:b\":{\"type\":\"bool\",\"default\":false,\"rollout_pct\":1.5}}}"},
		{"bad-name", "{\"schema_version\":1,\"version\":1,\"knobs\":{\"a b\":{\"type\":\"bool\",\"default\":false}}}"},
		{"dup-key", "{\"schema_version\":1,\"version\":1,\"knobs\":{\"a:b\":{\"type\":\"bool\",\"default\":false},\"a:b\":{\"type\":\"bool\",\"default\":true}}}"},
		{"trailing", "{\"schema_version\":1,\"version\":1,\"knobs\":{}} x"}, {"nan", "{\"schema_version\":1,\"version\":NaN,\"knobs\":{}}"},
		{"deep", std::string(300, '[') + std::string(300, ']')}, {"nul", std::string("{\"schema_version\":1,\"version\":1,\"knobs\":{\"a\0b\":{\"type\":\"bool\",\"default\":false}}}", 80)},
		{"utf8-bad", "{\"schema_version\":1,\"version\":1,\"knobs\":{\"a\xff:b\":{\"type\":\"bool\",\"default\":false}}}"},
	};
	for (const auto& [name, text] : bad) {
		auto r = parse_source_snapshot(text, "src");
		w5::line("malformed " + name, r ? "ACCEPTED" : "rejected: " + result_text(r.error()));
		auto d = parse_defaults(text, "defaults");
		w5::line("malformed-defaults " + name, d ? "ACCEPTED" : "rejected: " + result_text(d.error()));
	}
	// 3. Rollout buckets over many (name, hash value) pairs.
	{
		std::uint64_t acc = 0xcbf29ce484222325ULL;
		std::size_t below_half = 0, total = 0;
		for (std::size_t i = 0; i < 300; ++i) {
			std::string name = "ns" + std::to_string(i % 7) + ":knob_" + std::to_string(i);
			for (int u = 0; u < 200; ++u) {
				const auto bucket = stable_bucket(name, "user-" + std::to_string(u));
				acc = w5::fnv(&bucket, sizeof bucket, acc);
				below_half += bucket < 50;
				++total;
			}
		}
		w5::line("buckets", "total=" + std::to_string(total) + " below50=" + std::to_string(below_half) + " digest=" + w5::hex(acc));
	}
	return 0;
}
