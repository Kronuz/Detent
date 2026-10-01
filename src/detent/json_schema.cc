#include "detent/json_schema.hh"
#include "detent/json_schema_internal.hh"
#include "detent/validation.hh"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <cstdint>
#include <optional>
#include <string>

namespace detent {

namespace internal {

// A single-request DoS found by security review: rapidjson's stock
// recursive-descent Parse() recurses one native C++ stack frame per
// nesting level with NO depth limit, *before* any of this file's own
// bounded-size validators (max_knobs, name-length caps) ever run. A ~1 MiB
// body of a few hundred thousand nested `[`s -- far under the 16 MiB PUT
// cap enforced upstream in detentd -- reliably overruns the thread stack
// and crashes the whole process (reproduced: SIGBUS on macOS, would be
// SIGSEGV on Linux). The same parser is also the *client's* snapshot-
// ingest path (parse_source_snapshot, fed by detent_http from whatever a
// distributor -- or, with no TLS, anything impersonating one -- returns),
// so an unbounded-depth payload can crash every consuming application
// process too, not just detentd. A cheap linear pre-scan for max bracket
// nesting, rejecting before rapidjson ever sees the bytes, is the primary
// defense (bounds memory/CPU, not just the stack); Parse<kParseIterativeFlag>
// below is defense-in-depth in case some other caller of this file's
// parsing ever bypasses the pre-scan.

bool nesting_depth_exceeds(std::string_view json_text, int max_depth) {
	int depth = 0;
	bool in_string = false;
	bool escaped = false;
	for (char c : json_text) {
		if (in_string) {
			if (escaped) {
				escaped = false;
			} else if (c == '\\') {
				escaped = true;
			} else if (c == '"') {
				in_string = false;
			}
			continue;
		}
		if (c == '"') {
			in_string = true;
		} else if (c == '{' || c == '[') {
			if (++depth > max_depth) return true;
		} else if (c == '}' || c == ']') {
			--depth;
		}
	}
	return false;
}

Result<KnobDef, UpdateError> parse_knob_def(const rapidjson::Value& v, const std::string& name) {
	if (!v.IsObject()) {
		return UpdateError{UpdateRejectReason::Schema, "knob '" + name + "' is not an object"};
	}
	if (!v.HasMember("type") || !v["type"].IsString()) {
		return UpdateError{UpdateRejectReason::Schema, "knob '" + name + "' missing/invalid 'type'"};
	}
	std::string type(v["type"].GetString(), v["type"].GetStringLength());

	if (type == "bool") {
		BoolDef def;
		bool have_default = false;
		for (auto it = v.MemberBegin(); it != v.MemberEnd(); ++it) {
			std::string key(it->name.GetString(), it->name.GetStringLength());
			if (key == "type") {
				continue;
			} else if (key == "default") {
				if (!it->value.IsBool()) {
					return UpdateError{UpdateRejectReason::Schema, "bool default not boolean: " + name};
				}
				def.default_value = it->value.GetBool();
				have_default = true;
			} else if (key == "overrides") {
				if (!it->value.IsObject()) {
					return UpdateError{UpdateRejectReason::Schema, "overrides not object: " + name};
				}
				for (auto oit = it->value.MemberBegin(); oit != it->value.MemberEnd(); ++oit) {
					std::string gkey(oit->name.GetString(), oit->name.GetStringLength());
					if (!is_valid_group_key(gkey)) {
						return UpdateError{UpdateRejectReason::Schema, "bad group key on knob " + name};
					}
					GroupOverride value;
					if (oit->value.IsBool()) {
						value = oit->value.GetBool();
					} else if (oit->value.IsObject() && oit->value.MemberCount() == 1 &&
					           oit->value.HasMember("rollout_pct") && oit->value["rollout_pct"].IsUint() &&
					           oit->value["rollout_pct"].GetUint() <= 100) {
						value = GroupRollout{static_cast<std::uint8_t>(oit->value["rollout_pct"].GetUint())};
					} else {
						return UpdateError{UpdateRejectReason::Schema, "override must be boolean or {rollout_pct: 0..100}: " + name};
					}
					// Strict rejection (Section 4): a duplicate override key
					// within one knob's `overrides` object must reject the
					// whole input, not silently keep whichever the JSON
					// parser or this insertion order happened to see last.
					if (!def.overrides.insert({gkey, value}).second) {
						return UpdateError{UpdateRejectReason::Schema, "duplicate override key '" + gkey + "' on knob " + name};
					}
				}
			} else if (key == "rollout_pct") {
				if (it->value.IsNull()) {
					def.rollout_pct.reset();
				} else if (it->value.IsInt()) {
					int pct = it->value.GetInt();
					if (pct < 0 || pct > 100) {
						return UpdateError{UpdateRejectReason::Schema, "rollout_pct out of range: " + name};
					}
					def.rollout_pct = static_cast<std::uint8_t>(pct);
				} else {
					return UpdateError{UpdateRejectReason::Schema, "rollout_pct wrong type: " + name};
				}
			} else {
				return UpdateError{UpdateRejectReason::Schema, "unknown field '" + key + "' on knob " + name};
			}
		}
		// Strict rejection (Section 4/9): a knob definition missing its
		// required "default" must fail the whole update, not silently
		// substitute the type's zero value (false/0) -- confirmed missing
		// previously: {"type":"bool"} parsed successfully as default=false.
		if (!have_default) {
			return UpdateError{UpdateRejectReason::Schema, "bool knob missing required 'default': " + name};
		}
		return KnobDef{std::move(def)};
	} else if (type == "int") {
		IntDef def;
		bool have_default = false;
		for (auto it = v.MemberBegin(); it != v.MemberEnd(); ++it) {
			std::string key(it->name.GetString(), it->name.GetStringLength());
			if (key == "type") {
				continue;
			} else if (key == "default") {
				if (!it->value.IsInt64()) {
					return UpdateError{UpdateRejectReason::Schema, "int default not integer: " + name};
				}
				def.default_value = it->value.GetInt64();
				have_default = true;
			} else {
				return UpdateError{UpdateRejectReason::Schema, "unknown field '" + key + "' on knob " + name};
			}
		}
		if (!have_default) {
			return UpdateError{UpdateRejectReason::Schema, "int knob missing required 'default': " + name};
		}
		return KnobDef{def};
	}
	return UpdateError{UpdateRejectReason::Schema, "unknown type on knob " + name};
}

rapidjson::Value write_knob_def(const KnobDef& def, rapidjson::Document::AllocatorType& alloc) {
	rapidjson::Value entry(rapidjson::kObjectType);
	if (const auto* b = std::get_if<BoolDef>(&def)) {
		entry.AddMember("type", "bool", alloc);
		entry.AddMember("default", b->default_value, alloc);
		rapidjson::Value overrides(rapidjson::kObjectType);
		for (const auto& [gkey, gval] : b->overrides) {
			rapidjson::Value key(gkey.data(), static_cast<rapidjson::SizeType>(gkey.size()), alloc);
			if (const auto* forced = std::get_if<bool>(&gval)) {
				overrides.AddMember(key, *forced, alloc);
			} else {
				rapidjson::Value group(rapidjson::kObjectType);
				group.AddMember("rollout_pct", static_cast<unsigned>(std::get<GroupRollout>(gval).rollout_pct), alloc);
				overrides.AddMember(key, group, alloc);
			}
		}
		entry.AddMember("overrides", overrides, alloc);
		if (b->rollout_pct) {
			entry.AddMember("rollout_pct", static_cast<int>(*b->rollout_pct), alloc);
		} else {
			entry.AddMember("rollout_pct", rapidjson::Value(rapidjson::kNullType), alloc);
		}
	} else {
		const auto& i = std::get<IntDef>(def);
		entry.AddMember("type", "int", alloc);
		entry.AddMember("default", i.default_value, alloc);
	}
	return entry;
}

} // namespace internal

namespace {

using SnapResult = Result<Snapshot, UpdateError>;

SnapResult fail(UpdateRejectReason reason, std::string detail) {
	return UpdateError{reason, std::move(detail)};
}

// Minimal RFC 3339 UTC shape check: YYYY-MM-DDTHH:MM:SS(.fff)?Z. Not a full
// calendar validator; good enough to reject obviously malformed input per
// Section 4 ("reject if present and invalid").
bool is_iso8601_utc(std::string_view s) {
	auto digit = [](char c) { return c >= '0' && c <= '9'; };
	if (s.size() < 20 || s.back() != 'Z') return false;
	static constexpr int kDigitPositions[] = {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18};
	for (int pos : kDigitPositions) {
		if (!digit(s[static_cast<std::size_t>(pos)])) return false;
	}
	return s[4] == '-' && s[7] == '-' && s[10] == 'T' && s[13] == ':' && s[16] == ':';
}

SnapResult parse_common(std::string_view json_text, std::string source_id, std::size_t max_knobs,
                         std::size_t max_bytes, bool is_source) {
	// Every caller of parse_common (parse_defaults, parse_source_snapshot)
	// used to rely on the *caller* enforcing a byte cap -- true for
	// detentd's own PUT ingest (CappedBodySink) and the file source's own
	// read, but not for an arbitrary InitOptions::source_poll
	// implementation or a raw HttpConnection response body, so an
	// untrusted/spoofed source (there is no TLS/auth) could still drive
	// unbounded parsing/allocation through those paths. Enforce it here,
	// once, so every caller gets it whether or not it remembers to.
	if (json_text.size() > max_bytes) {
		return fail(UpdateRejectReason::Oversize, "input exceeds max_bytes");
	}

	// See kMaxJsonNestingDepth's comment: reject unbounded nesting before
	// rapidjson ever sees the bytes -- this is a real, reproduced,
	// unauthenticated single-request crash otherwise (security review
	// finding F1), not a theoretical concern.
	if (internal::nesting_depth_exceeds(json_text, internal::kMaxJsonNestingDepth)) {
		return fail(UpdateRejectReason::Schema, "JSON nesting depth exceeds limit");
	}

	if (json_text.find('\0') != std::string_view::npos) return fail(UpdateRejectReason::Parse, "NUL byte in JSON text");
	rapidjson::Document doc;
	// Strict parsing: rapidjson's stock default flags already reject
	// comments, trailing commas, and NaN/Inf; unlike Xapiand we never relax
	// RAPIDJSON_PARSE_DEFAULT_FLAGS (see CMakeLists.txt comment).
	// kParseIterativeFlag: constant-stack (heap-based) parsing instead of
	// rapidjson's default recursive descent -- defense-in-depth alongside
	// the depth pre-scan above, in case some other caller of this function
	// is ever added without going through it.
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
	std::optional<std::string> published_at;
	const rapidjson::Value* knobs_value = nullptr;

	for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it) {
		std::string key(it->name.GetString(), it->name.GetStringLength());
		// Strict rejection (Section 4): rapidjson's DOM keeps every member
		// on a duplicate key, not just the last one; a naive "if/else if"
		// walk (as this loop already does per-field below) would silently
		// let the last duplicate win instead of rejecting the whole input.
		if ((key == "schema_version" && have_schema_version) ||
		    (key == "version" && have_version) ||
		    (key == "published_at" && published_at.has_value()) ||
		    (key == "knobs" && knobs_value != nullptr)) {
			return fail(UpdateRejectReason::Schema, "duplicate top-level field: " + key);
		}
		if (key == "schema_version") {
			if (!it->value.IsInt() || it->value.GetInt() != 1) {
				return fail(UpdateRejectReason::Schema, "schema_version must be exactly 1");
			}
			have_schema_version = true;
		} else if (key == "version") {
			if (!is_source) {
				return fail(UpdateRejectReason::Schema, "unexpected 'version' in defaults file");
			}
			if (!it->value.IsUint64() || it->value.GetUint64() == 0) {
				return fail(UpdateRejectReason::Version, "version must be a positive integer");
			}
			version = it->value.GetUint64();
			have_version = true;
		} else if (key == "published_at") {
			if (!is_source) {
				return fail(UpdateRejectReason::Schema, "unexpected 'published_at' in defaults file");
			}
			if (!it->value.IsString() || !is_iso8601_utc(std::string_view(it->value.GetString(), it->value.GetStringLength()))) {
				return fail(UpdateRejectReason::Schema, "published_at is not a valid RFC 3339 UTC timestamp");
			}
			published_at = it->value.GetString();
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
	if (is_source && !have_version) return fail(UpdateRejectReason::Version, "missing version");
	if (knobs_value == nullptr) return fail(UpdateRejectReason::Schema, "missing 'knobs'");
	if (static_cast<std::size_t>(knobs_value->MemberCount()) > max_knobs) {
		return fail(UpdateRejectReason::Oversize, "too many knobs");
	}

	Snapshot snap;
	snap.version = is_source ? version : 0;
	snap.published_at = std::move(published_at);
	snap.source_id = std::move(source_id);

	for (auto it = knobs_value->MemberBegin(); it != knobs_value->MemberEnd(); ++it) {
		std::string name(it->name.GetString(), it->name.GetStringLength());
		if (!is_valid_knob_name(name)) {
			return fail(UpdateRejectReason::Schema, "invalid knob name: " + name);
		}
		auto def_result = internal::parse_knob_def(it->value, name);
		if (!def_result) return def_result.error();
		// Strict rejection (Section 4): a duplicate knob name must reject
		// the whole input, not silently keep whichever definition this
		// insertion order happened to see last -- confirmed previously:
		// two "a:b" entries parsed successfully with the later one winning.
		if (!snap.knobs.insert({name, std::move(def_result).value()}).second) {
			return fail(UpdateRejectReason::Schema, "duplicate knob name: " + name);
		}
	}

	return snap;
}

} // namespace

Result<Snapshot, UpdateError> parse_defaults(std::string_view json_text, std::string source_id, std::size_t max_knobs,
                                              std::size_t max_bytes) {
	return parse_common(json_text, std::move(source_id), max_knobs, max_bytes, /*is_source=*/false);
}

Result<Snapshot, UpdateError> parse_source_snapshot(std::string_view json_text, std::string source_id,
                                                     std::size_t max_knobs, std::size_t max_bytes) {
	return parse_common(json_text, std::move(source_id), max_knobs, max_bytes, /*is_source=*/true);
}

Snapshot merge_over_defaults(const Snapshot& defaults, const Snapshot& live) {
	Snapshot merged;
	merged.version = live.version;
	merged.published_at = live.published_at;
	merged.source_id = live.source_id;
	merged.knobs = defaults.knobs; // start from the baked defaults, whole map
	for (const auto& [name, def] : live.knobs) {
		merged.knobs[name] = def; // whole-definition replace, never field-wise
	}
	return merged;
}

std::string serialize_snapshot(const Snapshot& snapshot) {
	rapidjson::Document doc;
	doc.SetObject();
	auto& alloc = doc.GetAllocator();

	doc.AddMember("schema_version", 1, alloc);
	doc.AddMember("version", snapshot.version, alloc);
	if (snapshot.published_at) {
		rapidjson::Value v(snapshot.published_at->c_str(), alloc);
		doc.AddMember("published_at", v, alloc);
	}

	rapidjson::Value knobs(rapidjson::kObjectType);
	for (const auto& [name, def] : snapshot.knobs) {
		rapidjson::Value key(name.data(), static_cast<rapidjson::SizeType>(name.size()), alloc);
		knobs.AddMember(key, internal::write_knob_def(def, alloc), alloc);
	}
	doc.AddMember("knobs", knobs, alloc);

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	doc.Accept(writer);
	return buffer.GetString();
}

} // namespace detent
