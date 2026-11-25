// Service's internals shared by service.cc, service_options.cc and service_runtime.cc: the JSON
// field helpers and the types nested in Service.
#pragma once
#include "service.hh"
#include "change_history.hh"
#include "export_scope.hh"
#include "delivery_export.hh"
#include "detent/json_schema_internal.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_distribution_http/reply_pump.hh"
#include <metrics.hh>
#include <rapidjson/document.h>

namespace detent::authority {
namespace detail {
using Clock = std::chrono::steady_clock;
inline std::uint64_t elapsed_ns(Clock::time_point start, Clock::time_point end = Clock::now()) {
	auto value = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}
inline void check(bool b, const char *message) {
	if (!b) {
		throw std::invalid_argument(message);
	}
}
using Object = rapidjson::Value;
inline void fields(const Object &v, std::initializer_list<std::string_view> allowed) {
	check(v.IsObject(), "expected object");
	std::set<std::string_view> seen;
	for (auto it = v.MemberBegin(); it != v.MemberEnd(); ++it) {
		std::string_view key(it->name.GetString(), it->name.GetStringLength());
		check(seen.insert(key).second && std::find(allowed.begin(), allowed.end(), key) != allowed.end(),
			  "unknown or duplicate field");
	}
}
inline const Object &field(const Object &v, const char *key) {
	check(v.HasMember(key), "missing field");
	return v[key];
}
inline std::string text(const Object &v, std::size_t cap = 200) {
	check(v.IsString() && v.GetStringLength() <= cap, "invalid string");
	auto value = std::string(v.GetString(), v.GetStringLength());
	check(value.find('\0') == std::string::npos, "NUL in string");
	return value;
}
inline std::uint64_t number(const Object &v) {
	check(v.IsUint64(), "invalid unsigned integer");
	return v.GetUint64();
}
inline std::optional<std::uint64_t> optional(const Object &v, const char *key) {
	return v.HasMember(key) ? std::optional<std::uint64_t>{number(v[key])} : std::nullopt;
}
inline Identity identity(std::string_view hex) {
	check(hex.size() == 32, "identity requires 32 hex characters");
	Identity id{};
	auto nibble = [](char c) {
		if (c >= '0' && c <= '9') {
			return c - '0';
		}
		if (c >= 'a' && c <= 'f') {
			return c - 'a' + 10;
		}
		throw std::invalid_argument("invalid identity hex");
	};
	for (unsigned i = 0; i < 16; ++i) {
		id[i] = static_cast<char>((nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]));
	}
	check(id != Identity{}, "zero identity");
	return id;
}
// W13: a credential's patterns as one matcher; a malformed pattern is a request error.
inline distribution::NamespaceGrants grants_of(const std::set<std::string> &patterns) {
	check(!patterns.empty() && patterns.size() <= distribution::NamespaceGrants::maximum_patterns,
		  "namespace authorization bound");
	std::vector<std::string_view> views(patterns.begin(), patterns.end());
	return distribution::NamespaceGrants(views);
}
inline std::string hex(Identity id) {
	constexpr char digits[] = "0123456789abcdef";
	std::string out;
	for (unsigned char byte : id) {
		out += digits[byte >> 4];
		out += digits[byte & 15];
	}
	return out;
}
inline void unique_members(const rapidjson::Value &value) {
	if (value.IsObject()) {
		std::set<std::string_view> names;
		for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it) {
			check(names.insert({it->name.GetString(), it->name.GetStringLength()}).second,
				  "duplicate JSON field");
			unique_members(it->value);
		}
	} else if (value.IsArray())
		for (const auto &item : value.GetArray())
			unique_members(item);
}
inline rapidjson::Document document(std::string_view bytes, std::size_t cap) {
	check(bytes.size() <= cap && bytes.find('\0') == std::string_view::npos &&
			  !detent::internal::nesting_depth_exceeds(bytes, 64),
		  "JSON bound");
	rapidjson::Document d;
	d.Parse<rapidjson::kParseIterativeFlag | rapidjson::kParseValidateEncodingFlag>(bytes.data(),
																					bytes.size());
	check(!d.HasParseError() && d.IsObject(), "invalid JSON object");
	unique_members(d);
	return d;
}
} // namespace detail
using namespace detail;
struct Service::Accounting {
	struct GrantView {
		distribution::ScopeIdentity digest;
		std::vector<std::string_view, distribution::StorageAllocator<std::string_view>> names;
		explicit GrantView(std::shared_ptr<distribution::StorageLedger> account)
			: names(distribution::StorageAllocator<std::string_view>(std::move(account))) {}
	};
	using GrantViews = std::vector<GrantView, distribution::StorageAllocator<GrantView>>;
	std::shared_ptr<distribution::StorageLedger> delivery_account;
	std::shared_ptr<std::pmr::memory_resource> delivery_resource;
	std::unique_ptr<SubscriptionRequests> subscription_requests;
	std::mutex mutex;
	std::vector<Credential> credentials;
	std::vector<Digest> credential_hashes;
	std::optional<GrantViews> credential_grants;
	std::map<std::string, std::size_t> producers;
	std::size_t requests = 0, body_bytes = 0, reply_bytes = 0;
	kronuz::metrics::AtomicDurationHistogram publisher_ready, reply_retired;
};
struct Service::Delivery : distribution_http::OwnedReplyPump {
	std::shared_ptr<const AuthorizedScope> scope;
	std::optional<distribution::Cursor> base;
	ExportSelection selection = ExportSelection::Automatic;
	std::optional<ChangeHistory::Handoff> handoff;
	bool compress = false, retiring = false;
};
struct Service::Admission {
	std::shared_ptr<Accounting> accounting;
	const Credential *credential;
	std::size_t bytes;
	std::size_t credential_index = 0;
	std::shared_ptr<SubscriptionRequests::Lease> subscription;
	bool active = false;
	Clock::time_point admitted = Clock::now();
	~Admission() {
		if (!active) {
			return;
		}
		std::lock_guard lock(accounting->mutex);
		--accounting->requests;
		accounting->body_bytes -= bytes;
		auto found = accounting->producers.find(credential->producer);
		if (found != accounting->producers.end() && --found->second == 0) {
			accounting->producers.erase(found);
		}
	}
};
struct Service::ReplyAdmission {
	std::shared_ptr<Accounting> accounting;
	std::size_t bytes = 0;
	bool active = false;
	Clock::time_point ready = Clock::now();
	~ReplyAdmission() {
		if (active) {
			std::lock_guard lock(accounting->mutex);
			accounting->reply_bytes -= bytes;
			accounting->reply_retired.observe(elapsed_ns(ready));
		}
	}
};
// What a request holds from its headers to its settled response.
struct Service::Extension final : http::RequestExtension {
	std::shared_ptr<Admission> admission;
};
struct Service::Prepared {
	std::optional<Reply> immediate;
	std::shared_ptr<Task> task;
};
// What a command, read or result wait ends with. `buffered` is a reply that must not wait for an
// owned stream: the wait ended on the stop (N1).
struct Service::Outcome {
	Reply reply;
	bool buffered = false;
};
} // namespace detent::authority
