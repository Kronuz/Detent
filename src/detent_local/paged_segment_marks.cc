#include "paged_segment_marks.hh"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace detent::local::detail {
namespace {
std::size_t table_size(std::size_t capacity) {
	if (!capacity || capacity > (std::numeric_limits<std::size_t>::max() >> 2))
		throw std::length_error("invalid paged mark capacity");
	std::size_t result = 2;
	while (result < capacity * 2)
		result *= 2;
	return result;
}
} // namespace
PagedSegmentMarks::PagedSegmentMarks(std::shared_ptr<distribution::StorageLedger> account,
									 DirectoryIdentity directory, StorageIdentity storage,
									 std::size_t capacity)
	: entries_(distribution::StorageAllocator<Entry>(account)), storage_(storage), directory_(directory),
	  capacity_(capacity) {
	entries_.resize(table_size(capacity));
}
bool PagedSegmentMarks::admit(ValidationBudget &budget) {
	if (!budget.rows)
		return false;
	if (!charge_)
		charge_ = sizeof(SegmentOwnership);
	const auto count = std::min(charge_, budget.bytes);
	charge_ -= count;
	budget.bytes -= count;
	if (charge_)
		return false;
	--budget.rows;
	return true;
}
std::size_t PagedSegmentMarks::hash(const StorageIdentity &identity) noexcept {
	std::uint64_t value = 14695981039346656037ull;
	for (unsigned char byte : identity) {
		value ^= byte;
		value *= 1099511628211ull;
	}
	return static_cast<std::size_t>(value);
}
void PagedSegmentMarks::begin(distribution::RetirementHandle<PagedDependencyCertificate> certificate) {
	if (sealed_ || failed_ || busy() || !certificate)
		throw std::logic_error("paged marking unavailable");
	if (certificate->storage_identity() != storage_ || certificate->directory_identity() != directory_)
		throw std::invalid_argument("paged mark storage mismatch");
	certificate_ = std::move(certificate);
	row_ = probes_ = 0;
	positioned_ = false;
}
bool PagedSegmentMarks::busy() const noexcept { return bool(certificate_); }
void PagedSegmentMarks::step(ValidationBudget &budget) {
	if (sealed_ || failed_)
		throw std::logic_error("paged marking unavailable");
	if (!certificate_)
		return;
	try {
		const auto rows = certificate_->dependencies();
		while (row_ < rows.size()) {
			const auto &row = rows[row_];
			if (!positioned_) {
				if (!admit(budget))
					return;
				slot_ = hash(row.segment) & (entries_.size() - 1);
				positioned_ = true;
				probes_ = 0;
			}
			if (!admit(budget))
				return;
			auto &entry = entries_[slot_];
			if (!entry.occupied) {
				if (size_ == capacity_)
					throw std::length_error("paged mark capacity exhausted");
				entry = {row, true};
				++size_;
			} else if (entry.ownership.segment == row.segment) {
				if (entry.ownership != row)
					throw std::invalid_argument("conflicting paged dependency ownership");
			} else {
				if (++probes_ == entries_.size())
					throw std::length_error("paged mark table exhausted");
				slot_ = (slot_ + 1) & (entries_.size() - 1);
				continue;
			}
			++row_;
			positioned_ = false;
		}
		certificate_.reset();
	} catch (...) {
		failed_ = true;
		throw;
	}
}
void PagedSegmentMarks::seal() {
	if (sealed_ || failed_ || busy())
		throw std::logic_error("incomplete paged marks");
	sealed_ = true;
}
void PagedSegmentMarks::query(StorageIdentity identity) {
	if (!sealed_ || failed_ || querying_)
		throw std::logic_error("paged mark query unavailable");
	sought_ = identity;
	positioned_ = false;
	probes_ = 0;
	querying_ = true;
}
std::optional<bool> PagedSegmentMarks::query_step(ValidationBudget &budget) {
	if (!sealed_ || failed_ || !querying_)
		throw std::logic_error("paged mark query unavailable");
	if (!positioned_) {
		if (!admit(budget))
			return {};
		slot_ = hash(sought_) & (entries_.size() - 1);
		positioned_ = true;
	}
	while (admit(budget)) {
		const auto &entry = entries_[slot_];
		if (!entry.occupied || entry.ownership.segment == sought_) {
			querying_ = false;
			return entry.occupied;
		}
		if (++probes_ == entries_.size()) {
			failed_ = true;
			throw std::logic_error("invalid paged marks");
		}
		slot_ = (slot_ + 1) & (entries_.size() - 1);
	}
	return {};
}
bool PagedSegmentMarks::retire_step() noexcept {
	certificate_.reset();
	for (unsigned count = 0; count < 16 && !entries_.empty(); ++count)
		entries_.pop_back();
	return entries_.empty();
}
} // namespace detent::local::detail
