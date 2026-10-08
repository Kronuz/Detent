#include "change_history.hh"
#include "wire_fields.hh"
#include <algorithm>
#include <array>
#include <vector>
namespace detent::authority {
namespace d = distribution;
namespace m = distribution::messagepack;
namespace {
class HistoryDraft final : public AppliedChangeDraft {
	using Scratch = m::InlineBytes<512>;
	const State *candidate_;
	const Command *command_;
	std::span<const unsigned char> changed_;
	std::shared_ptr<d::StorageLedger> account_;
	std::size_t maximum_, row_ = 0, offset_ = 0, extent_ = 0, charge_ = 0, disposal_charge_ = 0;
	std::size_t definition_bytes_ = 0;
	std::shared_ptr<d::OwnedBuffer> body_;
	std::shared_ptr<CachedChange> entry_;
	Scratch pending_;
	const BoolDef *boolean_ = nullptr;
	std::optional<decltype(BoolDef::overrides)::const_iterator> group_;
	unsigned phase_ = 0;
	bool emitting_ = false, complete_ = false, discarded_ = false;
	void bounded_extent(std::size_t bytes) {
		if (bytes > maximum_ - extent_)
			throw d::StoragePressure("change transaction byte limit");
		extent_ += bytes;
	}
	void header(Scratch &out) {
		m::array(out, 4);
		m::unsigned_integer(out, 1);
		m::unsigned_integer(out, entry_->revision);
		entry_->namespace_offset = out.size() + (command_->name_space.size() <= 31 ? 1 : 2);
		entry_->namespace_length = command_->name_space.size();
		m::string(out, command_->name_space);
		m::array(out, entry_->mutations);
	}
	void next() {
		if (phase_ == 0) {
			header(pending_);
			phase_ = 1;
			return;
		}
		if (group_) {
			if (*group_ != boolean_->overrides.end()) {
				const auto &[name, value] = **group_;
				detail::write_wire_override(pending_, name, value);
				++*group_;
				if (!emitting_) {
					definition_bytes_ += pending_.size();
					++entry_->overrides;
					entry_->strings += name.size();
				}
			} else {
				if (!emitting_)
					entry_->maximum_definition_bytes =
						std::max(entry_->maximum_definition_bytes, definition_bytes_);
				group_.reset();
				boolean_ = nullptr;
				++row_;
			}
			return;
		}
		if (row_ == changed_.size()) {
			phase_ = 2;
			return;
		}
		if (!changed_[row_]) {
			++row_;
			return;
		}
		const auto &mutation = command_->mutations[row_];
		m::array(pending_, 3);
		m::string(pending_, mutation.key);
		m::unsigned_integer(pending_, mutation.value ? 0 : 1);
		if (!emitting_)
			entry_->strings += mutation.key.size();
		if (!mutation.value) {
			pending_.push_back('\xc0');
			++row_;
			return;
		}
		const auto *record = candidate_->find(mutation.key);
		if (!record || record->revision != entry_->revision)
			throw std::logic_error("changed value revision mismatch");
		const auto prefix = pending_.size();
		detail::write_wire_definition_head(pending_, *record->value);
		if (!emitting_)
			definition_bytes_ = pending_.size() - prefix;
		if ((boolean_ = std::get_if<BoolDef>(record->value.get()))) {
			if (!emitting_)
				entry_->maximum_overrides =
					std::max<std::size_t>(entry_->maximum_overrides, boolean_->overrides.size());
			group_ = boolean_->overrides.begin();
		} else {
			if (!emitting_)
				entry_->maximum_definition_bytes =
					std::max(entry_->maximum_definition_bytes, definition_bytes_);
			++row_;
		}
	}

