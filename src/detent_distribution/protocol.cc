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
std::uint64_t number(const m::WireToken &token) {
	require(token.kind == m::Kind::Unsigned || (token.kind == m::Kind::Signed && token.signed_value() >= 0),
			"unsigned distribution integer required");
	return token.number;
}
void array(const m::WireToken &token, std::uint64_t size) {
	require(token.kind == m::Kind::Array && token.number == size, "distribution tuple arity mismatch");
}
template <std::size_t N> void identity(const m::WireToken &token, std::array<char, N> &output) {
	require(token.kind == m::Kind::Binary && token.bytes.size() == N, "distribution identity width mismatch");
	std::copy(token.bytes.begin(), token.bytes.end(), output.begin());
}
bool charge(WorkBudget &budget, std::size_t bytes, std::size_t &credit) {
	if (!budget.tokens)
		return false;
	const auto amount = std::min(bytes - credit, budget.bytes);
	budget.bytes -= amount;
	credit += amount;
	if (credit != bytes)
		return false;
	--budget.tokens;
	credit = 0;
	return true;
}
template <class Storage> bool retire(Storage &storage, WorkBudget &budget) {
	while (auto capacity = storage.arena.front_capacity()) {
		if (!budget.tokens)
			return false;
		auto bytes = std::min(capacity - storage.retirement_credit, budget.bytes);
		budget.bytes -= bytes;
		storage.retirement_credit += bytes;
		if (storage.retirement_credit != capacity)
			return false;
		--budget.tokens;
		storage.retirement_credit = 0;
		storage.arena.release_one();
	}
	return true;
}
struct DefinitionDecoder {
	enum class Phase {
		DefinitionTuple,
		DefinitionTag,
		Integer,
		Boolean,
		Rollout,
		Overrides,
		OverrideTuple,
		Switch,
		OverrideTag,
		OverrideValue
	};
	const Limits &limits;
	std::uint64_t &declared_overrides;
	std::uint64_t &seen_overrides;
	std::uint64_t &declared_strings;
	std::uint64_t &seen_strings;
	WireBoolDef boolean;
	PagedStorage &arena;
	StableRows<OverrideRow> &overrides;
	std::uint64_t override_begin = 0;
	std::string_view switch_key;
	std::uint64_t remaining_overrides = 0, definition_arity = 0, override_tag = 0, definition_bytes = 0;
	Phase phase = Phase::DefinitionTuple;
	std::optional<WireDefinition> value;
	std::string_view text(m::WireToken &token) {
		require(token.kind == m::Kind::String && token.bytes.size() <= limits.string_bytes - seen_strings &&
					token.bytes.size() <= declared_strings - seen_strings,
				"distribution string budget exceeded");
		seen_strings += token.bytes.size();
		return store_text(arena, token.bytes).value;
	}
	void finish(WireDefinition definition) { value = std::move(definition); }
	void consume(m::WireToken token) {
		require(!value, "definition already decoded");
		switch (phase) {
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
			finish(IntDef{token.signed_value()});
			break;
		case Phase::Boolean:
			require(token.kind == m::Kind::Boolean, "boolean default required");
			boolean = {};
			boolean.default_value = token.number;
			override_begin = overrides.size();
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
				finish(std::move(boolean));
			break;
		case Phase::OverrideTuple:
			array(token, 3);
			phase = Phase::Switch;
			break;
		case Phase::Switch:
			switch_key = text(token);
			require(is_valid_group_key(switch_key) && valid_utf8(switch_key, false) &&
						(!boolean.overrides.count || overrides.back().first < switch_key),
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
				overrides.push_back({switch_key, bool(token.number)});
			} else {
				auto pct = number(token);
				require(pct <= 100, "override rollout out of range");
				overrides.push_back({switch_key, GroupRollout{static_cast<std::uint8_t>(pct)}});
			}
			boolean.overrides = RowRange{override_begin, overrides.size() - override_begin};
			++seen_overrides;
			if (--remaining_overrides)
				phase = Phase::OverrideTuple;
			else
				finish(std::move(boolean));
			break;
		}
	}
};
} // namespace

