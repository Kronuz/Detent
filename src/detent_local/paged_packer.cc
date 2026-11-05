#include "paged_packer.hh"
#include <algorithm>
#include <sha256.h>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace detent::local::detail {
namespace p = paged;
namespace d = distribution;
namespace {
void require(bool value) {
	if (!value)
		throw std::invalid_argument("invalid dense packer input");
}
template <class T> using Vector = std::vector<T, d::StorageAllocator<T>>;
struct Cluster {
	Vector<char> nodes;
	Vector<p::Reference> external;
	std::array<char, 202> path{};
	std::size_t path_size = 0;
	std::uint64_t keys = 0;
	std::uint16_t count = 0;
	unsigned height = 0;
	p::Reference reference;
	explicit Cluster(const std::shared_ptr<d::StorageLedger> &account)
		: nodes(d::StorageAllocator<char>(account)),
		  external(d::StorageAllocator<p::Reference>(account)) {}
	bool sealed() const { return reference.length != 0; }
	std::size_t size() const { return nodes.size() + p::child_bytes * external.size(); }
	std::string_view entering_path() const { return {path.data(), path_size}; }
	void release() {
		Vector<char>(nodes.get_allocator()).swap(nodes);
		Vector<p::Reference>(external.get_allocator()).swap(external);
	}
};
void relocate_slots(std::span<char> bytes, std::size_t base, std::size_t external) {
	std::size_t at = 0;
	while (at < bytes.size()) {
		const auto prefix = static_cast<unsigned char>(bytes[at + 1]);
		if (bytes[at] == static_cast<char>(p::PageKind::Branch)) {
			const std::size_t count = static_cast<unsigned char>(bytes[at + 2]) + 1;
			const auto slots = at + 3 + prefix + count;
			for (std::size_t i = 0; i < count; ++i) {
				const auto old = read_integer(bytes, slots + 2 * i, 2);
				const auto slot =
					old & 0x8000 ? (0x8000 | ((old & 0x7fff) + external)) : old + base;
				require(slot <= UINT16_MAX && ((old & 0x8000) || slot < 0x8000));
				write_integer(bytes, slots + 2 * i, slot, 2);
			}
			at += 3 + prefix + 3 * count;
		} else {
			at += 4 + prefix + read_integer(bytes, at + 2, 2);
		}
	}
	require(at == bytes.size());
}
} // namespace
struct PagedPacker::Impl {
	enum class Phase {
		InjectBegin,
		InjectScan,
		InjectCopy,
		Reference,
		Idle,
		Input,
		Choose,
		Combine,
		Complete,
		Finish,
		Allocate,
		Zero,
		Nodes,
		Externals,
		Hash,
		Ready,
		Done
	};
	std::shared_ptr<d::StorageLedger> account;
	p::Limits limits;
	std::stop_token stop;
	Vector<d::StorageOwner<Cluster>> frontier;
	d::StorageOwner<Cluster> current;
	TrieEvent event{};
	std::span<const char> body;
	std::span<const char> injected_page;
	std::string_view injected_path;
	p::Reference injected_reference;
	struct InjectFrame {
		p::NodeView node;
		std::size_t child = 0;
	};
	std::array<InjectFrame, 203> injected_frames{};
	std::size_t injected_start = 0, injected_position = 0, injected_depth = 0,
				injected_first_external = SIZE_MAX;
	bool injected_visit = true;
	std::shared_ptr<d::OwnedBuffer> output;
	Cluster *sealing = nullptr;
	p::Reference summary, root;
	SHA256 hash;
	Phase phase = Phase::Idle, after_seal = Phase::Idle;
	std::size_t charge = 0, total = 0, offset = 0, external = 0;
	std::uint64_t input_keys = 0, emitted_pages = 0, emitted_bytes = 0;
	bool failed = false, discarding = false;
	Impl(std::shared_ptr<d::StorageLedger> ledger, p::Limits bounds, std::stop_token token)
		: account(std::move(ledger)), limits(bounds), stop(token),
		  frontier(d::StorageAllocator<d::StorageOwner<Cluster>>(account)),
		  current(nullptr, d::StorageDelete<Cluster>{account}) {}
	void active() const {
		if (failed || discarding)
			throw std::logic_error("inactive dense packer");
		if (stop.stop_requested())
			throw std::system_error(std::make_error_code(std::errc::operation_canceled));
	}
	bool grant(std::size_t amount, ValidationBudget &budget) {
		if (!budget.rows)
			return false;
		if (!charge)
			charge = amount;
		const auto used = std::min(charge, budget.bytes);
		charge -= used;
		budget.bytes -= used;
		if (charge)
			return false;
		--budget.rows;
		return true;
	}
	void seal(Cluster &cluster, Phase after) {
		require(!cluster.sealed() && cluster.height < 255 && cluster.count && cluster.keys &&
				cluster.size() + p::page_header_bytes <= p::page_bytes);
		sealing = &cluster;
		after_seal = after;
		phase = Phase::Allocate;
	}
	void path(Cluster &cluster) {
		require(event.prefix.size() <= event.key.size() && event.key.size() <= 202);
		const auto path = event.key.substr(0, event.key.size() - event.prefix.size());
		cluster.path_size = path.size();
		std::copy(path.begin(), path.end(), cluster.path.begin());
	}
	void combine() {
		current = d::make_storage_owner<Cluster>(account, std::malloc, std::free, account);
		auto &cluster = *current;
		path(cluster);
		cluster.keys = event.keys;
		if (event.kind != p::PageKind::Branch) {
			cluster.nodes.resize(total);
			cluster.nodes[0] = static_cast<char>(event.kind);
			cluster.nodes[1] = static_cast<char>(event.prefix.size());
			write_integer(cluster.nodes, 2, body.size(), 2);
			std::copy(event.prefix.begin(), event.prefix.end(), cluster.nodes.begin() + 4);
			std::copy(body.begin(), body.end(), cluster.nodes.begin() + 4 + event.prefix.size());
			cluster.count = 1;
			return;
		}
		const auto count = event.children.size();
		const auto first = frontier.size() - count;
		std::size_t node_size = 3 + event.prefix.size() + 3 * count, externals = 0;
		for (std::size_t i = first; i < frontier.size(); ++i) {
			const auto &child = *frontier[i];
			node_size += child.sealed() ? 0 : child.nodes.size();
			externals += child.sealed() ? 1 : child.external.size();
		}
		cluster.nodes.resize(node_size);
		cluster.external.reserve(externals);
		cluster.nodes[0] = static_cast<char>(p::PageKind::Branch);
		cluster.nodes[1] = static_cast<char>(event.prefix.size());
		cluster.nodes[2] = static_cast<char>(count - 1);
		std::copy(event.prefix.begin(), event.prefix.end(), cluster.nodes.begin() + 3);
		std::copy(event.children.begin(), event.children.end(),
				  cluster.nodes.begin() + 3 + event.prefix.size());
		const auto slots = 3 + event.prefix.size() + count;
		std::size_t at = slots + 2 * count;
		cluster.count = 1;
		for (std::size_t j = 0; j < count; ++j) {
			auto &child = *frontier[first + j];
			if (child.sealed()) {
				write_integer(cluster.nodes, slots + 2 * j, 0x8000 | cluster.external.size(), 2);
				cluster.external.push_back(child.reference);
				cluster.height = std::max(cluster.height, unsigned(child.reference.height));
			} else {
				require(at + p::page_header_bytes < 0x8000);
				write_integer(cluster.nodes, slots + 2 * j, at, 2);
				std::copy(child.nodes.begin(), child.nodes.end(), cluster.nodes.begin() + at);
				relocate_slots(std::span<char>(cluster.nodes).subspan(at, child.nodes.size()), at,
							   cluster.external.size());
				at += child.nodes.size();
				cluster.external.insert(cluster.external.end(), child.external.begin(),
										child.external.end());
				cluster.count += child.count;
				cluster.height = std::max(cluster.height, child.height);
			}
		}
		require(at == node_size && cluster.size() == total);
		frontier.resize(first);
	}
};
PagedPacker::PagedPacker(std::shared_ptr<d::StorageLedger> account, p::Limits limits,
						 std::stop_token stop)
	: impl_(d::make_storage_owner<Impl>(account, std::malloc, std::free, account, limits, stop)) {}
