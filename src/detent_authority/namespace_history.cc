#include "namespace_history.hh"
#include "detent_distribution/messagepack.hh"
#include <algorithm>

namespace detent::authority {
namespace d = distribution;
namespace m = distribution::messagepack;
namespace {
void source(bool condition, const char *message) {
	if (!condition)
		throw d::HistoryFailure(message);
}
// Pulls whole tokens from one complete ring body.
class Tokens {
  public:
	Tokens(std::string_view bytes, const Limits &limits)
		: input_(bytes.data(), bytes.size()),
		  decoder_({bytes.size(), static_cast<std::uint32_t>(std::max<std::size_t>(limits.definition_bytes, 256)),
					32, static_cast<std::uint32_t>(std::max(limits.mutations, limits.overrides))}) {}
	m::Token next(m::Kind kind) {
		auto budget = input_.size();
		auto token = decoder_.next(input_, budget);
		source(token && token->kind == kind, "ring entry malformed");
		return std::move(*token);
	}
	std::optional<m::Token> next_or_nil(m::Kind kind) {
		auto budget = input_.size();
		auto token = decoder_.next(input_, budget);
		source(token && (token->kind == kind || token->kind == m::Kind::Nil ||
						 (kind == m::Kind::Signed && token->kind == m::Kind::Unsigned)),
			   "ring entry malformed");
		if (token->kind == m::Kind::Nil)
			return std::nullopt;
		return std::move(*token);
	}
	std::int64_t integer() {
		auto token = next_or_nil(m::Kind::Signed);
		source(token.has_value(), "ring entry malformed");
		return token->signed_value();
	}
	bool done() const noexcept { return input_.empty(); }

  private:
	std::span<const char> input_;
	m::Decoder decoder_;
};

class RecordsReader final : public d::HistoryReader {
  public:
	explicit RecordsReader(std::vector<d::HistoryRecord> records) : records_(std::move(records)) {}
	const d::HistoryRecord *next() override { return at_ < records_.size() ? &records_[at_++] : nullptr; }

  private:
	std::vector<d::HistoryRecord> records_;
	std::size_t at_ = 0;
};
// A tier's changes, each definition decoded when it is reached.
class ChangesReader final : public d::HistoryReader {
  public:
	explicit ChangesReader(history::Changes changes) : changes_(std::move(changes)) {}
	const d::HistoryRecord *next() override {
		if (at_ == changes_.size())
			return nullptr;
		const auto &change = changes_[at_++];
		if (change.definition)
			value_ = decode_definition(*change.definition);
		record_ = {change.key, change.revision, change.definition ? &value_ : nullptr};
		return &record_;
	}

  private:
	history::Changes changes_;
	std::size_t at_ = 0;
	KnobDef value_;
	d::HistoryRecord record_;
};
// The ring's entries after a position, each decoded when it is reached.
class RingReader final : public d::HistoryReader {
  public:
	RingReader(std::span<const std::shared_ptr<const CachedChange>> entries, const Limits &limits)
		: entries_(entries), limits_(limits) {}
	const d::HistoryRecord *next() override {
		while (index_ == records_.size()) {
			if (entry_ == entries_.size())
				return nullptr;
			records_ = ring_records(*entries_[entry_], limits_);
			position_ = entries_[entry_++]->position;
			index_ = 0;
		}
		const auto &[key, value] = records_[index_++];
		record_ = {key, position_, value ? &*value : nullptr};
		return &record_;
	}

  private:
	std::span<const std::shared_ptr<const CachedChange>> entries_;
	Limits limits_;
	std::vector<std::pair<std::string, std::optional<KnobDef>>> records_;
	std::size_t entry_ = 0, index_ = 0;
	std::uint64_t position_ = 0;
	d::HistoryRecord record_;
};
// The state at the seal: the namespace's live records, in key order.
class SealedReader final : public d::HistoryReader {
  public:
	SealedReader(const State &sealed, std::string_view name)
		: cursor_(sealed.namespace_records(name)), prefix_(std::string(name) + ':') {}
	const d::HistoryRecord *next() override {
		auto row = cursor_.next();
		if (!row || !row->first->starts_with(prefix_))
			return nullptr;
		record_ = {*row->first, row->second->revision, row->second->value.get()};
		return &record_;
	}

