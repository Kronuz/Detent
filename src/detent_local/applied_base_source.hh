#pragma once
#include "borrowed_snapshot_source.hh"
#include "detent_distribution/rows.hh"
namespace detent::local::detail {
// Sequential paged access supplies flat construction without decoding a snapshot.
class PagedFlatSource {
	struct Namespace {
		std::array<char, 200> name{};
		std::size_t length = 0;
		std::uint64_t revision = 0;
	};

  public:
	PagedFlatSource(RecoveredGeneration generation, std::shared_ptr<distribution::StorageLedger> heap)
		: heap_(std::move(heap)), generation_(std::move(generation)), namespace_cursor_(generation_, heap_),
		  records_(generation_, heap_), arena_(*heap_), namespaces_(arena_) {}
	const Descriptor &descriptor() const noexcept { return generation_.descriptor(); }
	DirectoryIdentity directory_identity() const noexcept { return generation_.directory_identity(); }
	std::uint32_t namespace_count() const { return namespace_cursor_.namespace_count(); }
	std::uint32_t record_count() const { return records_.records(); }
	bool prepare_namespace(std::uint64_t ordinal, ValidationBudget &budget) {
		if (ordinal >= namespace_count())
			throw std::out_of_range("paged flat namespace ordinal");
		while (namespaces_.size() <= ordinal) {
			if (!namespace_cursor_.next_namespace(budget))
				return false;
			const auto row = namespace_cursor_.name_space();
			Namespace result;
			if (row.name.size() > result.name.size())
				throw std::length_error("paged flat namespace length");
			result.length = row.name.size();
			result.revision = row.revision;
			std::copy(row.name.begin(), row.name.end(), result.name.begin());
			namespaces_.push_back(result);
		}
		return true;
	}
	SourceNamespace name_space(std::uint64_t ordinal) const {
		const auto &row = namespaces_[ordinal];
		return {{row.name.data(), row.length}, row.revision};
	}
	bool prepare_record(std::uint64_t ordinal, ValidationBudget &budget) {
		if (ordinal >= record_count())
			throw std::out_of_range("paged flat record ordinal");
		if (current_ == ordinal)
			return true;
		if (ordinal < next_) {
			records_.reset();
			next_ = 0;
		}
		while (next_ <= ordinal) {
			if (!records_.prepare(next_, budget))
				return false;
			++next_;
		}
		const auto length = records_.key(ordinal, typed_key_);
		const auto key = paged::decode_key({typed_key_.data(), length});
		std::uint64_t first = 0, last = namespaces_.size();
		while (first < last) {
			const auto middle = first + (last - first) / 2;
			if (name_space(middle).name < key.name_space)
				first = middle + 1;
			else
				last = middle;
		}
		if (first == namespaces_.size() || name_space(first).name != key.name_space)
			throw std::logic_error("paged flat record namespace mismatch");
		const auto qualified = key.name_space.size() + 1 + key.local_name.size();
		if (qualified > key_.size())
			throw std::length_error("paged flat qualified key length");
		std::copy(key.name_space.begin(), key.name_space.end(), key_.begin());
		key_[key.name_space.size()] = ':';
		std::copy(key.local_name.begin(), key.local_name.end(), key_.begin() + key.name_space.size() + 1);
		const auto definition = records_.definition(ordinal);
		record_ = {static_cast<std::uint32_t>(first),
				   {key_.data(), qualified},
				   definition.revision,
				   IntDef{definition.default_int}};
		if (definition.boolean)
			record_.definition =
				SourceBoolean{definition.default_bool, definition.rollout, 0, definition.overrides};
		current_ = ordinal;
		return true;
	}
	SourceRecord record(std::uint64_t ordinal) const {
		if (current_ != ordinal)
			throw std::logic_error("paged flat record not prepared");
		return record_;
	}
	SourceOverride override_value(std::uint64_t ordinal, std::uint64_t relative) const {
		if (current_ != ordinal)
			throw std::logic_error("paged flat override not prepared");
		const auto value = records_.override_value(ordinal, relative);
		if (value.percentage)
			return {value.key, GroupRollout{value.value}};
		return {value.key, bool(value.value)};
	}
	bool discard(ValidationBudget &budget) {
		while (arena_.front_capacity() && budget.rows) {
			if (!disposal_charge_)
				disposal_charge_ = arena_.front_capacity();
			const auto bytes = std::min(disposal_charge_, budget.bytes);
			disposal_charge_ -= bytes;
			budget.bytes -= bytes;
			if (disposal_charge_)
				return false;
			--budget.rows;
			arena_.release_one();
		}
		return !arena_.front_capacity();
	}

  private:
	std::shared_ptr<distribution::StorageLedger> heap_;
	RecoveredGeneration generation_;
	StateCursor namespace_cursor_;
	BorrowedSnapshotSource records_;
	distribution::PagedStorage arena_;
	distribution::StableRows<Namespace> namespaces_;
	std::array<char, 202> typed_key_{};
	std::array<char, 200> key_{};
	SourceRecord record_;
	std::uint64_t current_ = UINT64_MAX, next_ = 0;
	std::size_t disposal_charge_ = 0;
};
class AppliedBaseSource {
  public:
	AppliedBaseSource(RecoveredGeneration generation, std::shared_ptr<distribution::StorageLedger> heap) {
		if (StateCursor::logical_commitment(generation))
			paged_ = distribution::make_storage_owner<PagedFlatSource>(heap, std::malloc, std::free,
																	   std::move(generation), heap);
		else
			flat_.emplace(std::move(generation));
	}
	const Descriptor &descriptor() const noexcept {
		return flat_ ? flat_->descriptor() : paged_->descriptor();
	}
	DirectoryIdentity directory_identity() const noexcept {
		return flat_ ? flat_->directory_identity() : paged_->directory_identity();
	}
	std::uint32_t namespace_count() const {
		return flat_ ? flat_->namespace_count() : paged_->namespace_count();
	}
	std::uint32_t record_count() const { return flat_ ? flat_->record_count() : paged_->record_count(); }
	bool prepare_namespace(std::uint64_t ordinal, ValidationBudget &budget) {
		return flat_ || paged_->prepare_namespace(ordinal, budget);
	}
	bool prepare_record(std::uint64_t ordinal, ValidationBudget &budget) {
		return flat_ || paged_->prepare_record(ordinal, budget);
	}
	SourceNamespace name_space(std::uint64_t ordinal) const {
		return flat_ ? flat_->name_space(ordinal) : paged_->name_space(ordinal);
	}
	SourceRecord record(std::uint64_t ordinal) const {
		return flat_ ? flat_->record(ordinal) : paged_->record(ordinal);
	}
	SourceOverride override_value(std::uint64_t ordinal, std::uint64_t relative) const {
		if (paged_)
			return paged_->override_value(ordinal, relative);
		const auto flag = std::get<SourceBoolean>(flat_->record(ordinal).definition);
		if (relative >= flag.override_count)
			throw std::out_of_range("flat source override ordinal");
		return flat_->override_value(flag.override_start + relative);
	}
	bool discard(ValidationBudget &budget) { return flat_ || paged_->discard(budget); }

  private:
	std::optional<PinnedImageSource> flat_;
	distribution::StorageOwner<PagedFlatSource> paged_;
};
} // namespace detent::local::detail