PagedPacker::~PagedPacker() = default;
PagedPacker::PagedPacker(PagedPacker &&) noexcept = default;
void PagedPacker::push(TrieEvent event, std::span<const char> body) {
	auto &s = *impl_;
	s.active();
	if (s.phase != Impl::Phase::Idle)
		throw std::logic_error("dense packer event still pending");
	s.event = event;
	s.body = body;
	s.phase = Impl::Phase::Input;
}
void PagedPacker::push_cluster(std::span<const char> page, std::size_t node_offset,
							   std::string_view path) {
	auto &s = *impl_;
	s.active();
	if (s.phase != Impl::Phase::Idle)
		throw std::logic_error("dense packer event still pending");
	s.injected_page = page;
	s.injected_path = path;
	s.injected_start = node_offset;
	s.phase = Impl::Phase::InjectBegin;
}
void PagedPacker::push_reference(const p::Reference &reference, std::string_view path) {
	auto &s = *impl_;
	s.active();
	if (s.phase != Impl::Phase::Idle)
		throw std::logic_error("dense packer event still pending");
	s.injected_reference = reference;
	s.injected_path = path;
	s.phase = Impl::Phase::Reference;
}
void PagedPacker::finish() {
	auto &s = *impl_;
	s.active();
	if (s.phase != Impl::Phase::Idle)
		throw std::logic_error("dense packer event still pending");
	s.phase = Impl::Phase::Finish;
}
void PagedPacker::step(ValidationBudget &budget) {
	auto &s = *impl_;
	try {
		s.active();
		for (;;) {
			switch (s.phase) {
			case Impl::Phase::Reference:
				if (!s.grant(sizeof(Cluster) + s.injected_path.size(), budget))
					return;
				p::encode_reference(s.injected_reference);
				require(s.injected_reference.length > p::page_capacity &&
						s.injected_path.size() <= 202 &&
						s.injected_reference.keys <=
							std::uint64_t(s.limits.records) + s.limits.namespaces - s.input_keys);
				s.current =
					d::make_storage_owner<Cluster>(s.account, std::malloc, std::free, s.account);
				s.current->reference = s.injected_reference;
				s.current->keys = s.injected_reference.keys;
				s.current->path_size = s.injected_path.size();
				std::copy(s.injected_path.begin(), s.injected_path.end(), s.current->path.begin());
				s.input_keys += s.current->keys;
				s.phase = Impl::Phase::Complete;
				break;
			case Impl::Phase::InjectBegin:
				if (!s.grant(sizeof(Cluster) + s.injected_path.size(), budget))
					return;
				p::decode_page_header(s.injected_page, s.limits);
				require(s.injected_path.size() <= 202 && s.injected_start >= p::page_header_bytes);
				s.current =
					d::make_storage_owner<Cluster>(s.account, std::malloc, std::free, s.account);
				s.current->path_size = s.injected_path.size();
				std::copy(s.injected_path.begin(), s.injected_path.end(), s.current->path.begin());
				s.injected_position = s.injected_start;
				s.injected_depth = 0;
				s.injected_first_external = SIZE_MAX;
				s.injected_visit = true;
				s.phase = Impl::Phase::InjectScan;
				break;
			case Impl::Phase::InjectScan: {
				auto &cluster = *s.current;
				if (s.injected_visit) {
					const auto node =
						p::decode_node(s.injected_page, s.injected_position, s.limits);
					if (!s.grant(node.end - s.injected_position + sizeof(Impl::InjectFrame),
								 budget))
						return;
					require(cluster.count < UINT16_MAX);
					++cluster.count;
					s.injected_position = node.end;
					if (node.kind == p::PageKind::Branch) {
						require(s.injected_depth < s.injected_frames.size());
						s.injected_frames[s.injected_depth++] = {node, 0};
					} else {
						require(cluster.keys <
								std::uint64_t(s.limits.records) + s.limits.namespaces);
						++cluster.keys;
					}
					s.injected_visit = false;
				}
				while (s.injected_depth) {
					auto &frame = s.injected_frames[s.injected_depth - 1];
					if (frame.child == frame.node.labels.size()) {
						--s.injected_depth;
						continue;
					}
					const auto slot = frame.node.slot(frame.child);
					if (slot & 0x8000) {
						if (!s.grant(p::child_bytes, budget))
							return;
						const auto index = slot & 0x7fff;
						if (s.injected_first_external == SIZE_MAX)
							s.injected_first_external = index;
						require(index == s.injected_first_external + cluster.external.size());
						const auto reference = p::decode_external(s.injected_page, index, s.limits);
						require(reference.keys <= std::uint64_t(s.limits.records) +
													  s.limits.namespaces - cluster.keys &&
								reference.height < 255);
						cluster.keys += reference.keys;
						cluster.height = std::max(cluster.height, unsigned(reference.height));
						cluster.external.push_back(reference);
						++frame.child;
					} else {
						require(slot == s.injected_position);
						++frame.child;
						s.injected_visit = true;
						break;
					}
				}
				if (!s.injected_visit)
					s.phase = Impl::Phase::InjectCopy;
				break;
			}
			case Impl::Phase::InjectCopy: {
				auto &cluster = *s.current;
				const auto size = s.injected_position - s.injected_start;
				require(size + cluster.external.size() * p::child_bytes <=
							p::page_capacity - p::page_header_bytes &&
						cluster.keys <=
							std::uint64_t(s.limits.records) + s.limits.namespaces - s.input_keys);
				if (!s.grant(2 * size, budget))
					return;
				cluster.nodes.assign(s.injected_page.begin() + s.injected_start,
									 s.injected_page.begin() + s.injected_position);
				std::size_t at = 0;
				while (at < cluster.nodes.size()) {
					const auto prefix = static_cast<unsigned char>(cluster.nodes[at + 1]);
					if (cluster.nodes[at] == static_cast<char>(p::PageKind::Branch)) {
						const std::size_t count =
							static_cast<unsigned char>(cluster.nodes[at + 2]) + 1;
						const auto slots = at + 3 + prefix + count;
						for (std::size_t j = 0; j < count; ++j) {
							const auto old = read_integer(cluster.nodes, slots + 2 * j, 2);
							const auto rebased =
								old & 0x8000
									? (0x8000 | ((old & 0x7fff) - s.injected_first_external))
									: old - s.injected_start;
							write_integer(cluster.nodes, slots + 2 * j, rebased, 2);
						}
						at += 3 + prefix + 3 * count;
					} else
						at += 4 + prefix + read_integer(cluster.nodes, at + 2, 2);
				}
				require(at == cluster.nodes.size());
				s.input_keys += cluster.keys;
				s.phase = Impl::Phase::Complete;
				break;
			}

			case Impl::Phase::Idle:
			case Impl::Phase::Ready:
			case Impl::Phase::Done:
				return;
			case Impl::Phase::Input: {
				if (!s.grant(sizeof(Cluster) + s.event.key.size() + s.event.children.size(),
							 budget))
					return;
				const auto &e = s.event;
				require(e.prefix.size() <= 202 && e.prefix.size() <= s.limits.prefix_bytes && e.key.size() <= 202 &&
						e.prefix.size() <= e.key.size() && e.key.ends_with(e.prefix) && e.keys &&
						e.keys <= std::uint64_t(s.limits.records) + s.limits.namespaces);
				if (e.kind == p::PageKind::Branch) {
					require(s.body.empty() && e.children.size() >= 2 && e.children.size() <= 256 &&
							e.children.size() <= s.frontier.size());
					s.total = 3 + e.prefix.size() + 3 * e.children.size();
					std::uint64_t keys = 0;
					int previous = -1;
					const auto first = s.frontier.size() - e.children.size();
					for (std::size_t i = 0; i < e.children.size(); ++i) {
						const auto label = e.children[i];
						const auto &child = *s.frontier[first + i];
						const auto path = child.entering_path();
						require(label > previous && path.size() == e.key.size() + 1 &&
								path.starts_with(e.key) &&
								static_cast<unsigned char>(path.back()) == label &&
								child.keys <= e.keys - keys);
						keys += child.keys;
						previous = label;
						s.total += child.sealed() ? p::child_bytes : child.size();
					}
					require(keys == e.keys);
					s.phase = Impl::Phase::Choose;
				} else {
					require(
						(e.kind == p::PageKind::Definition || e.kind == p::PageKind::Namespace) &&
						e.children.empty() && e.keys == 1 && !s.body.empty() &&
						(e.kind != p::PageKind::Namespace || s.body.size() == 8) &&
						s.body.size() <= p::maximum_leaf_body_bytes - e.prefix.size() &&
						s.input_keys < std::uint64_t(s.limits.records) + s.limits.namespaces);
					++s.input_keys;
					s.total = 4 + e.prefix.size() + s.body.size();
					s.phase = Impl::Phase::Combine;
				}
				break;
			}
			case Impl::Phase::Choose: {
				if (!s.grant(s.event.children.size() * sizeof(Cluster), budget))
					return;
				Cluster *best = nullptr;
				if (s.total > p::page_capacity - p::page_header_bytes) {
					const auto first = s.frontier.size() - s.event.children.size();
					for (std::size_t i = first; i < s.frontier.size(); ++i) {
						auto &candidate = *s.frontier[i];
						if (candidate.sealed() || candidate.size() <= p::child_bytes)
							continue;
						if (!best || candidate.height < best->height ||
							(candidate.height == best->height && candidate.size() > best->size()))
							best = &candidate;
					}
				}
				if (best) {
					s.total -= best->size() - p::child_bytes;
					s.seal(*best, Impl::Phase::Choose);
				} else {
					s.phase = Impl::Phase::Combine;
				}
				break;
			}
			case Impl::Phase::Combine:
				if (!s.grant(3 * s.total + sizeof(Cluster), budget))
					return;
				s.combine();
				if (s.total > p::page_capacity - p::page_header_bytes)
					s.seal(*s.current, Impl::Phase::Complete);
				else
					s.phase = Impl::Phase::Complete;
				break;
			case Impl::Phase::Complete:
				if (!s.grant(sizeof(Cluster), budget))
					return;
				require(s.frontier.size() < 202 * 255 + 1);
				s.frontier.push_back(std::move(s.current));
				s.event = {};
				s.body = {};
				s.injected_page = {};
				s.injected_path = {};
				s.phase = Impl::Phase::Idle;
				return;
			case Impl::Phase::Finish:
				if (!s.grant(sizeof(Cluster), budget))
					return;
				require(s.frontier.size() <= 1);
				if (s.frontier.empty()) {
					require(!s.input_keys);
					s.phase = Impl::Phase::Done;
				} else {
					auto &root = *s.frontier.back();
					require(root.path_size == 0 && root.keys == s.input_keys);
					if (root.sealed()) {
						s.root = root.reference;
						s.phase = Impl::Phase::Done;
					} else {
						s.seal(root, Impl::Phase::Finish);
					}
				}
				break;
			case Impl::Phase::Allocate: {
				auto &cluster = *s.sealing;
				const auto length = p::page_header_bytes + cluster.size();
				if (!s.grant(length, budget))
					return;
				require(s.emitted_pages < s.limits.pages &&
						length <= s.limits.bytes - s.emitted_bytes);
				s.output = d::make_owned_buffer(s.account, length);
				s.summary = {};
				s.summary.length = length;
				s.summary.keys = cluster.keys;
				s.summary.height = cluster.height + 1;
				s.offset = 0;
				s.phase = Impl::Phase::Zero;
				break;
			}
			case Impl::Phase::Zero: {
				const auto n =
					std::min<std::size_t>(s.summary.length - s.output->size(), budget.bytes);
				s.output->resize(s.output->size() + n);
				budget.bytes -= n;
				if (s.output->size() != s.summary.length)
					return;
				const p::PageHeader h{
					s.summary.length, static_cast<std::uint16_t>(s.sealing->external.size()),
					s.sealing->count, s.summary.keys,
					s.summary.height, {}};
				const auto header = p::encode_page_header(h, s.limits);
				std::copy(header.begin(), header.end(), s.output->mutable_bytes().begin());
				s.phase = Impl::Phase::Nodes;
				break;
			}
			case Impl::Phase::Nodes: {
				const auto &nodes = s.sealing->nodes;
				const auto n = std::min(nodes.size() - s.offset, budget.bytes);
				std::copy_n(nodes.begin() + s.offset, n,
							s.output->mutable_bytes().begin() + 64 + s.offset);
				budget.bytes -= n;
				s.offset += n;
				if (s.offset != nodes.size())
					return;
				if (!s.grant(nodes.size(), budget))
					return;
				relocate_slots(s.output->mutable_bytes().subspan(64, nodes.size()), 64, 0);
				s.external = 0;
				s.phase = Impl::Phase::Externals;
				break;
			}
			case Impl::Phase::Externals:
				if (s.external != s.sealing->external.size()) {
					if (!s.grant(p::child_bytes, budget))
						return;
					const auto &r = s.sealing->external[s.external];
					const auto content = p::encode_content(r);
					const auto placement = p::encode_placement(r);
					auto out = s.output->mutable_bytes();
					const auto count = s.sealing->external.size();
					std::copy(content.begin(), content.end(),
							  out.begin() + out.size() - p::child_bytes * count +
								  p::content_bytes * s.external);
					std::copy(placement.begin(), placement.end(),
							  out.begin() + out.size() - p::placement_bytes * count +
								  p::placement_bytes * s.external);
					++s.external;
					break;
				}
				s.hash.reset();
				s.offset = 0;
				s.phase = Impl::Phase::Hash;
				break;
			case Impl::Phase::Hash: {
				const auto end = s.output->size() - p::placement_bytes * s.sealing->external.size();
				if (s.offset == 32)
					s.offset = 64;
				const auto remaining = s.offset < 32 ? 32 - s.offset : end - s.offset;
				const auto n = std::min(remaining, budget.bytes);
				s.hash.add(s.output->data() + s.offset, n);
				s.offset += n;
				budget.bytes -= n;
				if (s.offset != end) {
					if (!budget.bytes)
						return;
					break;
				}
				if (!s.grant(32, budget))
					return;
				s.hash.getHash(reinterpret_cast<unsigned char *>(s.summary.digest.data()));
				p::count_hash(p::HashKind::Page, 1, end - 32);
				std::copy(s.summary.digest.begin(), s.summary.digest.end(),
						  s.output->mutable_bytes().begin() + 32);
				s.output->seal();
				s.phase = Impl::Phase::Ready;
				return;
			}
			}
		}
	} catch (...) {
		s.failed = true;
		throw;
	}
}
bool PagedPacker::accepted() const noexcept {
	return impl_ && !impl_->failed && !impl_->discarding && impl_->phase == Impl::Phase::Idle;
}
bool PagedPacker::ready() const noexcept {
	return impl_ && !impl_->failed && !impl_->discarding && impl_->phase == Impl::Phase::Ready;
}
SealedPage PagedPacker::page() const {
	if (!ready())
		throw std::logic_error("dense page unavailable");
	impl_->active();
	return {impl_->output, impl_->summary, impl_->sealing->entering_path()};
}
bool PagedPacker::commit(const p::Reference &reference, ValidationBudget &budget) {
	auto &s = *impl_;
	try {
		s.active();
		if (!ready())
			throw std::logic_error("dense page unavailable");
		if (!s.grant(sizeof(Cluster), budget))
			return false;
		p::encode_reference(reference);
		require(reference != p::Reference{} && reference.length == s.summary.length &&
				reference.keys == s.summary.keys && reference.height == s.summary.height &&
				reference.digest == s.summary.digest);
		s.sealing->reference = reference;
		s.sealing->release();
		++s.emitted_pages;
		s.emitted_bytes += reference.length;
		s.output.reset();
		s.phase = s.after_seal;
		return true;
	} catch (...) {
		s.failed = true;
		throw;
	}
}
bool PagedPacker::done() const noexcept {
	return impl_ && !impl_->failed && !impl_->discarding && impl_->phase == Impl::Phase::Done;
}
p::Reference PagedPacker::root() const {
	if (!done())
		throw std::logic_error("dense root unavailable");
	return impl_->root;
}
bool PagedPacker::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	auto &s = *impl_;
	s.discarding = true;
	while (!s.frontier.empty() || s.current || s.output) {
		if (!s.grant(p::page_header_bytes + sizeof(Cluster), budget))
			return false;
		if (s.output)
			s.output.reset();
		else if (s.current)
			s.current.reset();
		else
			s.frontier.pop_back();
	}
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
} // namespace detent::local::detail