bool ValidatedSnapshot::discard(WorkBudget &budget) {
	cursor_ = {};
	if (!storage_)
		return true;
	if (!storage_->retiring) {
		storage_->retiring = true;
		storage_->records.clear();
		storage_->namespaces.clear();
		storage_->overrides.clear();
	}
	return retire(*storage_, budget);
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
		End
	};
	Cursor expected;
	std::vector<std::string> scope;
	std::size_t scope_retirement_credit = 0;
	Limits limits;
	m::WireDecoder decoder;
	ValidatedSnapshot snapshot;
	Record record;
	std::optional<DefinitionDecoder> definition;
	std::string_view previous_key;
	std::uint64_t declared_namespaces = 0, declared_records = 0, declared_overrides = 0, declared_strings = 0;
	std::uint64_t seen_overrides = 0, seen_strings = 0;
	std::uint32_t previous_namespace = 0;
	Phase phase = Phase::Root;
	bool failed = false, taken = false;
	Impl(Cursor cursor, std::vector<std::string> names, Limits bounds)
		: expected(cursor), scope(std::move(names)), limits(bounds),
		  decoder(
			  m::Limits{bounds.bytes, 200, 32,
						std::max({32u, bounds.records, bounds.namespaces, bounds.overrides_per_definition})}),
		  snapshot(bounds) {
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
	std::string_view text(m::WireToken &token) {
		require(token.kind == m::Kind::String, "distribution string required");
		require(token.bytes.size() <= limits.string_bytes - seen_strings &&
					token.bytes.size() <= declared_strings - seen_strings,
				"distribution string budget exceeded");
		seen_strings += token.bytes.size();
		return store_text(snapshot.storage_->arena, token.bytes).value;
	}
	void finish_record(WireDefinition value) {
		record.definition = std::move(value);
		previous_namespace = record.namespace_index;
		previous_key = record.key;
		snapshot.storage_->records.push_back(std::move(record));
		record = {};
		phase = snapshot.storage_->records.size() == declared_records ? Phase::End : Phase::RecordTuple;
	}
	void consume(m::WireToken token) {
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
			require(name == scope[snapshot.storage_->namespaces.size()],
					"snapshot namespace order/scope mismatch");
			snapshot.storage_->namespaces.push_back({std::move(name), 0});
			phase = Phase::NamespaceRevision;
			break;
		}
		case Phase::NamespaceRevision:
			snapshot.storage_->namespaces.back().revision = number(token);
			require(snapshot.storage_->namespaces.back().revision <= snapshot.cursor_.revision,
					"namespace revision exceeds snapshot");
			phase = snapshot.storage_->namespaces.size() == declared_namespaces ? Phase::Records
																				: Phase::NamespaceTuple;
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
			require(snapshot.storage_->records.empty() || previous_namespace < record.namespace_index ||
						(previous_namespace == record.namespace_index && previous_key < record.key),
					"unordered or duplicate snapshot key");
			phase = Phase::KeyRevision;
			break;
		case Phase::KeyRevision:
			record.revision = number(token);
			require(record.revision &&
						record.revision <= snapshot.storage_->namespaces[record.namespace_index].revision,
					"invalid snapshot key revision");
			phase = Phase::DefinitionTuple;
			break;
		case Phase::DefinitionTuple:
			if (!definition)
				definition.emplace(DefinitionDecoder{limits,
													 declared_overrides,
													 seen_overrides,
													 declared_strings,
													 seen_strings,
													 {},
													 snapshot.storage_->arena,
													 snapshot.storage_->overrides});
			definition->consume(std::move(token));
			if (definition->value) {
				finish_record(std::move(*definition->value));
				definition.reset();
			}
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
	if (!impl_->snapshot.discard(budget))
		return false;
	while (!impl_->scope.empty()) {
		if (!charge(budget, impl_->scope.back().size() + sizeof(std::string), impl_->scope_retirement_credit))
			return false;
		impl_->scope.pop_back();
	}
	impl_.reset();
	return true;
}

const Mutation &ValidatedDelta::mutation(std::uint64_t ordinal) const {
	require(storage_ && ordinal < storage_->mutations.size(), "delta mutation ordinal out of range");
	return storage_->mutations[static_cast<std::size_t>(ordinal)];
}
const NamespaceRevision &ValidatedDelta::touched(std::uint64_t ordinal) const {
	require(storage_ && ordinal < storage_->touched.size(), "delta namespace ordinal out of range");
	return storage_->touched[static_cast<std::size_t>(ordinal)];
}
bool ValidatedDelta::discard(WorkBudget &budget) {
	base_ = {};
	target_ = {};
	if (!storage_)
		return true;
	if (!storage_->retiring) {
		storage_->retiring = true;
		storage_->mutations.clear();
		storage_->touched.clear();
		storage_->transactions.clear();
		storage_->overrides.clear();
		storage_->indexes.clear();
	}
	return retire(*storage_, budget);
}

