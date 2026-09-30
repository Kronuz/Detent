#include "detent/json_patch.hh"
#include "detent/json_schema_internal.hh"
#include "detent/validation.hh"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <optional>
#include <utility>

namespace detent {

namespace {

using PatchResult = Result<SnapshotPatch, UpdateError>;

PatchResult fail(UpdateRejectReason reason, std::string detail) {
	return UpdateError{reason, std::move(detail)};
}

} // namespace

Result<SnapshotPatch, UpdateError> parse_snapshot_patch(
	std::string_view json_text, std::string /*source_id*/, std::size_t max_knobs, std::size_t max_bytes) {
	// Same three defenses parse_common (json_schema.cc) applies, in the same
	// order, for the same reasons: an untrusted/spoofed PATCH body gets no
	// less scrutiny than a full snapshot just because it's usually smaller.
	if (json_text.size() > max_bytes) {
		return fail(UpdateRejectReason::Oversize, "input exceeds max_bytes");
	}
	if (internal::nesting_depth_exceeds(json_text, internal::kMaxJsonNestingDepth)) {
		return fail(UpdateRejectReason::Schema, "JSON nesting depth exceeds limit");
	}

	rapidjson::Document doc;
	doc.Parse<rapidjson::kParseIterativeFlag>(json_text.data(), json_text.size());
	if (doc.HasParseError()) {
		return fail(UpdateRejectReason::Parse, "invalid JSON");
	}
	if (!doc.IsObject()) {
		return fail(UpdateRejectReason::Schema, "root is not an object");
	}

	bool have_schema_version = false;
	bool have_version = false;
	std::uint64_t version = 0;
	const rapidjson::Value* knobs_value = nullptr;

	for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it) {
		std::string key = it->name.GetString();
		if ((key == "schema_version" && have_schema_version) ||
		    (key == "version" && have_version) ||
		    (key == "knobs" && knobs_value != nullptr)) {
			return fail(UpdateRejectReason::Schema, "duplicate top-level field: " + key);
		}
		if (key == "schema_version") {
			if (!it->value.IsInt() || it->value.GetInt() != 1) {
				return fail(UpdateRejectReason::Schema, "schema_version must be exactly 1");
			}
			have_schema_version = true;
		} else if (key == "version") {
			if (!it->value.IsUint64() || it->value.GetUint64() == 0) {
				return fail(UpdateRejectReason::Version, "version must be a positive integer");
			}
			version = it->value.GetUint64();
			have_version = true;
		} else if (key == "knobs") {
			if (!it->value.IsObject()) {
				return fail(UpdateRejectReason::Schema, "'knobs' is not an object");
			}
			knobs_value = &it->value;
		} else {
			return fail(UpdateRejectReason::Schema, "unknown top-level field: " + key);
		}
	}

	if (!have_schema_version) return fail(UpdateRejectReason::Schema, "missing schema_version");
	if (!have_version) return fail(UpdateRejectReason::Version, "missing version");
	if (knobs_value == nullptr) return fail(UpdateRejectReason::Schema, "missing 'knobs'");
	// A patch's own knob COUNT isn't bounded by max_knobs the same way a
	// full snapshot's is -- a patch of N entries can still grow the result
	// past max_knobs (checked in apply_patch, where the result is known) or
	// stay well under it (the common case: a handful of changed knobs) --
	// but an absurdly large patch body is still worth rejecting outright
	// rather than doing the work of validating it knob-by-knob first.
	if (static_cast<std::size_t>(knobs_value->MemberCount()) > max_knobs) {
		return fail(UpdateRejectReason::Oversize, "too many knobs in patch");
	}

	SnapshotPatch patch;
	patch.version = version;

	for (auto it = knobs_value->MemberBegin(); it != knobs_value->MemberEnd(); ++it) {
		std::string name = it->name.GetString();
		if (!is_valid_knob_name(name)) {
			return fail(UpdateRejectReason::Schema, "invalid knob name: " + name);
		}
		std::optional<KnobDef> value;
		if (!it->value.IsNull()) {
			auto def_result = internal::parse_knob_def(it->value, name);
			if (!def_result) return def_result.error();
			value = std::move(def_result).value();
		}
		// Strict rejection, matching parse_common's duplicate-knob-name
		// handling: a repeated key in the patch's own "knobs" object must
		// reject the whole patch, not silently keep the last one.
		if (!patch.knobs.insert({name, std::move(value)}).second) {
			return fail(UpdateRejectReason::Schema, "duplicate knob name: " + name);
		}
	}

	return patch;
}

Result<Snapshot, UpdateError> apply_patch(const Snapshot& base, const SnapshotPatch& patch, std::size_t max_knobs) {
	Snapshot out = base;
	out.version = patch.version;
	for (const auto& [name, def] : patch.knobs) {
		if (def) {
			out.knobs[name] = *def; // upsert: new knob or whole-definition replace, never field-wise
		} else {
			out.knobs.erase(name); // absent from the live overlay -> falls back to its baked default, if any
		}
	}
	if (out.knobs.size() > max_knobs) {
		return UpdateError{UpdateRejectReason::Oversize, "patch result exceeds max_knobs"};
	}
	return out;
}

SnapshotPatch diff_snapshots(const Snapshot& from, const Snapshot& to) {
	SnapshotPatch patch;
	patch.version = to.version;
	for (const auto& [name, def] : to.knobs) {
		auto it = from.knobs.find(name);
		if (it == from.knobs.end() || !(it->second == def)) {
			patch.knobs[name] = def; // new or changed
		}
	}
	for (const auto& [name, def] : from.knobs) {
		(void)def;
		if (to.knobs.find(name) == to.knobs.end()) {
			patch.knobs[name] = std::nullopt; // removed
		}
	}
	return patch;
}

SnapshotPatch compose_patches(const SnapshotPatch& older, const SnapshotPatch& newer) {
	SnapshotPatch composed;
	composed.version = newer.version;
	composed.knobs = older.knobs;
	for (const auto& [name, entry] : newer.knobs) composed.knobs[name] = entry;
	return composed;
}

std::string serialize_patch(const SnapshotPatch& patch) {
	rapidjson::Document doc;
	doc.SetObject();
	auto& alloc = doc.GetAllocator();

	doc.AddMember("schema_version", 1, alloc);
	doc.AddMember("version", patch.version, alloc);

	rapidjson::Value knobs(rapidjson::kObjectType);
	for (const auto& [name, def] : patch.knobs) {
		rapidjson::Value key(name.c_str(), alloc);
		if (def) {
			knobs.AddMember(key, internal::write_knob_def(*def, alloc), alloc);
		} else {
			knobs.AddMember(key, rapidjson::Value(rapidjson::kNullType), alloc);
		}
	}
	doc.AddMember("knobs", knobs, alloc);

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	doc.Accept(writer);
	return buffer.GetString();
}

} // namespace detent
