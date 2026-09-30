// Reference publisher tool -- NOT a "Detent server". The spec explicitly
// scopes control-plane/publishing out of the client (Section 15: "Management
// UI / control plane -- Not Built -- No public Detent design; not needed
// for client correctness"). Section 10's KnobSource contract is
// publisher-agnostic: "This client consumes snapshots; it does not decide
// who may publish them." This tool is just a minimal, honest implementation
// of that extension seam -- something to atomically write a valid,
// version-bumped snapshot file so the file-source update path can be
// exercised end to end, alongside examples/demo.cc.
//
// Usage:
//   detent_publish <target.json> [name=value ...] [--clear name ...]
//                      [--override name:group=value ...] [--rollout name=pct ...]
//
// Each run reads the target file's CURRENT live overlay (if it exists),
// applies the requested changes, bumps the version by one, and atomically
// rewrites the file (temp file + rename), matching the atomic-replacement
// contract Section 10 assumes of any publisher.
#include "detent/json_schema.hh"
#include "detent/validation.hh"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace detent;

namespace {

Snapshot load_existing(const std::filesystem::path& path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) return Snapshot{}; // no file yet: version 0, empty overlay
	std::ostringstream ss;
	ss << in.rdbuf();
	auto parsed = parse_source_snapshot(ss.str(), "publish_snapshot");
	if (!parsed) {
		std::fprintf(stderr, "warning: existing %s is not a valid snapshot (%s); starting fresh\n",
		              path.c_str(), parsed.error().detail.c_str());
		return Snapshot{};
	}
	return std::move(parsed).value();
}

BoolDef& as_bool(Snapshot& snap, const std::string& name) {
	auto it = snap.knobs.find(name);
	if (it != snap.knobs.end()) {
		if (auto* b = std::get_if<BoolDef>(&it->second)) return *b;
		throw std::runtime_error("knob already has a different type: " + name);
	}
	snap.knobs[name] = BoolDef{};
	return std::get<BoolDef>(snap.knobs[name]);
}

void set_value(Snapshot& snap, const std::string& name, const std::string& value) {
	if (!is_valid_knob_name(name)) throw std::runtime_error("invalid knob name: " + name);
	if (value == "true" || value == "false") {
		auto& def = as_bool(snap, name);
		def.default_value = (value == "true");
	} else {
		char* end = nullptr;
		long long parsed = std::strtoll(value.c_str(), &end, 10);
		if (end == value.c_str() || *end != '\0') {
			throw std::runtime_error("value is neither true/false nor an integer: " + value);
		}
		snap.knobs[name] = IntDef{static_cast<std::int64_t>(parsed)};
	}
}

void set_override(Snapshot& snap, const std::string& name_group, const std::string& value) {
	auto colon = name_group.rfind(':');
	// name itself contains one colon (knobset:local); the group separator is
	// the SECOND colon this tool's own CLI syntax introduces.
	auto second = name_group.find(':', name_group.find(':') + 1);
	if (second == std::string::npos) throw std::runtime_error("expected name:group=value, got: " + name_group);
	auto name = name_group.substr(0, second);
	auto group = name_group.substr(second + 1);
	if (value != "true" && value != "false") throw std::runtime_error("group override must be true/false");
	auto& def = as_bool(snap, name);
	def.overrides[group] = (value == "true");
	(void)colon;
}

void set_rollout(Snapshot& snap, const std::string& name, const std::string& value) {
	int pct = std::atoi(value.c_str());
	if (pct < 0 || pct > 100) throw std::runtime_error("rollout_pct must be 0..100");
	auto& def = as_bool(snap, name);
	def.rollout_pct = static_cast<std::uint8_t>(pct);
}

void clear_knob(Snapshot& snap, const std::string& name) {
	snap.knobs.erase(name);
}

void write_atomically(const std::filesystem::path& path, const std::string& text) {
	auto tmp = path;
	tmp += ".tmp";
	{
		std::ofstream out(tmp, std::ios::binary);
		out << text;
	}
	std::filesystem::rename(tmp, path);
}

} // namespace

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr,
		              "usage: %s <target.json> [name=value ...] [--clear name ...] "
		              "[--override name:group=value ...] [--rollout name=pct ...]\n",
		              argv[0]);
		return 2;
	}

	std::filesystem::path target = argv[1];
	auto snap = load_existing(target);

	try {
		for (int i = 2; i < argc; ++i) {
			std::string arg = argv[i];
			if (arg == "--clear") {
				if (++i >= argc) throw std::runtime_error("--clear needs a knob name");
				clear_knob(snap, argv[i]);
			} else if (arg == "--override") {
				if (++i >= argc) throw std::runtime_error("--override needs name:group=value");
				std::string pair = argv[i];
				auto eq = pair.find('=');
				if (eq == std::string::npos) throw std::runtime_error("--override needs name:group=value");
				set_override(snap, pair.substr(0, eq), pair.substr(eq + 1));
			} else if (arg == "--rollout") {
				if (++i >= argc) throw std::runtime_error("--rollout needs name=pct");
				std::string pair = argv[i];
				auto eq = pair.find('=');
				if (eq == std::string::npos) throw std::runtime_error("--rollout needs name=pct");
				set_rollout(snap, pair.substr(0, eq), pair.substr(eq + 1));
			} else {
				auto eq = arg.find('=');
				if (eq == std::string::npos) throw std::runtime_error("expected name=value, --clear, --override, or --rollout, got: " + arg);
				set_value(snap, arg.substr(0, eq), arg.substr(eq + 1));
			}
		}
	} catch (const std::exception& e) {
		std::fprintf(stderr, "error: %s\n", e.what());
		return 1;
	}

	snap.version += 1;
	snap.source_id = "publish_snapshot";
	auto text = serialize_snapshot(snap);
	write_atomically(target, text);

	std::printf("published %s: version=%llu, %zu live knob(s)\n", target.c_str(),
	            static_cast<unsigned long long>(snap.version), snap.knobs.size());
	return 0;
}
