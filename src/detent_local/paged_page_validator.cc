#include "paged_page_validator.hh"
#include "detent_local/read_counters.hh"
#include <algorithm>
#include <sha256.h>
#include <stdexcept>
#include <vector>

namespace detent::local::detail {
namespace p = paged;
namespace {
void require(bool value) {
	if (!value)
		throw std::invalid_argument("invalid immutable dense page");
}
} // namespace
struct PageValidator::Impl {
	struct Metric {
		std::size_t size = 0;
		std::uint64_t keys = 0;
		unsigned height = 0;
		bool external = false, sealed = false;
	};
	enum class CanonicalPhase { Seed, Choose, Result };
	struct Frame {
		p::NodeView node;
		std::size_t first = 0, child = 0, key_depth = 0, start = 0, scan = 0, chosen = SIZE_MAX;
		Metric result;
		CanonicalPhase canonical = CanonicalPhase::Seed;
	};
	enum class Phase { First, Content, Walk, Done };
	std::shared_ptr<const void> owner;
	std::span<const char> bytes;
	p::Reference reference;
	p::PageHeader header;
	p::Limits limits;
	SHA256 hash;
	std::array<Frame, 203> frames{};
	std::vector<Metric, distribution::StorageAllocator<Metric>> metrics;
	std::size_t depth = 0, offset = 0, position = p::page_header_bytes, nodes = 0, external = 0,
	            key_depth = 0, charge = 0;
	Phase phase = Phase::First;
	bool failed = false, compare = true, visit = true, charging = false;
	Impl(std::shared_ptr<distribution::StorageLedger> account, std::shared_ptr<const void> lifetime,
	     std::span<const char> source, p::Reference expected, p::Limits bounds,
	     bool compare_digests)
	    : owner(std::move(lifetime)), bytes(source), reference(expected), limits(bounds),
	      metrics(distribution::StorageAllocator<Metric>(account)), compare(compare_digests) {
		require(owner && reference != p::Reference{} && bytes.size() == reference.length);
		p::encode_reference(reference);
		header = p::decode_page_header(bytes, limits);
		require(header.length == reference.length && header.keys == reference.keys &&
		        header.height == reference.height && header.digest == reference.digest);
	}
	bool grant(std::size_t amount, ValidationBudget &budget) {
		if (!charging) {
			charge = amount;
			charging = true;
		}
		const auto count = std::min(charge, budget.bytes);
		charge -= count;
		budget.bytes -= count;
		if (charge || !budget.rows)
			return false;
		--budget.rows;
		charging = false;
		return true;
	}
	bool digest(std::span<const char> source, ValidationBudget &budget) {
		if (!compare)
			return true;
		const auto count = std::min(source.size() - offset, budget.bytes);
		if (count)
			hash.add(source.data() + offset, count);
		offset += count;
		budget.bytes -= count;
		return offset == source.size();
	}
	bool combine(Frame &frame, ValidationBudget &work) {
		constexpr auto budget = p::page_capacity - p::page_header_bytes;
		if (!frame.result.size)
			frame.result.size = frame.node.end - frame.start;
		for (;;) {
			if (frame.canonical == CanonicalPhase::Seed) {
				while (frame.first + frame.scan < metrics.size()) {
					if (!grant(sizeof(Metric), work))
						return false;
					auto &child = metrics[frame.first + frame.scan++];
					child.sealed = child.size > budget;
					frame.result.size += child.sealed ? p::child_bytes : child.size;
				}
				frame.scan = 0;
				frame.canonical = CanonicalPhase::Choose;
			}
			if (frame.canonical == CanonicalPhase::Choose) {
				if (frame.result.size > budget) {
					while (frame.first + frame.scan < metrics.size()) {
						if (!grant(sizeof(Metric), work))
							return false;
						const auto i = frame.first + frame.scan++;
						const auto &child = metrics[i];
						if (!child.sealed && child.size > p::child_bytes &&
						    (frame.chosen == SIZE_MAX ||
						     child.height < metrics[frame.chosen].height ||
						     (child.height == metrics[frame.chosen].height &&
						      child.size > metrics[frame.chosen].size)))
							frame.chosen = i;
					}
					frame.scan = 0;
					if (frame.chosen != SIZE_MAX) {
						auto &child = metrics[frame.chosen];
						frame.result.size -= child.size - p::child_bytes;
						child.sealed = true;
						frame.chosen = SIZE_MAX;
						continue;
					}
				}
				frame.canonical = CanonicalPhase::Result;
			}
			while (frame.first + frame.scan < metrics.size()) {
				if (!grant(sizeof(Metric), work))
					return false;
				const auto &child = metrics[frame.first + frame.scan++];
				require(child.external == child.sealed &&
				        child.keys <= header.keys - frame.result.keys);
				frame.result.keys += child.keys;
				frame.result.height =
				    std::max(frame.result.height, child.height + unsigned(child.sealed));
			}
			return true;
		}
	}
};
PageValidator::PageValidator(std::shared_ptr<distribution::StorageLedger> account,
                             std::shared_ptr<const void> owner, std::span<const char> bytes,
                             p::Reference reference, p::Limits limits, bool compare_digests)
    : impl_(distribution::make_storage_owner<Impl>(account, std::malloc, std::free, account,
	                                               std::move(owner), bytes, reference, limits,
	                                               compare_digests)) {}
PageValidator::~PageValidator() = default;
PageValidator::PageValidator(PageValidator &&) noexcept = default;
void PageValidator::step(ValidationBudget &budget) {
	if (!impl_ || impl_->failed)
		throw std::logic_error("inactive page validation");
	auto &s = *impl_;
	try {
		for (;;) {
			if (s.phase == Impl::Phase::Done)
				return;
			if (s.phase == Impl::Phase::First) {
				if (!s.digest(s.bytes.first(32), budget))
					return;
				s.offset = 0;
				s.phase = Impl::Phase::Content;
			}
			if (s.phase == Impl::Phase::Content) {
				const auto end = s.bytes.size() - p::placement_bytes * s.header.externals;
				if (!s.digest(s.bytes.subspan(64, end - 64), budget) || !budget.rows)
					return;
				--budget.rows;
				if (s.compare) {
					p::Digest digest;
					s.hash.getHash(reinterpret_cast<unsigned char *>(digest.data()));
					p::count_hash(p::HashKind::Page, 1, end - 32);
					DETENT_COUNT(bytes_hashed, end - 32);
					require(digest == s.reference.digest);
				}
				s.phase = Impl::Phase::Walk;
			}
			if (s.visit) {
				const auto node = p::decode_node(s.bytes, s.position, s.limits);
				if (!s.grant(node.end - s.position + sizeof(Impl::Metric), budget))
					return;
				require(++s.nodes <= s.header.nodes && s.key_depth <= 202 &&
				        node.prefix.size() <= 202 - s.key_depth);
				const auto start = s.position;
				s.position = node.end;
				if (node.kind == p::PageKind::Branch) {
					require(s.depth < s.frames.size() && s.key_depth + node.prefix.size() < 202);
					s.frames[s.depth++] = {node, s.metrics.size(), 0,
					                       s.key_depth + node.prefix.size() + 1, start};
				} else {
					s.metrics.push_back({node.end - start, 1, 0});
				}
				s.visit = false;
			}
			while (s.depth) {
				auto &frame = s.frames[s.depth - 1];
				if (frame.child == frame.node.labels.size()) {
					if (!s.combine(frame, budget))
						return;
					const auto metric = frame.result;
					s.metrics.resize(frame.first);
					s.metrics.push_back(metric);
					--s.depth;
					continue;
				}
				const auto slot = frame.node.slot(frame.child);
				if (slot & 0x8000) {
					if (!s.grant(p::child_bytes + sizeof(Impl::Metric), budget))
						return;
					require((slot & 0x7fff) == s.external && s.external < s.header.externals);
					const auto r = p::decode_external(s.bytes, s.external++, s.limits);
					s.metrics.push_back(
					    {r.length - p::page_header_bytes, r.keys, unsigned(r.height) - 1, true});
					++frame.child;
					continue;
				}
				require(slot == s.position);
				++frame.child;
				s.key_depth = frame.key_depth;
				s.visit = true;
				break;
			}
			if (s.visit)
				continue;
			require(s.metrics.size() == 1 && s.nodes == s.header.nodes &&
			        s.external == s.header.externals &&
			        s.position == s.bytes.size() - p::child_bytes * s.header.externals);
			const auto result = s.metrics.front();
			require(result.size == s.bytes.size() - p::page_header_bytes &&
			        result.keys == s.header.keys && result.height + 1 == s.header.height);
			s.phase = Impl::Phase::Done;
		}
	} catch (...) {
		s.failed = true;
		throw;
	}
}
bool PageValidator::done() const noexcept {
	return impl_ && !impl_->failed && impl_->phase == Impl::Phase::Done;
}
p::PageHeader PageValidator::header() const {
	if (!done())
		throw std::logic_error("page validation incomplete");
	return impl_->header;
}
bool PageValidator::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
} // namespace detent::local::detail
