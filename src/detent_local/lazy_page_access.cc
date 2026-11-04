#include "detent_local/read_counters.hh"
#include "lazy_page_access.hh"
#include "detent_local/paged_definition.hh"
#include "generation_file_access.hh"
#include "paged_limits.hh"
#include "generation_mapping.hh"
#include "segment_names.hh"
#include <atomic>
#include <bit>
#include <cstring>
#include <sys/mman.h>
#include <system_error>
#include <vector>

namespace detent::local::detail {
namespace d = distribution;
namespace p = paged;
namespace {
std::atomic<void (*)(LazyPageWindow, void *) noexcept> window_hook{nullptr};
std::atomic<void *> window_context{nullptr};
void window(LazyPageWindow at) noexcept {
	if (auto hook = window_hook.load(std::memory_order_relaxed))
		hook(at, window_context.load(std::memory_order_relaxed));
}
// One bit per 80-byte granule: validated pages are disjoint (the validators' cell marks),
// start 8-aligned and span at least one header, so distinct pages own distinct granules.
constexpr std::size_t granule_bytes = p::page_header_bytes;
std::size_t slot_count(std::uint64_t segments) {
	return std::bit_ceil(std::max<std::size_t>(2, std::size_t(std::min<std::uint64_t>(segments, 1u << 20)) * 2));
}
std::size_t identity_hash(const StorageIdentity &identity) noexcept {
	std::uint64_t low, high;
	std::memcpy(&low, identity.data(), 8);
	std::memcpy(&high, identity.data() + 8, 8);
	return std::size_t((low ^ (high * 0x9e3779b97f4a7c15ull)) * 0xff51afd7ed558ccdull >> 17);
}
} // namespace
void LazyPageTesting::install(void (*hook)(LazyPageWindow, void *) noexcept, void *context) noexcept {
	window_context.store(context, std::memory_order_relaxed);
	window_hook.store(hook, std::memory_order_relaxed);
}
// A read-only private mapping of one segment, charged to the mapped-bytes account.
struct Mapping {
	std::span<const char> bytes;
	d::StorageLedger::Ticket ticket;
	Mapping() = default;
	Mapping(Mapping &&other) noexcept
	    : bytes(std::exchange(other.bytes, {})), ticket(std::move(other.ticket)) {}
	~Mapping() {
		if (!bytes.empty())
			::munmap(const_cast<char *>(bytes.data()), bytes.size());
	}
};
struct PageTable::State {
	using Word = std::atomic<std::uint64_t>;
	// A mapped segment and its verified-page bitmap; immutable once published except for the bits.
	struct Segment {
		StorageIdentity identity;
		Mapping mapping;
		std::span<const char> bytes;
		mutable std::vector<Word, d::StorageAllocator<Word>> verified;
		Segment(const StorageIdentity &id, Mapping held, const std::shared_ptr<d::StorageLedger> &account)
		    : identity(id), mapping(std::move(held)), bytes(mapping.bytes),
		      verified((bytes.size() / granule_bytes + 64) / 64, d::StorageAllocator<Word>(account)) {}
	};
	using Slot = std::atomic<Segment *>;
	int directory;
	uid_t publisher;
	StorageIdentity storage;
	p::Root root;
	ViewLimits limits;
	std::shared_ptr<d::StorageLedger> heap, mapping_account;
	mutable std::vector<Slot, d::StorageAllocator<Slot>> slots;
	mutable std::atomic<std::uint64_t> mapped_bytes{0}, published{0}, verifications{0};
	mutable std::atomic<bool> failed{false};
	std::size_t retired = 0;
	State(int fd, uid_t uid, StorageIdentity identity, p::Root selected, ViewLimits bounds,
	      std::shared_ptr<d::StorageLedger> account, std::shared_ptr<d::StorageLedger> mappings)
	    : directory(fd), publisher(uid), storage(identity), root(selected), limits(bounds),
	      heap(std::move(account)), mapping_account(std::move(mappings)),
	      slots(slot_count(root.segments), d::StorageAllocator<Slot>(heap)) {
		if (directory < 0 || !heap || !mapping_account)
			throw std::invalid_argument("lazy reader requires pinned resources");
		p::encode_root(root, paged_limits(limits, p::root_bytes));
	}
	~State() {
		while (release_one())
			;
	}
	void destroy(Segment *segment) const noexcept {
		d::StorageAllocator<Segment> allocator(heap);
		std::destroy_at(segment);
		allocator.deallocate(segment, 1);
	}
	bool release_one() noexcept {
		for (; retired != slots.size(); ++retired)
			if (auto segment = slots[retired].exchange(nullptr, std::memory_order_acquire)) {
				destroy(segment);
				return true;
			}
		return false;
	}
	const char *excess(std::uint64_t segments, std::uint64_t bytes, std::size_t length) const noexcept {
		if (segments > root.segments)
			return "local generation exceeds its pack count";
		if (bytes > limits.bytes - p::root_bytes || length > limits.bytes - p::root_bytes - bytes)
			return "local generation exceeds its physical extent";
		return nullptr;
	}
	// Refuses a new segment past the pack count or the mapped-bytes cap before mapping it. The
	// counts include segments published since this thread's probe, so one of those is not new.
	// Acquire pairs with the winner's release increments, so a counted segment's slot is visible
	// to the re-probe below.
	const Segment *admit(const StorageIdentity &identity, std::size_t probe, std::size_t length) const {
		const auto refusal = excess(published.load(std::memory_order_acquire) + 1,
		                            mapped_bytes.load(std::memory_order_acquire), length);
		if (!refusal)
			return nullptr;
		if (auto raced = find(identity, probe))
			return raced;
		throw ReadSourceFailure(refusal);
	}
	Mapping open_mapping(const StorageIdentity &identity, std::size_t probe, const Segment *&raced) const {
		if ((raced = admit(identity, probe, 0)))
			return {};
		const SegmentNames names(identity);
		DETENT_COUNT(segment_opens, 1);
		RecoveryFile payload(::openat(directory, names.payload.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
		struct stat held{};
		if (payload.fd < 0 || ::fstat(payload.fd, &held) != 0 || held.st_size < p::page_header_bytes ||
		    std::uint64_t(held.st_size) > p::segment_bytes)
			throw ReadSourceFailure("local pack has an invalid extent");
		const auto length = static_cast<std::size_t>(held.st_size);
		check_generation_file(payload.fd, publisher, length);
		if (!internal::mode_only_acl(payload.fd))
			throw ReadSourceFailure("segment payload has an unsafe ACL");
		if ((raced = admit(identity, probe, length)))
			return {};
		Mapping mapped;
		mapped.ticket = mapping_account->admit(rounded_mapping_bytes(length));
		const auto address = ::mmap(nullptr, length, PROT_READ, MAP_PRIVATE, payload.fd, 0);
		if (address == MAP_FAILED)
			throw std::runtime_error("immutable segment mmap failed");
		mapped.bytes = {static_cast<const char *>(address), length};
		struct stat named{};
		if (::fstatat(directory, names.payload.data(), &named, AT_SYMLINK_NOFOLLOW) != 0 ||
		    named.st_dev != held.st_dev || named.st_ino != held.st_ino)
			throw ReadSourceFailure("segment payload changed during attachment");
		return mapped;
	}
	// Slots publish with a release compare-and-swap and are read with acquire loads, so a reader
	// sees a segment's mapping, extent and zeroed bitmap; relaxed here is a data race on them.
	// Probes from `probe`; sets `probe` to the first empty slot when the identity is absent.
	const Segment *find(const StorageIdentity &identity, std::size_t &probe) const {
		const auto mask = slots.size() - 1;
		for (std::size_t n = 0; n != slots.size(); probe = (probe + 1) & mask, ++n) {
			const auto found = slots[probe].load(std::memory_order_acquire);
			if (!found || found->identity == identity)
				return found;
		}
		throw ReadSourceFailure("local generation exceeds its pack count");
	}
	const Segment &segment(const StorageIdentity &identity) const {
		auto probe = identity_hash(identity) & (slots.size() - 1);
		if (auto found = find(identity, probe))
			return *found;
		return publish(identity, probe);
	}
	const Segment &publish(const StorageIdentity &identity, std::size_t probe) const {
		d::StorageAllocator<Segment> allocator(heap);
		window(LazyPageWindow::Miss);
		const Segment *raced = nullptr;
		auto mapping = open_mapping(identity, probe, raced);
		if (raced)
			return *raced;
		auto mine = allocator.allocate(1);
		try {
			std::construct_at(mine, identity, std::move(mapping), heap);
		} catch (...) {
			allocator.deallocate(mine, 1);
			throw;
		}
		window(LazyPageWindow::Publish);
		const auto mask = slots.size() - 1;
		for (std::size_t n = 0; n != slots.size(); probe = (probe + 1) & mask, ++n) {
			Segment *found = nullptr;
			if (slots[probe].compare_exchange_strong(found, mine, std::memory_order_release,
			                                         std::memory_order_acquire)) {
				const auto length = mine->bytes.size();
				// Release orders the slot store before the counts admit() reads.
				if (auto refusal = excess(published.fetch_add(1, std::memory_order_release) + 1,
				                          mapped_bytes.fetch_add(length, std::memory_order_release), length))
					throw ReadSourceFailure(refusal);
				return *mine;
			}
			if (found->identity == identity) {
				destroy(mine);
				return *found;
			}
		}
		destroy(mine);
		throw ReadSourceFailure("local generation exceeds its pack count");
	}
	std::span<const char> resolve(const p::Reference &reference) const {
		if (failed.load(std::memory_order_relaxed))
			throw ReadSourceFailure("local generation failed verification");
		try {
			const auto &mapped = segment(reference.segment);
			const auto extent = mapped.bytes;
			if (reference.length < p::page_header_bytes || reference.offset > extent.size() ||
			    reference.length > extent.size() - reference.offset)
				throw ReadSourceFailure("local page exceeds its segment");
			const auto bytes = extent.subspan(reference.offset, reference.length);
			const auto granule = reference.offset / granule_bytes;
			auto &word = mapped.verified[granule / 64];
			const auto bit = std::uint64_t(1) << (granule % 64);
			// Release on the set pairs with acquire on the test, so a hit happens after a completed
			// verification. The bytes are immutable while mapped, so this orders no data today.
			if (word.load(std::memory_order_acquire) & bit)
				return bytes;
			const auto bounds = paged_limits(limits, p::root_bytes);
			p::verify_page(bytes, reference, bounds);
			DETENT_COUNT(pages_verified, 1);
			const auto header = p::decode_page_header(bytes, bounds);
			if (header.kind == p::PageKind::Definition)
				p::validate_definition(bytes.last(header.body), root.cursor.revision, limits.overrides);
			else if (header.kind == p::PageKind::Namespace &&
			         read_integer(bytes.last(header.body), 0, 8) > root.cursor.revision)
				throw ReadSourceFailure("local namespace exceeds selected revision");
			verifications.fetch_add(1, std::memory_order_relaxed);
			window(LazyPageWindow::Mark);
			word.fetch_or(bit, std::memory_order_release);
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
PageTable::PageTable(int fd, uid_t publisher, StorageIdentity storage, p::Root root, ViewLimits limits,
                     std::shared_ptr<d::StorageLedger> heap, std::shared_ptr<d::StorageLedger> mappings)
    : state_(d::make_storage_owner<State>(heap, std::malloc, std::free, fd, publisher, storage, root, limits,
                                          heap, std::move(mappings))) {}
PageTable::~PageTable() = default;
PageTable::PageTable(PageTable &&) noexcept = default;
std::span<const char> PageTable::resolve(const p::Reference &reference) const { return state_->resolve(reference); }
std::uint32_t PageTable::maximum_overrides() const noexcept { return state_->limits.overrides; }
bool PageTable::available() const noexcept { return !state_->failed.load(std::memory_order_relaxed); }
std::size_t PageTable::mapped_segments() const { return state_->published.load(std::memory_order_relaxed); }
std::uint64_t PageTable::page_verifications() const noexcept {
	return state_->verifications.load(std::memory_order_relaxed);
}
bool PageTable::release_one() noexcept { return state_ && state_->release_one(); }

LazyPageAccess::LazyPageAccess(std::shared_ptr<const void> owner, int fd, uid_t publisher,
                               StorageIdentity storage, p::Root root, ViewLimits limits,
                               std::shared_ptr<d::StorageLedger> heap, std::shared_ptr<d::StorageLedger> mappings)
    : lease_(std::move(owner)) {
	if (!lease_)
		throw std::invalid_argument("lazy reader requires pinned resources");
	table_.emplace(fd, publisher, storage, root, limits, std::move(heap), std::move(mappings));
}
LazyPageAccess::~LazyPageAccess() = default;
std::span<const char> LazyPageAccess::resolve(const p::Reference &reference) const {
	return table_->resolve(reference);
}
std::uint32_t LazyPageAccess::maximum_overrides() const noexcept { return table_->maximum_overrides(); }
bool LazyPageAccess::available() const noexcept { return table_->available(); }
std::size_t LazyPageAccess::mapped_segments() const { return table_->mapped_segments(); }
std::uint64_t LazyPageAccess::page_verifications() const noexcept { return table_->page_verifications(); }
bool LazyPageAccess::retire_step() noexcept {
	for (std::size_t i = 0; i != ValidationBudget{}.rows; ++i)
		if (!table_->release_one()) {
			table_.reset();
			lease_.reset();
			return true;
		}
	return false;
}
} // namespace detent::local::detail
