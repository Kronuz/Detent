#include "detent_distribution/protocol.hh"
#include "detent/utf8.hh"
#include "detent/validation.hh"
#include "detent_distribution/messagepack.hh"
#include <algorithm>
#include <limits>

namespace detent::distribution {
namespace {
namespace m = messagepack;
void require(bool value, const char *message) {
	if (!value)
		throw std::invalid_argument(message);
}
std::uint64_t number(const m::Token &token) {
	require(token.kind == m::Kind::Unsigned || (token.kind == m::Kind::Signed && token.signed_value() >= 0),
			"unsigned distribution integer required");
	return token.number;
}
void array(const m::Token &token, std::uint64_t size) {
	require(token.kind == m::Kind::Array && token.number == size, "distribution tuple arity mismatch");
}
template <std::size_t N> void identity(const m::Token &token, std::array<char, N> &output) {
	require(token.kind == m::Kind::Binary && token.bytes.size() == N, "distribution identity width mismatch");
	std::copy(token.bytes.begin(), token.bytes.end(), output.begin());
}
bool charge(WorkBudget &budget, std::size_t bytes) {
	if (!budget.tokens || budget.bytes < bytes)
		return false;
	--budget.tokens;
	budget.bytes -= bytes;
	return true;
}
bool discard_overrides(BoolDef &definition, WorkBudget &budget) {
	while (!definition.overrides.empty()) {
		auto first = definition.overrides.begin();
		if (!charge(budget, first->first.size() + sizeof(GroupOverride)))
			return false;
		definition.overrides.erase(first);
	}
	return true;
}
} // namespace

bool ValidatedSnapshot::discard(WorkBudget &budget) {
	cursor_ = {};
	while (!records_.empty()) {
		auto &record = records_.back();
		if (auto flag = std::get_if<BoolDef>(&record.definition); flag && !discard_overrides(*flag, budget))
			return false;
		if (!charge(budget, record.key.size() + sizeof(Record)))
			return false;
		records_.pop_back();
	}
	while (!namespaces_.empty()) {
		if (!charge(budget, namespaces_.back().name.size() + sizeof(NamespaceRevision)))
			return false;
		namespaces_.pop_back();
	}
	return true;
}

struct SnapshotDecoder::Impl {
	enum class Phase {
		Root,
		Version,
		Tag,
		ProfileTuple,
		ProfileId,
		Capabilities,
		CursorTuple,
		Cluster,
		Group,
		Epoch,
		Scope,
		Revision,
		CountsTuple,
		NamespaceCount,
		RecordCount,
		OverrideCount,
		StringCount,
		Namespaces,
		NamespaceTuple,
		NamespaceName,
		NamespaceRevision,
		Records,
		RecordTuple,
		NamespaceIndex,
		Key,
		KeyRevision,
		DefinitionTuple,
		DefinitionTag,
		Integer,
		Boolean,
		Rollout,
		Overrides,
		OverrideTuple,
		Switch,
		OverrideTag,
		OverrideValue,
		End
	};
	Cursor expected;
	std::vector<std::string> scope;
	Limits limits;
	m::Decoder decoder;
	ValidatedSnapshot snapshot;
	Record record;
	BoolDef boolean;
	std::string switch_key, previous_key;
	std::uint64_t declared_namespaces = 0, declared_records = 0, declared_overrides = 0, declared_strings = 0;
	std::uint64_t seen_overrides = 0, seen_strings = 0, remaining_overrides = 0, definition_arity = 0,
				  override_tag = 0, definition_bytes = 0;
	std::uint32_t previous_namespace = 0;
	Phase phase = Phase::Root;
	bool failed = false, taken = false;
	Impl(Cursor cursor, std::vector<std::string> names, Limits bounds)
		: expected(cursor), scope(std::move(names)), limits(bounds),
		  decoder(m::Limits{
			  bounds.bytes, 200, 32,
			  std::max({32u, bounds.records, bounds.namespaces, bounds.overrides_per_definition})}) {
		require(expected.cluster != Identity{} && expected.group != Identity{} &&
					expected.restore_epoch != Identity{} && expected.scope != ScopeIdentity{},
				"unconfigured distribution identity");
		require(scope.size() <= limits.namespaces, "subscription namespace limit exceeded");
		for (std::size_t i = 0; i < scope.size(); ++i) {
			require(!scope[i].empty() && scope[i].size() < 200 && is_valid_knob_name(scope[i] + ":x") &&
						(!i || scope[i - 1] < scope[i]),
					"invalid or unordered subscription scope");
		}
	}
	std::string text(m::Token &token) {
		require(token.kind == m::Kind::String, "distribution string required");
		require(token.bytes.size() <= limits.string_bytes - seen_strings &&
					token.bytes.size() <= declared_strings - seen_strings,
				"distribution string budget exceeded");
		seen_strings += token.bytes.size();
		return std::move(token.bytes);
	}
	void finish_record(KnobDef value) {
		record.definition = std::move(value);
		previous_namespace = record.namespace_index;
		previous_key = record.key;
		snapshot.records_.push_back(std::move(record));
		record = {};
		phase = snapshot.records_.size() == declared_records ? Phase::End : Phase::RecordTuple;
	}
	void consume(m::Token token) {
		switch (phase) {
		case Phase::Root:
			array(token, 7);
			phase = Phase::Version;
			break;
		case Phase::Version:
			require(number(token) == 1, "unsupported distribution version");
			phase = Phase::Tag;
			break;
		case Phase::Tag:
			require(number(token) == 1, "snapshot tag required");
			phase = Phase::ProfileTuple;
			break;
		case Phase::ProfileTuple:
			array(token, 2);
			phase = Phase::ProfileId;
			break;
		case Phase::ProfileId:
			require(number(token) == 1, "unsupported semantic profile");
			phase = Phase::Capabilities;
			break;
		case Phase::Capabilities:
			snapshot.profile_.required_capabilities = number(token);
			require((snapshot.profile_.required_capabilities & ~1ull) == 0,
					"unsupported required capabilities");
			phase = Phase::CursorTuple;
			break;
		case Phase::CursorTuple:
			array(token, 5);
			phase = Phase::Cluster;
			break;
		case Phase::Cluster:
			identity(token, snapshot.cursor_.cluster);
			phase = Phase::Group;
			break;
		case Phase::Group:
			identity(token, snapshot.cursor_.group);
			phase = Phase::Epoch;
			break;
		case Phase::Epoch:
			identity(token, snapshot.cursor_.restore_epoch);
			phase = Phase::Scope;
			break;
		case Phase::Scope:
			identity(token, snapshot.cursor_.scope);
			require(snapshot.cursor_.same_identity(expected),
					"snapshot identity does not match configured scope");
			phase = Phase::Revision;
			break;
		case Phase::Revision:
			snapshot.cursor_.revision = number(token);
			require(snapshot.cursor_.revision >= expected.revision, "snapshot revision rollback");
			phase = Phase::CountsTuple;
			break;
		case Phase::CountsTuple:
			array(token, 4);
			phase = Phase::NamespaceCount;
			break;
		case Phase::NamespaceCount:
			declared_namespaces = number(token);
			require(declared_namespaces == scope.size(), "snapshot namespace count mismatch");
			phase = Phase::RecordCount;
			break;
		case Phase::RecordCount:
			declared_records = number(token);
			require(declared_records <= limits.records, "snapshot record limit exceeded");
			phase = Phase::OverrideCount;
			break;
		case Phase::OverrideCount:
			declared_overrides = number(token);
			require(declared_overrides <= declared_records * limits.overrides_per_definition,
					"snapshot override limit exceeded");
			phase = Phase::StringCount;
			break;
		case Phase::StringCount:
			declared_strings = number(token);
			require(declared_strings <= limits.string_bytes, "snapshot string limit exceeded");
			phase = Phase::Namespaces;
			break;
		case Phase::Namespaces:
			array(token, declared_namespaces);
			phase = declared_namespaces ? Phase::NamespaceTuple : Phase::Records;
			break;
		case Phase::NamespaceTuple:
			array(token, 2);
			phase = Phase::NamespaceName;
			break;
		case Phase::NamespaceName: {
			auto name = text(token);
			require(name == scope[snapshot.namespaces_.size()], "snapshot namespace order/scope mismatch");
			snapshot.namespaces_.push_back({std::move(name), 0});
			phase = Phase::NamespaceRevision;
			break;
		}
		case Phase::NamespaceRevision:
			snapshot.namespaces_.back().revision = number(token);
			require(snapshot.namespaces_.back().revision <= snapshot.cursor_.revision,
					"namespace revision exceeds snapshot");
			phase =
				snapshot.namespaces_.size() == declared_namespaces ? Phase::Records : Phase::NamespaceTuple;
			break;
		case Phase::Records:
			array(token, declared_records);
			phase = declared_records ? Phase::RecordTuple : Phase::End;
			break;
		case Phase::RecordTuple:
			array(token, 4);
			phase = Phase::NamespaceIndex;
			break;
		case Phase::NamespaceIndex: {
			auto index = number(token);
			require(index < scope.size(), "snapshot namespace index out of range");
			record.namespace_index = static_cast<std::uint32_t>(index);
			phase = Phase::Key;
			break;
		}
		case Phase::Key:
			record.key = text(token);
			require(is_valid_knob_name(record.key) &&
						record.key.substr(0, record.key.find(':')) == scope[record.namespace_index],
					"snapshot key namespace mismatch");
			require(snapshot.records_.empty() || previous_namespace < record.namespace_index ||
						(previous_namespace == record.namespace_index && previous_key < record.key),
					"unordered or duplicate snapshot key");
			phase = Phase::KeyRevision;
			break;
		case Phase::KeyRevision:
			record.revision = number(token);
			require(record.revision &&
						record.revision <= snapshot.namespaces_[record.namespace_index].revision,
					"invalid snapshot key revision");
			phase = Phase::DefinitionTuple;
			break;
		case Phase::DefinitionTuple:
			require(token.kind == m::Kind::Array && (token.number == 2 || token.number == 4),
					"invalid definition tuple");
			definition_arity = token.number;
			phase = Phase::DefinitionTag;
			break;
		case Phase::DefinitionTag: {
			auto tag = number(token);
			require((tag == 1 && definition_arity == 2) || (tag == 0 && definition_arity == 4),
					"invalid definition tag/arity");
			phase = tag ? Phase::Integer : Phase::Boolean;
			break;
		}
		case Phase::Integer:
			require(limits.definition_bytes >= 9, "integer definition byte limit exceeded");
			finish_record(IntDef{token.signed_value()});
			break;
		case Phase::Boolean:
			require(token.kind == m::Kind::Boolean, "boolean default required");
			boolean = {};
			boolean.default_value = token.number;
			phase = Phase::Rollout;
			break;
		case Phase::Rollout:
			if (token.kind != m::Kind::Nil) {
				auto pct = number(token);
				require(pct <= 100, "rollout percentage out of range");
				boolean.rollout_pct = static_cast<std::uint8_t>(pct);
			}
			definition_bytes = boolean.rollout_pct ? 19 : 11;
			require(definition_bytes <= limits.definition_bytes, "boolean definition byte limit exceeded");
			phase = Phase::Overrides;
			break;
		case Phase::Overrides:
			require(token.kind == m::Kind::Array && token.number <= limits.overrides_per_definition &&
						token.number <= declared_overrides - seen_overrides,
					"definition override count invalid");
			remaining_overrides = token.number;
			if (remaining_overrides)
				phase = Phase::OverrideTuple;
			else
				finish_record(std::move(boolean));
			break;
		case Phase::OverrideTuple:
			array(token, 3);
			phase = Phase::Switch;
			break;
		case Phase::Switch:
			switch_key = text(token);
			require(is_valid_group_key(switch_key) && valid_utf8(switch_key, false) &&
						(boolean.overrides.empty() || boolean.overrides.rbegin()->first < switch_key),
					"invalid or unordered override switch");
			phase = Phase::OverrideTag;
			break;
		case Phase::OverrideTag:
			override_tag = number(token);
			require(override_tag <= 1, "unknown override tag");
			phase = Phase::OverrideValue;
			break;
		case Phase::OverrideValue:
			definition_bytes += 8 + switch_key.size() + 1 + (override_tag ? 8 : 1);
			require(definition_bytes <= limits.definition_bytes, "override definition byte limit exceeded");
			if (!override_tag) {
				require(token.kind == m::Kind::Boolean, "forced override boolean required");
				boolean.overrides.emplace(std::move(switch_key), bool(token.number));
			} else {
				auto pct = number(token);
				require(pct <= 100, "override rollout out of range");
				boolean.overrides.emplace(std::move(switch_key),
										  GroupRollout{static_cast<std::uint8_t>(pct)});
			}
			++seen_overrides;
			if (--remaining_overrides)
				phase = Phase::OverrideTuple;
			else
				finish_record(std::move(boolean));
			break;
		case Phase::End:
			throw std::invalid_argument("trailing snapshot object");
		}
	}
};

SnapshotDecoder::SnapshotDecoder(Cursor expected, std::vector<std::string> namespaces, Limits limits)
	: impl_(std::make_unique<Impl>(expected, std::move(namespaces), limits)) {}
SnapshotDecoder::~SnapshotDecoder() = default;
SnapshotDecoder::SnapshotDecoder(SnapshotDecoder &&) noexcept = default;
void SnapshotDecoder::feed(std::span<const char> &input, WorkBudget &budget) {
	require(impl_ && !impl_->failed && !impl_->taken, "snapshot decoder unavailable");
	try {
		while (!input.empty() && budget.bytes && budget.tokens) {
			if (auto token = impl_->decoder.next(input, budget.bytes)) {
				--budget.tokens;
				impl_->consume(std::move(*token));
			}
		}
	} catch (...) {
		impl_->failed = true;
		throw;
	}
}
ValidatedSnapshot SnapshotDecoder::finish() {
	require(impl_ && !impl_->failed && !impl_->taken, "snapshot decoder unavailable");
	try {
		impl_->decoder.finish();
		require(impl_->phase == Impl::Phase::End && impl_->seen_overrides == impl_->declared_overrides &&
					impl_->seen_strings == impl_->declared_strings,
				"incomplete snapshot or aggregate count mismatch");
		impl_->taken = true;
		return std::move(impl_->snapshot);
	} catch (...) {
		impl_->failed = true;
		throw;
	}
}
bool SnapshotDecoder::discard(WorkBudget &budget) {
	if (!impl_)
		return true;
	impl_->failed = true;
	if (!discard_overrides(impl_->boolean, budget) || !impl_->snapshot.discard(budget))
		return false;
	while (!impl_->scope.empty()) {
		if (!charge(budget, impl_->scope.back().size() + sizeof(std::string)))
			return false;
		impl_->scope.pop_back();
	}
	impl_.reset();
	return true;
}
} // namespace detent::distribution
