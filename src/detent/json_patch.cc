#include "detent/json_patch.hh"
#include "detent/flat_map.hh"
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

	if (json_text.find('\0') != std::string_view::npos) return fail(UpdateRejectReason::Parse, "NUL byte in JSON text");
	rapidjson::Document doc;
	doc.Parse<rapidjson::kParseIterativeFlag | rapidjson::kParseValidateEncodingFlag>(json_text.data(), json_text.size());
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
		std::string key(it->name.GetString(), it->name.GetStringLength());
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

	// Strict rejection, matching parse_common's duplicate-knob-name
	// handling: a repeated key in the patch's own "knobs" object must
	// reject the whole patch, not silently keep the last one. Entries are
	// appended in input order and sorted once; the first duplicate in input
	// order is reported and wins over any error found after its second
	// occurrence.
	flat_map_builder<KnobName, std::optional<KnobDef>, std::less<>> builder(patch.knobs);
	builder.reserve(static_cast<std::size_t>(knobs_value->MemberCount()));
	const auto duplicate_error = [&]() -> std::optional<PatchResult> {
		if (const auto duplicate = builder.finish()) {
			return fail(UpdateRejectReason::Schema, "duplicate knob name: " + builder.entry(*duplicate).first);
		}
		return std::nullopt;
	};
	for (auto it = knobs_value->MemberBegin(); it != knobs_value->MemberEnd(); ++it) {
		std::string name(it->name.GetString(), it->name.GetStringLength());
		if (!is_valid_knob_name(name)) {
			if (auto duplicate = duplicate_error()) return std::move(*duplicate);
			return fail(UpdateRejectReason::Schema, "invalid knob name: " + name);
		}
		std::optional<KnobDef> value;
		if (!it->value.IsNull()) {
			auto def_result = internal::parse_knob_def(it->value, name);
			if (!def_result) {
				if (auto duplicate = duplicate_error()) return std::move(*duplicate);
				return def_result.error();
			}
			value = std::move(def_result).value();
		}
		builder.push(std::move(name), std::move(value));
	}
	if (auto duplicate = duplicate_error()) return std::move(*duplicate);

	return patch;
}

Result<Snapshot, UpdateError> apply_patch(const Snapshot& base, const SnapshotPatch& patch, std::size_t max_knobs) {
	// Linear merge of two sorted tables. A patch entry with a definition is an
	// upsert (new knob or whole-definition replace, never field-wise); a null
	// entry removes the knob (absent from the live overlay -> falls back to
	// its baked default, if any) and is a no-op for a knob that is absent.
	std::size_t count = 0;
	flat_map_merge(
		base.knobs, patch.knobs, [&](const auto&) { ++count; }, [&](const auto& e) { count += e.second ? 1 : 0; },
		[&](const auto&, const auto& e) { count += e.second ? 1 : 0; });
	if (count > max_knobs) {
		return UpdateError{UpdateRejectReason::Oversize, "patch result exceeds max_knobs"};
	}
	Snapshot out;
	out.version = patch.version;
	out.published_at = base.published_at;
	out.source_id = base.source_id;
	out.knobs.reserve(count);
	flat_map_merge(
		base.knobs, patch.knobs, [&](const auto& e) { out.knobs.emplace(e); },
		[&](const auto& e) {
			if (e.second) out.knobs.emplace(e.first, *e.second);
		},
		[&](const auto&, const auto& e) {
			if (e.second) out.knobs.emplace(e.first, *e.second);
		});
	return out;
}

SnapshotPatch diff_snapshots(const Snapshot& from, const Snapshot& to) {
	SnapshotPatch patch;
	patch.version = to.version;
	flat_map_merge(
		from.knobs, to.knobs,
		[&](const auto& e) { patch.knobs.emplace(e.first, std::nullopt); },        // removed
		[&](const auto& e) { patch.knobs.emplace(e.first, e.second); },            // new
		[&](const auto& f, const auto& t) {
			if (!(f.second == t.second)) patch.knobs.emplace(t.first, t.second); // changed
		});
	patch.knobs.shrink_to_fit();
	return patch;
}

SnapshotPatch compose_patches(const SnapshotPatch& older, const SnapshotPatch& newer) {
	SnapshotPatch composed;
	composed.version = newer.version;
	std::size_t count = 0;
	flat_map_merge(older.knobs, newer.knobs, [&](const auto&) { ++count; }, [&](const auto&) { ++count; },
	               [&](const auto&, const auto&) { ++count; });
	composed.knobs.reserve(count);
	flat_map_merge(
		older.knobs, newer.knobs, [&](const auto& e) { composed.knobs.emplace(e); },
		[&](const auto& e) { composed.knobs.emplace(e); }, [&](const auto&, const auto& e) { composed.knobs.emplace(e); });
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
		rapidjson::Value key(name.data(), static_cast<rapidjson::SizeType>(name.size()), alloc);
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
