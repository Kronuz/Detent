#include "lazy_page_access.hh"
#include "detent_local/paged_definition.hh"
#include "generation_file_access.hh"
#include "paged_limits.hh"
#include "segment_mapping.hh"
#include "segment_names.hh"
#include <atomic>
#include <lru.h>
#include <mutex>
#include <system_error>
#include <unordered_map>

namespace detent::local::detail {
namespace d = distribution;
namespace p = paged;
namespace {
std::shared_ptr<d::StorageLedger> cache_account_for(const std::shared_ptr<d::StorageLedger> &heap,
                                                    std::size_t bytes) {
	if (!heap)
		throw std::invalid_argument("lazy reader requires a storage account");
	if (!bytes)
		return heap;
	return std::allocate_shared<d::StorageLedger>(d::StorageAllocator<d::StorageLedger>(heap), heap,
	                                              std::min(bytes, heap->limit() / 2));
}
struct IdentityHash {
	std::size_t operator()(const StorageIdentity &value) const noexcept {
		return std::hash<std::string_view>{}({value.data(), value.size()});
	}
};
struct PageKey {
	const SegmentMapping *mapping;
	std::uint32_t offset, length;
	bool operator==(const PageKey &) const = default;
};
struct PageHash {
	std::size_t operator()(const PageKey &key) const noexcept {
		const std::array<std::size_t, 3> parts{std::hash<const void *>{}(key.mapping), key.offset,
		                                       key.length};
		return std::hash<std::string_view>{}({reinterpret_cast<const char *>(parts.data()), sizeof(parts)});
	}
};
struct VerifiedPage {
	p::Digest physical, logical;
};
} // namespace
struct LazyPageAccess::State {
	using Mapping = d::RetirementHandle<SegmentMapping>;
	using MappingEntry = std::pair<const StorageIdentity, Mapping>;
	using Maps = std::unordered_map<StorageIdentity, Mapping, IdentityHash, std::equal_to<StorageIdentity>,
	                                d::StorageAllocator<MappingEntry>>;
	using Node = lru::node<std::pair<const PageKey, VerifiedPage>, lru::base_node>;
	using CacheAllocator = d::StorageAllocator<std::pair<const PageKey, Node>>;
	using Cache = lru::lru<PageKey, VerifiedPage, PageHash, std::equal_to<PageKey>, Node, CacheAllocator>;
	std::shared_ptr<const void> lease;
	int directory;
	uid_t publisher;
	StorageIdentity storage;
	p::Root root;
	ViewLimits limits;
	d::RetirementFactory factory;
	std::shared_ptr<d::StorageLedger> heap, mapping_account, cache_account;
	mutable std::mutex mutex;
	mutable Maps maps;
	mutable std::uint64_t mapped_bytes = 0;
	mutable Cache checked;
	mutable std::atomic<bool> failed{false};
	State(std::shared_ptr<const void> owner, int fd, uid_t uid, StorageIdentity identity, p::Root selected,
	      ViewLimits bounds, d::RetirementFactory allocator, std::shared_ptr<d::StorageLedger> account,
	      std::shared_ptr<d::StorageLedger> mappings)
	    : lease(std::move(owner)), directory(fd), publisher(uid), storage(identity), root(selected),
	      limits(bounds), factory(std::move(allocator)), heap(std::move(account)),
	      mapping_account(std::move(mappings)),
	      cache_account(cache_account_for(heap, limits.verification_cache_bytes)),
	      maps(0, IdentityHash{}, std::equal_to<StorageIdentity>{}, d::StorageAllocator<MappingEntry>(heap)),
	      checked(0, std::chrono::milliseconds(0), CacheAllocator(cache_account)) {
		if (!lease || directory < 0 || !heap || !mapping_account || !factory.uses_account(heap))
			throw std::invalid_argument("lazy reader requires pinned resources");
		p::encode_root(root, paged_limits(limits, p::root_bytes));
	}
	bool evict_one() const {
		if (checked.empty())
			return false;
		auto oldest = checked.end();
		--oldest;
		checked.erase(oldest);
		return true;
	}
	Mapping open_mapping(const StorageIdentity &identity) const {
		if (maps.size() >= root.segments)
			throw ReadSourceFailure("local generation exceeds its pack count");
		const SegmentNames names(identity);
		RecoveryFile payload(::openat(directory, names.payload.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
		struct stat held{};
		if (payload.fd < 0 || ::fstat(payload.fd, &held) != 0 || held.st_size < p::page_header_bytes ||
		    std::uint64_t(held.st_size) > p::segment_bytes)
			throw ReadSourceFailure("local pack has an invalid extent");
		const auto length = static_cast<std::size_t>(held.st_size);
		check_generation_file(payload.fd, publisher, length);
		if (!internal::mode_only_acl(payload.fd))
			throw ReadSourceFailure("segment payload has an unsafe ACL");
		if (mapped_bytes > limits.bytes - p::root_bytes ||
		    length > limits.bytes - p::root_bytes - mapped_bytes)
			throw ReadSourceFailure("local generation exceeds its physical extent");
		auto mapped =
		    factory.make<SegmentMapping>(mapping_account, payload.fd, publisher, SegmentExtent{length});
		struct stat named{};
		if (::fstatat(directory, names.payload.data(), &named, AT_SYMLINK_NOFOLLOW) != 0 ||
		    std::uint64_t(named.st_dev) != mapped->identity().device ||
		    std::uint64_t(named.st_ino) != mapped->identity().inode)
			throw ReadSourceFailure("segment payload changed during attachment");
		return mapped;
	}

	void remember(const PageKey &key, const p::Reference &reference) const {
		if (!limits.verification_cache_bytes)
			return;
		try {
			checked.emplace(key, VerifiedPage{reference.physical, reference.logical});
			return;
		} catch (const d::StoragePressure &) {
		} catch (const std::bad_alloc &) {
		}
		if (!evict_one())
			return;
		try {
			checked.emplace(key, VerifiedPage{reference.physical, reference.logical});
		} catch (const d::StoragePressure &) {
		} catch (const std::bad_alloc &) {
		}
	}
	std::span<const char> resolve(const p::Reference &reference) const {
		std::lock_guard lock(mutex);
		if (failed.load(std::memory_order_relaxed))
			throw ReadSourceFailure("local generation failed verification");
		try {
			auto found = maps.find(reference.segment);
			if (found == maps.end()) {
				try {
					found = maps.emplace(reference.segment, open_mapping(reference.segment)).first;
				} catch (const d::StoragePressure &) {
					if (!evict_one())
						throw;
					found = maps.emplace(reference.segment, open_mapping(reference.segment)).first;
				}
				mapped_bytes += found->second->bytes().size();
			}
			const auto &mapping = *found->second;
			const auto extent = mapping.bytes();
			if (reference.offset > extent.size() || reference.length > extent.size() - reference.offset)
				throw ReadSourceFailure("local page exceeds its segment");
			const PageKey key{&mapping, reference.offset, reference.length};
			const auto bytes = extent.subspan(reference.offset, reference.length);
			if (auto hit = checked.find(key); hit != checked.end()) {
				if (hit->second.physical != reference.physical || hit->second.logical != reference.logical)
					throw ReadSourceFailure("local page reference changed");
				return bytes;
			}
			const auto bounds = paged_limits(limits, p::root_bytes);
			p::verify_page(bytes, reference, bounds);
			const auto header = p::decode_page_header(bytes, bounds);
			if (header.kind == p::PageKind::Definition)
				p::validate_definition(bytes.last(header.body), root.cursor.revision, limits.overrides);
			else if (header.kind == p::PageKind::Namespace &&
			         read_integer(bytes.last(header.body), 0, 8) > root.cursor.revision)
				throw ReadSourceFailure("local namespace exceeds selected revision");
			remember(key, reference);
			return bytes;
		} catch (const d::StoragePressure &) {
			throw ReadSourceFailure("local reader admission exhausted");
		} catch (const std::bad_alloc &) {
			throw ReadSourceFailure("local reader allocation failed");
		} catch (...) {
			failed.store(true, std::memory_order_relaxed);
			throw ReadSourceFailure("local page failed verification");
		}
	}
};
LazyPageAccess::LazyPageAccess(std::shared_ptr<const void> owner, int fd, uid_t publisher,
                               StorageIdentity storage, p::Root root, ViewLimits limits,
                               d::RetirementFactory factory, std::shared_ptr<d::StorageLedger> heap,
                               std::shared_ptr<d::StorageLedger> mappings)
    : state_(d::make_storage_owner<State>(heap, std::malloc, std::free, std::move(owner), fd, publisher,
	                                      storage, root, limits, std::move(factory), heap,
	                                      std::move(mappings))) {}
LazyPageAccess::~LazyPageAccess() = default;
std::span<const char> LazyPageAccess::resolve(const p::Reference &reference) const {
	return state_->resolve(reference);
}
std::uint32_t LazyPageAccess::maximum_overrides() const noexcept { return state_->limits.overrides; }
bool LazyPageAccess::available() const noexcept { return !state_->failed.load(std::memory_order_relaxed); }
std::size_t LazyPageAccess::mapped_segments() const {
	std::lock_guard lock(state_->mutex);
	return state_->maps.size();
}
bool LazyPageAccess::retire_step() noexcept {
	for (std::size_t i = 0; i != ValidationBudget{}.rows; ++i) {
		if (state_->evict_one())
			continue;
		if (state_->maps.empty()) {
			state_.reset();
			return true;
		}
		state_->maps.erase(state_->maps.begin());
	}
	return false;
}
} // namespace detent::local::detail
