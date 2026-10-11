#include "paged_contextual_reuse.hh"
#include "paged_navigation.hh"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace detent::local::detail {
namespace p = paged;
namespace d = distribution;
ContextualReuse::ContextualReuse(d::RetirementHandle<LeasedPagedRoot> base, p::Reference reference,
								 p::PageHeader header, std::string_view prefix, const SegmentMapping &mapping)
	: base_(std::move(base)), reference_(reference), header_(header), prefix_size_(prefix.size()),
	  ownership_(mapping.ownership()), file_identity_(mapping.identity()) {
	if (prefix.size() > prefix_.size())
		throw std::logic_error("reuse context exceeds canonical key");
	std::copy(prefix.begin(), prefix.end(), prefix_.begin());
}
const p::Reference &ContextualReuse::reference() const {
	if (!base_)
		throw std::logic_error("moved contextual reuse");
	return reference_;
}
const p::PageHeader &ContextualReuse::header() const {
	reference();
	return header_;
}
std::string_view ContextualReuse::entering_prefix() const {
	reference();
	return {prefix_.data(), prefix_size_};
}
const SegmentOwnership &ContextualReuse::ownership() const {
	reference();
	return ownership_;
}
SegmentFileIdentity ContextualReuse::file_identity() const {
	reference();
	return file_identity_;
}
bool ContextualReuse::matches(const p::Reference &reference, std::string_view prefix,
							  const Descriptor &descriptor, DirectoryIdentity directory,
							  StorageIdentity storage) const {
	return base_ && reference == reference_ && prefix == entering_prefix() &&
		   descriptor == base_->descriptor() && directory == base_->directory_identity() &&
		   storage == base_->storage_identity();
}
struct ReuseNavigator::Impl {
	enum class Phase { Node, Child, Done };
	d::RetirementHandle<LeasedPagedRoot> base;
	p::Reference reference;
	p::PageHeader header;
	std::span<const char> bytes;
	std::array<char, 202> key{};
	std::size_t key_size = 0, cursor = 0, level = 0, wanted = 0, charge = 0, segment = SIZE_MAX;
	std::uint32_t prefix = 0;
	std::optional<ContextualReuse> result;
	std::optional<p::Reference> exact;
	Phase phase = Phase::Node;
	bool failed = false, consumed = false;
	Impl(d::RetirementHandle<LeasedPagedRoot> value, std::string_view target, std::size_t depth)
		: base(std::move(value)), wanted(depth) {
		if (!base || target.size() > key.size())
			throw std::invalid_argument("invalid reuse navigation");
		p::decode_key(target);
		std::copy(target.begin(), target.end(), key.begin());
		key_size = target.size();
		reference = base->root().tree;
		if (!base->root().pages)
			phase = Phase::Done;
	}
	Impl(d::RetirementHandle<LeasedPagedRoot> value, p::Reference expected, std::string_view entering)
		: base(std::move(value)), wanted(SIZE_MAX), exact(expected) {
		if (!base || entering.size() > key.size())
			throw std::invalid_argument("invalid reuse context");
		p::encode_reference(expected);
		std::copy(entering.begin(), entering.end(), key.begin());
		key_size = entering.size();
		reference = base->root().tree;
		if (!base->root().pages)
			phase = Phase::Done;
	}
	bool admit(std::size_t amount, ValidationBudget &budget) {
		if (!budget.rows)
			return false;
		if (!charge)
			charge = amount;
		auto n = std::min(charge, budget.bytes);
		charge -= n;
		budget.bytes -= n;
		if (charge)
			return false;
		--budget.rows;
		return true;
	}
};
ReuseNavigator::ReuseNavigator(std::shared_ptr<d::StorageLedger> account,
							   d::RetirementHandle<LeasedPagedRoot> root, std::string_view key,
							   std::size_t depth)
	: impl_(d::make_storage_owner<Impl>(account, std::malloc, std::free, std::move(root), key, depth)) {}