struct DeltaDecoder::Impl {
	enum class Phase {
		Root,
		Version,
		Tag,
		ProfileTuple,
		ProfileId,
		Capabilities,
		Base,
		Target,
		Counts,
		TransactionCount,
		MutationCount,
		OverrideCount,
		StringCount,
		Transactions,
		TransactionTuple,
		Revision,
		Touched,
		NamespaceTuple,
		NamespaceName,
		NamespaceRevision,
		Mutations,
		MutationTuple,
		NamespaceIndex,
		Key,
		MutationTag,
		Delete,
		Definition,
		End
	};
	struct CursorParser {
		Cursor value;
		unsigned step = 0;
		bool consume(const m::WireToken &token) {
			switch (step++) {
			case 0:
				array(token, 5);
				break;
			case 1:
				identity(token, value.cluster);
				break;
			case 2:
				identity(token, value.group);
				break;
			case 3:
				identity(token, value.restore_epoch);
				break;
			case 4:
				identity(token, value.scope);
				break;
			case 5:
				value.revision = number(token);
				return true;
			default:
				throw std::logic_error("cursor parser already complete");
			}
			return false;
		}
	};
	Cursor expected;
	std::vector<std::string> scope;
	std::size_t scope_retirement_credit = 0;
	Limits limits;
	m::WireDecoder decoder;
	ValidatedDelta delta;
	CursorParser cursor;
	std::optional<DefinitionDecoder> definition;
	Mutation mutation;
	Transaction transaction;
	StableRows<std::uint32_t> &touched_indexes;
	std::string_view previous_key;
	std::uint32_t previous_namespace = 0;
	std::uint64_t declared_transactions = 0, declared_mutations = 0, declared_overrides = 0,
				  declared_strings = 0;
	std::uint64_t seen_mutations = 0, seen_overrides = 0, seen_strings = 0, touched_count = 0,
				  mutation_count = 0, mutated_namespaces = 0;
	Phase phase = Phase::Root;
	bool failed = false, taken = false;
	Impl(Cursor base, std::vector<std::string> names, Limits bounds, UpdateKind kind)
		: expected(base), scope(std::move(names)), limits(bounds),
		  decoder(
			  m::Limits{bounds.bytes, 200, 32,
						std::max({32u, bounds.records, bounds.namespaces, bounds.overrides_per_definition})}),
		  delta(bounds), touched_indexes(delta.storage_->indexes) {
		require(kind == UpdateKind::Delta || kind == UpdateKind::Progress, "invalid delta decoder kind");
		delta.kind_ = kind;
		require(expected.cluster != Identity{} && expected.group != Identity{} &&
					expected.restore_epoch != Identity{} && expected.scope != ScopeIdentity{},
				"unconfigured delta identity");
		require(scope.size() <= bounds.namespaces, "delta scope bound exceeded");
		for (std::size_t i = 0; i < scope.size(); ++i)
			require(!scope[i].empty() && scope[i].size() < 200 && is_valid_knob_name(scope[i] + ":x") &&
						(!i || scope[i - 1] < scope[i]),
					"invalid delta scope");
	}
	std::string_view text(m::WireToken &token) {
		require(token.kind == m::Kind::String && token.bytes.size() <= limits.string_bytes - seen_strings &&
					token.bytes.size() <= declared_strings - seen_strings,
				"delta string budget exceeded");
		seen_strings += token.bytes.size();
		return store_text(delta.storage_->arena, token.bytes).value;
	}
	bool contains_index(std::uint64_t index) const {
		std::uint64_t low = 0, high = touched_indexes.size();
		while (low < high) {
			auto middle = low + (high - low) / 2;
			if (touched_indexes[middle] < index)
				low = middle + 1;
			else
				high = middle;
		}
		return low < touched_indexes.size() && touched_indexes[low] == index;
	}
	void finish_mutation() {
		if (!transaction.mutations.count || previous_namespace != mutation.namespace_index)
			++mutated_namespaces;
		previous_namespace = mutation.namespace_index;
		previous_key = mutation.key;
		delta.storage_->mutations.push_back(std::move(mutation));
		++transaction.mutations.count;
		mutation = {};
		++seen_mutations;
		if (transaction.mutations.count == mutation_count) {
			require(mutated_namespaces == touched_count, "delta has untouched namespace watermark");
			delta.storage_->transactions.push_back(transaction);
			phase = delta.storage_->transactions.size() == declared_transactions ? Phase::End
																				 : Phase::TransactionTuple;
		} else
			phase = Phase::MutationTuple;
	}
	void consume(m::WireToken token) {
		switch (phase) {
		case Phase::Root:
			array(token, delta.kind_ == UpdateKind::Progress ? 5 : 7);
			phase = Phase::Version;
			break;
		case Phase::Version:
			require(number(token) == 1, "unsupported delta version");
			phase = Phase::Tag;
			break;
		case Phase::Tag:
			require(number(token) == static_cast<unsigned>(delta.kind_), "delta kind mismatch");
			phase = Phase::ProfileTuple;
			break;
		case Phase::ProfileTuple:
			array(token, 2);
			phase = Phase::ProfileId;
			break;
		case Phase::ProfileId:
			require(number(token) == 1, "unsupported delta semantic profile");
			phase = Phase::Capabilities;
			break;
		case Phase::Capabilities:
			delta.profile_.required_capabilities = number(token);
			require((delta.profile_.required_capabilities & ~1ull) == 0, "unsupported delta capabilities");
			phase = Phase::Base;
			break;
		case Phase::Base:
			if (cursor.consume(token)) {
				delta.base_ = cursor.value;
				require(delta.base_ == expected, "delta base cursor mismatch");
				cursor = {};
				phase = Phase::Target;
			}
			break;
		case Phase::Target:
			if (cursor.consume(token)) {
				delta.target_ = cursor.value;
				require(delta.target_.same_identity(expected) && delta.target_.revision > expected.revision,
						"delta target identity/revision mismatch");
				phase = delta.kind_ == UpdateKind::Progress ? Phase::End : Phase::Counts;
			}
			break;
		case Phase::Counts:
			array(token, 4);
			phase = Phase::TransactionCount;
			break;
		case Phase::TransactionCount:
			declared_transactions = number(token);
			require(declared_transactions && declared_transactions <= limits.records,
					"delta transaction bound exceeded");
			phase = Phase::MutationCount;
			break;
		case Phase::MutationCount:
			declared_mutations = number(token);
			require(declared_mutations >= declared_transactions && declared_mutations <= limits.records,
					"delta mutation bound exceeded");
			phase = Phase::OverrideCount;
			break;
		case Phase::OverrideCount:
			declared_overrides = number(token);
			require(declared_overrides <= declared_mutations * limits.overrides_per_definition,
					"delta override bound exceeded");
			phase = Phase::StringCount;
			break;
		case Phase::StringCount:
			declared_strings = number(token);
			require(declared_strings <= limits.string_bytes, "delta string bound exceeded");
			phase = Phase::Transactions;
			break;
		case Phase::Transactions:
			array(token, declared_transactions);
			phase = Phase::TransactionTuple;
			break;
		case Phase::TransactionTuple:
			array(token, 3);
			phase = Phase::Revision;
			break;
		case Phase::Revision: {
			auto revision = number(token);
			require(revision > (delta.storage_->transactions.empty()
									? expected.revision
									: delta.storage_->transactions.back().revision) &&
						revision <= delta.target_.revision,
					"unordered delta transaction revision");
			transaction = {
				revision, {delta.storage_->touched.size(), 0}, {delta.storage_->mutations.size(), 0}};
			mutated_namespaces = 0;
			phase = Phase::Touched;
			break;
		}
		case Phase::Touched:
			require(token.kind == m::Kind::Array && token.number && token.number <= scope.size(),
					"delta touched namespace bound invalid");
			touched_count = token.number;
			touched_indexes.clear();
			phase = Phase::NamespaceTuple;
			break;
		case Phase::NamespaceTuple:
			array(token, 2);
			phase = Phase::NamespaceName;
			break;
		case Phase::NamespaceName: {
			auto name = text(token);
			auto found = std::lower_bound(scope.begin(), scope.end(), name);
			require(found != scope.end() && *found == name, "delta touches namespace outside scope");
			auto index = static_cast<std::uint32_t>(found - scope.begin());
			require(touched_indexes.empty() || touched_indexes.back() < index,
					"unordered delta touched namespace");
			touched_indexes.push_back(index);
			require(delta.storage_->touched.size() < declared_mutations,
					"aggregate touched row limit exceeded");
			delta.storage_->touched.push_back({std::move(name), 0});
			++transaction.touched.count;
			phase = Phase::NamespaceRevision;
			break;
		}
		case Phase::NamespaceRevision:
			require(number(token) == transaction.revision, "delta touched revision mismatch");
			delta.storage_->touched.back().revision = transaction.revision;
			phase = touched_indexes.size() == touched_count ? Phase::Mutations : Phase::NamespaceTuple;
			break;
		case Phase::Mutations:
			require(token.kind == m::Kind::Array && token.number &&
						token.number <= declared_mutations - seen_mutations,
					"delta transaction mutation count invalid");
			mutation_count = token.number;
			phase = Phase::MutationTuple;
			break;
		case Phase::MutationTuple:
			array(token, 4);
			phase = Phase::NamespaceIndex;
			break;
		case Phase::NamespaceIndex: {
			auto index = number(token);
			require(index < scope.size() && contains_index(index), "delta mutation namespace not touched");
			mutation.namespace_index = static_cast<std::uint32_t>(index);
			phase = Phase::Key;
			break;
		}
		case Phase::Key:
			mutation.key = text(token);
			require(is_valid_knob_name(mutation.key) &&
						mutation.key.substr(0, mutation.key.find(':')) == scope[mutation.namespace_index],
					"invalid delta mutation key");
			require(!transaction.mutations.count || previous_namespace < mutation.namespace_index ||
						(previous_namespace == mutation.namespace_index && previous_key < mutation.key),
					"unordered or duplicate delta mutation");
			phase = Phase::MutationTag;
			break;
		case Phase::MutationTag: {
			auto tag = number(token);
			require(tag <= 1, "unknown delta mutation tag");
			phase = tag ? Phase::Delete : Phase::Definition;
			break;
		}
		case Phase::Delete:
			require(token.kind == m::Kind::Nil, "delete mutation requires nil");
			finish_mutation();
			break;
		case Phase::Definition:
			if (!definition)
				definition.emplace(DefinitionDecoder{limits,
													 declared_overrides,
													 seen_overrides,
													 declared_strings,
													 seen_strings,
													 {},
													 delta.storage_->arena,
													 delta.storage_->overrides});
			definition->consume(std::move(token));
			if (definition->value) {
				mutation.value = std::move(*definition->value);
				definition.reset();
				finish_mutation();
			}
			break;
		case Phase::End:
			throw std::invalid_argument("trailing delta object");
		}
	}
};
DeltaDecoder::DeltaDecoder(Cursor expected_base, std::vector<std::string> namespaces, Limits limits,
						   UpdateKind kind)
	: impl_(std::make_unique<Impl>(expected_base, std::move(namespaces), limits, kind)) {}
