#include "paged_leaf_body.hh"
#include <algorithm>
namespace detent::local::detail {
namespace p = paged;
LeafBody::LeafBody(std::shared_ptr<distribution::StorageLedger> account, p::Definition definition,
				   const void *context, std::uint64_t ordinal, OverrideAt callback, std::size_t maximum)
	: account_(std::move(account)), definition_(definition), context_(context), override_(callback),
	  ordinal_(ordinal), maximum_(maximum), size_(p::definition_header_bytes) {
	std::array<char, p::definition_header_bytes> header{};
	p::encode_definition_header(header, definition_);
	if (!account_ || maximum_ < size_ || (definition_.overrides && (!context_ || !override_)))
		throw std::invalid_argument("invalid paged leaf body source");
}
LeafBody::LeafBody(std::shared_ptr<distribution::StorageLedger> account, std::uint64_t watermark)
	: account_(std::move(account)), maximum_(8), size_(8), namespace_(true) {
	if (!account_)
		throw std::invalid_argument("paged watermark account missing");
	definition_.revision = watermark;
}
bool LeafBody::grant(std::size_t bytes, ValidationBudget &budget) {
	if (!budget.rows)
		return false;
	if (!charge_)
		charge_ = bytes;
	auto n = std::min(charge_, budget.bytes);
	charge_ -= n;
	budget.bytes -= n;
	if (charge_)
		return false;
	--budget.rows;
	return true;
}
void LeafBody::step(ValidationBudget &budget) {
	if (failed_ || discarding_)
		throw std::logic_error("paged leaf body unavailable");
	try {
		while (phase_ != Phase::Done) {
			switch (phase_) {
			case Phase::Count:
				if (index_ < definition_.overrides) {
					const auto row = override_(context_, ordinal_, index_);
					if (row.key.empty() || row.key.size() > 128 || size_ > maximum_ ||
						row.key.size() + p::override_header_bytes > maximum_ - size_)
						throw std::length_error("paged leaf body limit");
					if (!grant(row.key.size() + p::override_header_bytes, budget))
						return;
					size_ += row.key.size() + p::override_header_bytes;
					++index_;
					break;
				}
				output_ = distribution::make_owned_buffer(account_, size_);
				phase_ = Phase::Zero;
				break;
			case Phase::Zero: {
				auto n = std::min(size_ - output_->size(), budget.bytes);
				output_->resize(output_->size() + n);
				budget.bytes -= n;
				if (output_->size() != size_)
					return;
				phase_ = Phase::Header;
				break;
			}
			case Phase::Header:
				if (!grant(namespace_ ? 8 : p::definition_header_bytes, budget))
					return;
				if (namespace_)
					write_integer(output_->mutable_bytes(), 0, definition_.revision, 8);
				else
					p::encode_definition_header(output_->mutable_bytes().first(p::definition_header_bytes),
												definition_);
				offset_ = namespace_ ? 8 : p::definition_header_bytes;
				index_ = 0;
				phase_ = Phase::Overrides;
				break;
			case Phase::Overrides:
				if (index_ < definition_.overrides) {
					const auto row = override_(context_, ordinal_, index_);
					if (row.key.empty() || row.key.size() > 128 || offset_ > size_ ||
						row.key.size() + p::override_header_bytes > size_ - offset_)
						throw std::length_error("paged leaf body source changed");
					const auto size = p::override_header_bytes + row.key.size();
					if (!grant(size, budget))
						return;
					if (!previous_.empty() &&
						!std::lexicographical_compare(previous_.begin(), previous_.end(), row.key.begin(),
													  row.key.end(),
													  [](unsigned char a, unsigned char b) { return a < b; }))
						throw std::invalid_argument("unordered paged leaf overrides");
					p::encode_override(output_->mutable_bytes().subspan(offset_, size), row);
					previous_ = {output_->data() + offset_ + p::override_header_bytes, row.key.size()};
					offset_ += size;
					++index_;
					break;
				}
				if (offset_ != size_)
					throw std::logic_error("paged leaf body length changed");
				output_->seal();
				phase_ = Phase::Done;
				break;
			default:
				break;
			}
		}
	} catch (...) {
		failed_ = true;
		throw;
	}
}
bool LeafBody::done() const noexcept { return !failed_ && !discarding_ && phase_ == Phase::Done; }
std::shared_ptr<const distribution::OwnedBuffer> LeafBody::bytes() const {
	if (!done())
		throw std::logic_error("unsealed paged leaf body");
	return output_;
}
bool LeafBody::discard(ValidationBudget &budget) {
	if (!discarding_) {
		discarding_ = true;
		charge_ = output_ ? output_->storage_capacity() : 0;
	}
	if (!output_)
		return true;
	if (!budget.rows)
		return false;
	auto n = std::min(charge_, budget.bytes);
	charge_ -= n;
	budget.bytes -= n;
	if (charge_)
		return false;
	--budget.rows;
	output_.reset();
	return true;
}
} // namespace detent::local::detail