  public:
	const void *origin;
	HistoryDraft(const State &candidate, const Command &command, std::span<const unsigned char> changed,
				 std::shared_ptr<d::StorageLedger> account, std::size_t maximum, const void *owner)
		: candidate_(&candidate), command_(&command), changed_(changed), account_(std::move(account)),
		  maximum_(maximum), origin(owner) {
		if (changed.size() != command.mutations.size() || changed.size() > UINT32_MAX ||
			command.name_space.size() > 198)
			throw std::invalid_argument("invalid changed transaction shape");
		entry_ = std::allocate_shared<CachedChange>(d::StorageAllocator<CachedChange>(account_));
		entry_->revision = candidate.revision();
		entry_->strings = command.name_space.size();
		for (auto value : changed)
			if (value)
				++entry_->mutations;
		if (!entry_->mutations)
			throw std::invalid_argument("empty changed transaction");
	}
	bool step(CommandWorkBudget &budget) override {
		if (discarded_)
			throw std::logic_error("discarded change draft");
		while (!complete_) {
			if (!pending_.empty()) {
				if (!emitting_) {
					bounded_extent(pending_.size());
					pending_.clear();
				} else {
					auto n = std::min(pending_.size() - offset_, budget.bytes);
					if (!n)
						return false;
					body_->append({pending_.data() + offset_, n});
					offset_ += n;
					budget.bytes -= n;
					if (offset_ == pending_.size()) {
						pending_.clear();
						offset_ = 0;
					}
				}
				continue;
			}
			if (!budget.records)
				return false;
			if (!charge_)
				charge_ = 512;
			auto n = std::min(charge_, budget.bytes);
			charge_ -= n;
			budget.bytes -= n;
			if (charge_)
				return false;
			--budget.records;
			if (phase_ == 2) {
				if (!emitting_) {
					body_ = d::make_owned_buffer(account_, extent_);
					emitting_ = true;
					row_ = 0;
					phase_ = 0;
				} else {
					body_->seal();
					entry_->body = std::move(body_);
					complete_ = true;
					candidate_ = nullptr;
					command_ = nullptr;
					changed_ = {};
				}
			} else
				next();
		}
		return true;
	}
	bool discard(CommandWorkBudget &budget) noexcept override {
		discarded_ = true;
		group_.reset();
		boolean_ = nullptr;
		const auto capacity = body_ ? body_->storage_capacity()
									: (entry_ && entry_->body ? entry_->body->storage_capacity() : 0);
		if (capacity) {
			if (!budget.receipts)
				return false;
			if (!disposal_charge_)
				disposal_charge_ = capacity;
			auto n = std::min(disposal_charge_, budget.bytes);
			disposal_charge_ -= n;
			budget.bytes -= n;
			if (disposal_charge_)
				return false;
			--budget.receipts;
			body_.reset();
		}
		candidate_ = nullptr;
		command_ = nullptr;
		changed_ = {};
		entry_.reset();
		return true;
	}
	std::shared_ptr<const CachedChange> take() noexcept {
		if (!complete_ || discarded_)
			return {};
		return std::move(entry_);
	}
};
} // namespace
struct ChangeHistory::Impl {
	using Entry = std::shared_ptr<const CachedChange>;
	using Slots = std::vector<Entry, d::StorageAllocator<Entry>>;
	std::shared_ptr<d::StorageLedger> account;
	HistoryOptions options;
	Slots live, retired;
	std::array<Entry, 64> staged{};
	Context context{};
	std::uint64_t floor = 0, through = 0, batch_index = 0, batch_base = 0, batch_gap = 0;
	std::size_t head = 0, count = 0, bytes = 0, retired_head = 0, retired_count = 0, staged_count = 0,
				credit = 0;
	bool batch = false, clearing = false;
	Impl(std::shared_ptr<d::StorageLedger> ledger, HistoryOptions bounds)
		: account(std::move(ledger)), options(bounds), live(d::StorageAllocator<Entry>(account)),
		  retired(d::StorageAllocator<Entry>(account)) {
		if (options.transactions > 65536 || !options.transaction_bytes ||
			options.transaction_bytes > 16 * 1024 * 1024)
			throw std::invalid_argument("invalid change history limits");
		live.resize(options.transactions);
		retired.resize(options.transactions + 64);
	}
	bool retire(Entry &entry) noexcept {
		if (!entry)
			return true;
		if (retired_count == retired.size())
			return false;
		retired[(retired_head + retired_count++) % retired.size()] = std::move(entry);
		return true;
	}
	bool evict() noexcept {
		if (!count)
			return true;
		auto &entry = live[head];
		const auto capacity = entry->body->storage_capacity();
		const auto revision = entry->revision;
		if (!retire(entry))
			return false;
		bytes -= capacity;
		if (!clearing)
			floor = std::max(floor, revision);
		head = (head + 1) % live.size();
		--count;
		return true;
	}
	void abandon() noexcept {
		// Staged bodies remain owned until collect() has capacity to retire them.
		batch = false;
		batch_gap = 0;
	}
};
ChangeHistory::ChangeHistory(std::shared_ptr<d::StorageLedger> account, HistoryOptions options)
	: impl_(d::make_storage_owner<Impl>(account, std::malloc, std::free, account, options)) {}
ChangeHistory::~ChangeHistory() = default;
void ChangeHistory::begin_batch(const Context &context, std::uint64_t index, std::uint64_t base) noexcept {
	auto &p = *impl_;
	if (p.context != context || p.through != base)
		reset(context, base);
	if (p.batch || p.staged_count || p.clearing) {
		p.abandon();
		return;
	}
	p.batch = true;
	p.batch_index = index;
	p.batch_base = base;
	p.batch_gap = 0;
}
std::shared_ptr<AppliedChangeDraft> ChangeHistory::capture(const State &state, const Command &command,
														   std::span<const unsigned char> changed) {
	auto &p = *impl_;
	if (!p.batch || p.staged_count == p.staged.size() || !p.options.transactions ||
		!p.options.payload_capacity || state.context() != p.context)
		return {};
	return std::allocate_shared<HistoryDraft>(d::StorageAllocator<HistoryDraft>(p.account), state, command,
											  changed, p.account, p.options.transaction_bytes, &p);
}
void ChangeHistory::stage(std::shared_ptr<AppliedChangeDraft> draft) noexcept {
	auto &p = *impl_;
	auto typed = std::dynamic_pointer_cast<HistoryDraft>(draft);
	if (!typed || typed->origin != &p || !p.batch || p.staged_count == p.staged.size()) {
		p.batch_gap = p.through + 1;
		return;
	}
	auto entry = typed->take();
	if (!entry) {
		p.batch_gap = p.through + 1;
		return;
	}
	p.staged[p.staged_count++] = std::move(entry);
}
void ChangeHistory::gap(std::uint64_t revision) noexcept {
	impl_->batch_gap = std::max(impl_->batch_gap, revision);
}
void ChangeHistory::publish(const Context &context, std::uint64_t index, std::uint64_t revision) noexcept {
	auto &p = *impl_;
	if (!p.batch) {
		if (context != p.context || revision != p.through)
			reset(context, revision);
		return;
	}
	if (context != p.context || index != p.batch_index || revision < p.batch_base) {
		reset(context, revision);
		return;
	}
	if (p.batch_gap)
		p.floor = std::max(p.floor, p.batch_gap);
	// A suffix is usable only when every committed revision has a body.
	// Validate this independently of the recorder's optional failure reports.
	auto previous = p.batch_base;
	for (std::size_t i = 0; i < p.staged_count; ++i) {
		const auto &entry = p.staged[i];
		if (!entry || entry->revision <= previous || entry->revision > revision) {
			p.floor = revision;
			break;
		}
		if (entry->revision - previous != 1)
			p.floor = std::max(p.floor, entry->revision - 1);
		previous = entry->revision;
	}
	if (previous < revision)
		p.floor = revision;
	for (std::size_t i = 0; i < p.staged_count; ++i) {
		auto &entry = p.staged[i];
		if (!entry)
			continue;
		const auto capacity = entry->body->storage_capacity();
		if (entry->revision <= p.floor || capacity > p.options.payload_capacity) {
			p.floor = std::max(p.floor, entry->revision);
			p.retire(entry);
			continue;
		}
		while (p.count && (p.count == p.live.size() || capacity > p.options.payload_capacity - p.bytes))
			if (!p.evict())
				break;
		if (!p.live.size() || p.count == p.live.size() || capacity > p.options.payload_capacity - p.bytes) {
			p.floor = std::max(p.floor, entry->revision);
			p.retire(entry);
			continue;
		}
		p.live[(p.head + p.count++) % p.live.size()] = std::move(entry);
		p.bytes += capacity;
	}
	p.through = revision;
	p.batch = false;
	p.batch_gap = 0;
	std::size_t retained = 0;
	for (std::size_t i = 0; i < p.staged_count; ++i)
		if (p.staged[i]) {
			if (retained != i)
				p.staged[retained] = std::move(p.staged[i]);
			++retained;
		}
	p.staged_count = retained;
	// Outstanding staged retention is handled by the bounded cleanup lane.
}
void ChangeHistory::abandon_batch() noexcept { impl_->abandon(); }
void ChangeHistory::reset(const Context &context, std::uint64_t revision) noexcept {
	auto &p = *impl_;
	p.abandon();
	p.context = context;
	p.floor = p.through = revision;
	p.clearing = p.count || p.staged_count;
}
bool ChangeHistory::pending() const noexcept {
	const auto &p = *impl_;
	return p.retired_count || (!p.batch && p.staged_count) || p.clearing ||
		   (p.count && p.live[p.head]->revision <= p.floor);
}
void ChangeHistory::collect(CommandWorkBudget &budget) noexcept {
	auto &p = *impl_;
	while (budget.receipts) {
		if (!p.batch && p.staged_count) {
			auto &entry = p.staged[p.staged_count - 1];
			if (!entry || p.retire(entry)) {
				--p.staged_count;
				--budget.receipts;
				continue;
			}
		}
		if (p.count && (p.clearing || p.live[p.head]->revision <= p.floor) && p.evict()) {
			--budget.receipts;
			continue;
		}
		if (p.clearing && !p.count && !p.staged_count)
			p.clearing = false;
		if (!p.retired_count)
			return;
		auto &entry = p.retired[p.retired_head];
		if (!p.credit)
			p.credit = entry->body->storage_capacity();
		auto n = std::min(p.credit, budget.bytes);
		p.credit -= n;
		budget.bytes -= n;
		if (p.credit)
			return;
		--budget.receipts;
		entry.reset();
		p.retired_head = (p.retired_head + 1) % p.retired.size();
		--p.retired_count;
	}
}
bool ChangeHistory::covers(const Context &context, std::uint64_t base, std::uint64_t target) const noexcept {
	const auto &p = *impl_;
	return context == p.context && p.floor <= base && base <= target && target <= p.through;
}
std::shared_ptr<const CachedChange> ChangeHistory::find(std::uint64_t revision) const noexcept {
	const auto &p = *impl_;
	if (revision <= p.floor || revision > p.through)
		return {};
	std::size_t low = 0, high = p.count;
	while (low < high) {
		auto mid = low + (high - low) / 2;
		if (p.live[(p.head + mid) % p.live.size()]->revision < revision)
			low = mid + 1;
		else
			high = mid;
	}
	if (low == p.count)
		return {};
	const auto &entry = p.live[(p.head + low) % p.live.size()];
	return entry->revision == revision ? entry : Impl::Entry{};
}
std::size_t ChangeHistory::size() const noexcept { return impl_->count; }
std::size_t ChangeHistory::payload_capacity() const noexcept { return impl_->bytes; }
std::uint64_t ChangeHistory::coverage_floor() const noexcept { return impl_->floor; }
std::uint64_t ChangeHistory::through() const noexcept { return impl_->through; }
std::shared_ptr<d::StorageLedger> ChangeHistory::storage_account() const noexcept { return impl_->account; }
} // namespace detent::authority