DeltaDecoder::~DeltaDecoder() = default;
DeltaDecoder::DeltaDecoder(DeltaDecoder &&) noexcept = default;
void DeltaDecoder::feed(std::span<const char> &input, WorkBudget &budget) {
	require(impl_ && !impl_->failed && !impl_->taken, "delta decoder unavailable");
	try {
		while (!input.empty() && budget.bytes && budget.tokens)
			if (auto token = impl_->decoder.next(input, budget.bytes)) {
				--budget.tokens;
				impl_->consume(std::move(*token));
			}
	} catch (...) {
		impl_->failed = true;
		throw;
	}
}
ValidatedDelta DeltaDecoder::finish() {
	require(impl_ && !impl_->failed && !impl_->taken, "delta decoder unavailable");
	try {
		impl_->decoder.finish();
		require(impl_->phase == Impl::Phase::End && impl_->seen_mutations == impl_->declared_mutations &&
					impl_->seen_overrides == impl_->declared_overrides &&
					impl_->seen_strings == impl_->declared_strings,
				"incomplete delta or aggregate totals mismatch");
		impl_->taken = true;
		return std::move(impl_->delta);
	} catch (...) {
		impl_->failed = true;
		throw;
	}
}
bool DeltaDecoder::discard(WorkBudget &budget) {
	if (!impl_)
		return true;
	impl_->failed = true;
	if (!impl_->delta.discard(budget))
		return false;
	while (!impl_->scope.empty()) {
		if (!charge(budget, impl_->scope.back().size() + sizeof(std::string), impl_->scope_retirement_credit))
			return false;
		impl_->scope.pop_back();
	}
	impl_.reset();
	return true;
}
} // namespace detent::distribution