ReuseNavigator::~ReuseNavigator() = default;
ReuseNavigator::ReuseNavigator(std::shared_ptr<d::StorageLedger> account,
							   d::RetirementHandle<LeasedPagedRoot> root, p::Reference expected,
							   std::string_view entering)
	: impl_(d::make_storage_owner<Impl>(account, std::malloc, std::free, std::move(root), expected,
										entering)) {}
ReuseNavigator::ReuseNavigator(ReuseNavigator &&) noexcept = default;
void ReuseNavigator::step(ValidationBudget &budget) {
	if (!impl_ || impl_->failed || impl_->consumed)
		throw std::logic_error("reuse navigation unavailable");
	auto &s = *impl_;
	try {
		while (s.phase != Impl::Phase::Done) {
			if (s.phase == Impl::Phase::Node) {
				const auto &registry = *s.base->registry();
				const auto inputs = registry.inputs();
				if (s.segment == SIZE_MAX) {
					s.segment = find_segment(inputs, s.reference.segment);
					if (s.segment == inputs.size())
						throw std::logic_error("validated reuse dependency missing");
					s.bytes = checked_page_bytes(inputs[s.segment], s.reference);
					s.prefix = read_integer(s.bytes, 20, 4);
				}
				const auto prefix = s.prefix;
				if (!s.admit(p::page_header_bytes + prefix, budget))
					return;
				s.header = p::decode_page_header(s.bytes);
				if (s.exact && s.cursor == s.key_size) {
					if (s.reference == *s.exact)
						s.result.emplace(ContextualReuse(s.base, s.reference, s.header,
														 {s.key.data(), s.key_size},
														 *registry.mapping(s.segment)));
					s.phase = Impl::Phase::Done;
					continue;
				}
				if (prefix > s.key_size - s.cursor ||
					std::memcmp(s.bytes.data() + p::page_header_bytes, s.key.data() + s.cursor, prefix)) {
					s.phase = Impl::Phase::Done;
					continue;
				}
				const auto entering = s.cursor;
				s.cursor += prefix;
				if (s.level == s.wanted || s.header.kind != p::PageKind::Branch) {
					if (!s.exact && (s.header.kind == p::PageKind::Branch || s.cursor == s.key_size))
						s.result.emplace(ContextualReuse(s.base, s.reference, s.header,
														 {s.key.data(), entering},
														 *registry.mapping(s.segment)));
					s.phase = Impl::Phase::Done;
					continue;
				}
				s.phase = Impl::Phase::Child;
			}
			if (s.phase == Impl::Phase::Child) {
				if (!s.admit(p::child_bytes + 8, budget))
					return;
				if (s.cursor == s.key_size) {
					s.phase = Impl::Phase::Done;
					continue;
				}
				const auto label = static_cast<unsigned char>(s.key[s.cursor]);
				const auto begin = p::page_header_bytes + s.header.prefix;
				std::size_t low = 0, high = s.header.children;
				while (low < high) {
					auto middle = low + (high - low) / 2;
					if (static_cast<unsigned char>(s.bytes[begin + middle * p::child_bytes]) < label)
						low = middle + 1;
					else
						high = middle;
				}
				if (low == s.header.children ||
					static_cast<unsigned char>(s.bytes[begin + low * p::child_bytes]) != label) {
					s.phase = Impl::Phase::Done;
					continue;
				}
				s.reference =
					p::decode_child(s.bytes.subspan(begin + low * p::child_bytes, p::child_bytes)).reference;
				s.segment = SIZE_MAX;
				++s.cursor;
				++s.level;
				s.phase = Impl::Phase::Node;
			}
		}
	} catch (...) {
		s.failed = true;
		throw;
	}
}
bool ReuseNavigator::done() const noexcept {
	return impl_ && !impl_->failed && impl_->phase == Impl::Phase::Done;
}
std::optional<ContextualReuse> ReuseNavigator::take() {
	if (!done() || impl_->consumed)
		throw std::logic_error("reuse navigation incomplete");
	impl_->consumed = true;
	return std::move(impl_->result);
}
bool ReuseNavigator::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
} // namespace detent::local::detail