  private:
	decltype(std::declval<const State &>().namespace_records("")) cursor_;
	std::string prefix_;
	d::HistoryRecord record_;
};
} // namespace

std::vector<std::pair<std::string, std::optional<KnobDef>>> ring_records(const CachedChange &change,
																		 const Limits &limits) {
	source(change.body != nullptr, "ring entry has no body");
	Tokens tokens(change.body->view(), limits);
	source(tokens.next(m::Kind::Array).number == 4 && tokens.next(m::Kind::Unsigned).number == 1,
		   "ring entry malformed");
	source(tokens.next(m::Kind::Unsigned).number == change.revision, "ring entry revision mismatch");
	source(tokens.next(m::Kind::String).bytes == change.name_space(), "ring entry namespace mismatch");
	const auto count = tokens.next(m::Kind::Array).number;
	source(count == change.mutations, "ring entry mutation count mismatch");
	std::vector<std::pair<std::string, std::optional<KnobDef>>> out;
	out.reserve(count);
	for (std::uint64_t i = 0; i < count; ++i) {
		source(tokens.next(m::Kind::Array).number == 3, "ring entry malformed");
		auto key = std::move(tokens.next(m::Kind::String).bytes);
		const auto op = tokens.next(m::Kind::Unsigned).number;
		auto head = tokens.next_or_nil(m::Kind::Array);
		source((op == 1) == !head && op <= 1, "ring entry malformed");
		std::optional<KnobDef> value;
		if (head) {
			source(head->number == 4, "ring entry malformed");
			value.emplace();
			value->default_value = tokens.integer();
			if (auto pct = tokens.next_or_nil(m::Kind::Unsigned)) {
				source(pct->number <= 100, "ring entry malformed");
				value->rollout_pct = static_cast<std::uint8_t>(pct->number);
			}
			if (auto rollout = tokens.next_or_nil(m::Kind::Signed))
				value->rollout_value = rollout->signed_value();
			const auto rows = tokens.next(m::Kind::Array).number;
			for (std::uint64_t r = 0; r < rows; ++r) {
				source(tokens.next(m::Kind::Array).number == 3, "ring entry malformed");
				auto group = std::move(tokens.next(m::Kind::String).bytes);
				const auto forced = tokens.next(m::Kind::Unsigned).number;
				source(forced <= 1, "ring entry malformed");
				GroupOverride row;
				if (forced == 0)
					row = tokens.integer();
				else {
					const auto pct = tokens.next(m::Kind::Unsigned).number;
					source(pct <= 100, "ring entry malformed");
					row = GroupRollout{static_cast<std::uint8_t>(pct)};
				}
				value->overrides.emplace(std::move(group), row);
			}
		}
		out.emplace_back(std::move(key), std::move(value));
	}
	source(tokens.done(), "ring entry has trailing bytes");
	std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	for (std::size_t i = 1; i < out.size(); ++i)
		source(out[i - 1].first != out[i].first, "ring entry repeats a key");
	return out;
}

std::unique_ptr<AuthorityHistory> AuthorityHistory::freeze(std::string_view name, const Identity &cluster,
														   const State &state, const State &sealed,
														   const history::Table &table, history::Files &files,
														   const ChangeHistory &ring) {
	if (!state.positions_enabled() || !sealed.positions_enabled() || table.index != sealed.applied_index() ||
		!table.index)
		return nullptr;
	const auto *entry = state.namespace_entry(name);
	if (!entry || !entry->live)
		return nullptr;
	std::unique_ptr<AuthorityHistory> out(new AuthorityHistory(std::string(name), sealed, files));
	out->limits_ = state.limits();
	const auto *n = table.find(name);
	if (n && n->incarnation == entry->incarnation) {
		const auto *at_seal = sealed.namespace_entry(name);
		source(at_seal && at_seal->live && at_seal->incarnation == n->incarnation && at_seal->position == n->position,
			   "the layout and the sealed state disagree");
		out->layout_ = *n;
		out->cut_ = n->base.last;
		out->ring_start_ = n->position;
		for (const auto *list : {&out->layout_->blocks, &out->layout_->level0})
			for (const auto &block : *list) {
				out->tiers_.push_back(&block);
				out->spans_.push_back({block.first, block.last, list == &out->layout_->blocks});
			}
	} else
		// This incarnation began after the latest seal: no tiers, an empty copy at its origin.
		out->cut_ = out->ring_start_ = entry->origin;
	auto entries = ring.entries(name, entry->incarnation, out->ring_start_);
	std::size_t held = 0;
	while (held < entries.size() && entries[held]->position == out->ring_start_ + held + 1)
		++held;
	entries.resize(held);
	out->ring_ = std::move(entries);
	out->head_ = {cluster, entry->incarnation, out->ring_start_ + held};
	source(out->head_.position <= entry->position, "the ring holds positions past the head");
	out->held_back_ = entry->position - out->head_.position;
	return out;
}

std::unique_ptr<d::HistoryReader> AuthorityHistory::read_span(std::size_t index) const {
	source(index < tiers_.size(), "namespace history span out of range");
	try {
		return std::make_unique<ChangesReader>(history::read_tier(*layout_, *tiers_[index], files_, limits_));
	} catch (const d::HistoryFailure &) {
		throw;
	} catch (const std::exception &error) {
		throw d::HistoryFailure(error.what());
	}
}

std::unique_ptr<d::HistoryReader> AuthorityHistory::read_ring(std::uint64_t after) const {
	source(after >= ring_start_ && after <= head_.position, "namespace history ring read out of range");
	return std::make_unique<RingReader>(
		std::span<const std::shared_ptr<const CachedChange>>(ring_).subspan(after - ring_start_), limits_);
}

std::unique_ptr<d::HistoryReader> AuthorityHistory::read_full_copy() const {
	if (!layout_)
		return std::make_unique<RecordsReader>(std::vector<d::HistoryRecord>{});
	return std::make_unique<SealedReader>(sealed_, name_);
}
} // namespace detent::authority
