// The reader core (docs/design/reader.md D1, D2, D4, D5, D10; docs/mechanisms/reader-core.md).
#include "detent/reader.h"
#include "change_file.hh"
#include "detent/validation.hh"
#include "detent_distribution/storage.hh"
#include "detent_local/read_counters.hh"
#include "evaluation.hh"
#include "generation_file_access.hh"
#include "lazy_page_access.hh"
#include "paged_read_state.hh"
#include "prepared_reader.hh"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <exception>
#include <iterator>
#include <limits>
#include <mutex>
#include <new>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <time.h>
#include <unistd.h>
#include <vector>
#ifdef __APPLE__
#include <mach/mach_time.h>
#endif
#ifdef DETENT_READER_TEST_HOOKS
#include "testing.hh"
#endif

namespace detent::reader::detail {
namespace l = detent::local;
namespace x = detent::local::detail;
namespace d = detent::distribution;

#ifdef DETENT_READER_TEST_HOOKS
using Window = detent::reader::testing::Window;
void (*window_hook)(Window, void *) noexcept = nullptr;
void *window_context = nullptr;
inline void window(Window at) noexcept {
	if (window_hook)
		window_hook(at, window_context);
}
#define DETENT_WINDOW(at) window(Window::at)
thread_local std::string failure_text;
#else
#define DETENT_WINDOW(at) ((void)0)
#endif

constexpr std::size_t ring_size = 4, stripes = 64;
constexpr std::uint64_t millisecond = 1'000'000;
constexpr std::uint64_t store_retry_ns = 200 * millisecond, backoff_min_ns = millisecond,
                        backoff_max_ns = 200 * millisecond;

// Monotonic nanoseconds without a syscall; read only on the retry paths.
std::uint64_t now_ns() noexcept {
#ifdef __APPLE__
	static const mach_timebase_info_data_t base = [] {
		mach_timebase_info_data_t info{};
		mach_timebase_info(&info);
		return info;
	}();
	return mach_absolute_time() * base.numer / base.denom;
#else
	timespec ts{};
	clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
	return std::uint64_t(ts.tv_sec) * 1'000'000'000 + std::uint64_t(ts.tv_nsec);
#endif
}

struct Failure {
	detent_status status;
};
// Every exception an attach or a table read can raise, as the status it reports.
detent_status classify(std::exception_ptr error) noexcept {
	try {
		std::rethrow_exception(error);
	} catch (const l::GenerationUnavailable &) {
		return DETENT_UNAVAILABLE;
	} catch (const Failure &failure) {
		return failure.status;
	} catch (const std::bad_alloc &) {
		return DETENT_NO_MEMORY;
	} catch (const d::StoragePressure &) {
		return DETENT_NO_MEMORY;
	} catch (const std::system_error &) {
		return DETENT_IO;
	} catch (...) {
		return DETENT_SOURCE_FAILURE;
	}
}

// One attached table entry's generation: its lease, checked records and page table.
struct Generation {
	x::AttachedGeneration attached;
	x::PageTable pages;
	x::PagedReadState state;
	std::size_t views = 0; // views holding it; changed only by the holder of `refreshing`
	Generation(x::AttachedGeneration generation, int directory, uid_t publisher,
	           const std::shared_ptr<d::StorageLedger> &heap, const std::shared_ptr<d::StorageLedger> &mappings)
	    : attached(std::move(generation)),
	      pages(directory, publisher, attached.storage, attached.root, attached.limits, heap, mappings),
	      state{attached.root, {}, std::shared_ptr<const x::PagedPageAccess>(std::shared_ptr<void>(), &pages)} {}
};
// One mapping of `change`, a member of every view built over it (reader.md D4 rule 4).
struct Change {
	x::ChangeMapping mapping;
	std::size_t views = 0;
};
// Today's store table has one entry, the selected generation, serving every namespace at slot 0.
constexpr std::uint32_t whole_store_slot = 0;
struct Entry {
	Generation *generation;
	const x::ChangeWord *sequence;
	std::uint64_t seen;
};
// Immutable once published: the store word and table it was built from, and the attached entries.
struct View {
	Change *change = nullptr; // null while the store has no change file (Q6)
	std::uint64_t store_seen = 0;
	bool table = false; // `selected` was present when the view was built
	std::vector<Entry> entries;
	const Entry *find(std::string_view) const noexcept { return entries.empty() ? nullptr : &entries.front(); }
};
struct alignas(64) Stripe {
	std::atomic<std::uint64_t> count{0};
};
struct Slot {
	std::array<Stripe, stripes> counts;
	std::atomic<View *> view{nullptr};
	std::uint64_t sum() const noexcept {
		std::uint64_t total = 0;
		for (const auto &stripe : counts)
			total += stripe.count.load(std::memory_order_seq_cst);
		return total;
	}
};
std::size_t stripe_of(const void *frame) noexcept {
	return std::size_t((std::uintptr_t(frame) >> 12) * 0x9e3779b97f4a7c15ull >> 58);
}
} // namespace detent::reader::detail
using namespace detent::reader::detail;

struct detent_reader {
	std::string store;
	std::vector<std::string> namespaces; // sorted; empty: all
	uid_t publisher = 0;
	int directory = -1; // the trusted store directory, opened once
	std::shared_ptr<d::StorageLedger> heap, mappings;
	std::optional<l::StorageIdentity> storage;
	std::array<Slot, ring_size> ring;
	alignas(64) std::atomic<std::uint64_t> current{0};
	alignas(64) std::atomic<bool> refreshing{false};
	// Written by the holder of `refreshing`; read by lookups deciding whether to try it.
	std::atomic<std::uint64_t> retry_at{0}, failed_store{UINT64_MAX}, store_try_at{0};
	std::atomic<int> failed_status{DETENT_OK};
	std::uint64_t backoff = 0;
	// Relaxed counters: written by the holder of `refreshing`, read by `testing::stats` from any thread.
	std::atomic<std::uint64_t> publishes{0}, refused{0}, attached{0}, generations{0};
	detent_reader *next = nullptr;
	~detent_reader() {
		for (auto &slot : ring)
			free_view(slot.view.exchange(nullptr));
		if (directory >= 0)
			::close(directory);
	}
	void free_view(View *view) noexcept {
		if (!view)
			return;
		for (const auto &entry : view->entries)
			if (!--entry.generation->views) {
				delete entry.generation;
				generations.fetch_sub(1, std::memory_order_relaxed);
			}
		if (view->change && !--view->change->views)
			delete view->change;
		delete view;
	}
	bool try_refreshing() noexcept {
		bool idle = false;
		return refreshing.compare_exchange_strong(idle, true, std::memory_order_acquire, std::memory_order_relaxed);
	}
	void release_refreshing() noexcept {
		refreshing.store(false, std::memory_order_release);
		refreshing.notify_all();
	}
	std::size_t live_views() const noexcept {
		std::size_t live = 0;
		for (const auto &slot : ring)
			live += slot.view.load(std::memory_order_relaxed) != nullptr;
		return live;
	}
};

namespace {
// Fork (reader.md D10): live handles under one mutex; the handlers take every `refreshing`.
std::mutex registry_mutex;
detent_reader *registry = nullptr;
pthread_once_t atfork_once = PTHREAD_ONCE_INIT;
void fork_prepare() noexcept {
	registry_mutex.lock();
	for (auto *handle = registry; handle; handle = handle->next)
		while (!handle->try_refreshing())
			handle->refreshing.wait(true, std::memory_order_relaxed);
	for (auto *handle = registry; handle; handle = handle->next)
		for (auto &slot : handle->ring)
			if (const auto *view = slot.view.load(std::memory_order_relaxed))
				for (const auto &entry : view->entries)
					entry.generation->pages.prepare_fork();
	DETENT_WINDOW(ForkPrepared);
}
void fork_parent() noexcept {
	for (auto *handle = registry; handle; handle = handle->next) {
		for (auto &slot : handle->ring)
			if (const auto *view = slot.view.load(std::memory_order_relaxed))
				for (const auto &entry : view->entries)
					entry.generation->pages.parent_after_fork();
		handle->release_refreshing();
	}
	registry_mutex.unlock();
}
// The child runs one thread, the one that forked, which is not inside a lookup (D1 item 8): counts
// left by the parent's threads would pin their slots forever.
void fork_child() noexcept {
	for (auto *handle = registry; handle; handle = handle->next) {
		for (auto &slot : handle->ring) {
			for (auto &stripe : slot.counts)
				stripe.count.store(0, std::memory_order_relaxed);
			if (const auto *view = slot.view.load(std::memory_order_relaxed))
				for (const auto &entry : view->entries)
					entry.generation->pages.child_after_fork();
		}
		handle->refreshing.store(false, std::memory_order_release);
	}
	registry_mutex.unlock();
}
void register_handle(detent_reader *handle) {
	pthread_once(&atfork_once, [] {
		if (pthread_atfork(fork_prepare, fork_parent, fork_child) != 0)
			std::abort();
	});
	std::lock_guard lock(registry_mutex);
	handle->next = registry;
	registry = handle;
}
void unregister_handle(detent_reader *handle) noexcept {
	std::lock_guard lock(registry_mutex);
	for (auto **link = &registry; *link; link = &(*link)->next)
		if (*link == handle) {
			*link = handle->next;
			return;
		}
}

std::string_view view_of(detent_str s) noexcept { return {s.data ? s.data : "", s.data ? s.size : 0}; }
std::optional<std::string_view> optional_of(detent_str s) noexcept {
	if (!s.data)
		return std::nullopt;
	return std::string_view(s.data, s.size);
}
bool valid_part(std::string_view part) noexcept {
	return !part.empty() && part.size() <= 198 &&
	       std::all_of(part.begin(), part.end(), detent::detail::is_name_char);
}
// Splits `ns:key` as the spec names knobs (view.cc's lookup rule).
bool split(detent_str name, std::string_view &ns, std::string_view &key) noexcept {
	const auto text = view_of(name);
	const auto colon = text.find(':');
	if (!name.data || colon == std::string_view::npos)
		return false;
	ns = text.substr(0, colon);
	key = text.substr(colon + 1);
	return valid_part(ns) && valid_part(key) && ns.size() + key.size() + 1 <= 200;
}
bool opened(const detent_reader &handle, std::string_view ns) noexcept {
	return handle.namespaces.empty() ||
	       std::binary_search(handle.namespaces.begin(), handle.namespaces.end(), ns);
}

// Unpins on every path out of a lookup.
struct Pin {
	Slot *slot = nullptr;
	std::size_t stripe;
	explicit Pin(std::size_t s) noexcept : stripe(s) {}
	~Pin() { release(); }
	void release() noexcept {
		if (slot)
			slot->counts[stripe].count.fetch_sub(1, std::memory_order_release);
		slot = nullptr;
	}
	// The reader half of D5's Dekker pair: increment, then recheck `current`.
	const View *acquire(detent_reader &handle) noexcept {
		for (;;) {
			const auto s = handle.current.load(std::memory_order_acquire);
			DETENT_WINDOW(BeforePin);
			auto &candidate = handle.ring[s % ring_size];
			candidate.counts[stripe].count.fetch_add(1, std::memory_order_seq_cst);
			if (handle.current.load(std::memory_order_seq_cst) == s) {
				slot = &candidate;
				DETENT_WINDOW(Pinned);
				return candidate.view.load(std::memory_order_acquire);
			}
			candidate.counts[stripe].count.fetch_sub(1, std::memory_order_release);
		}
	}
};

enum class Outcome { Published, Unchanged, Refused, Failed };

// Opens the store directory and `change` (Q6 (a): an absent store is not an error).
void open_store(detent_reader &handle, Change *&change) {
	if (handle.directory < 0) {
		struct stat info{};
		if (::stat(handle.store.c_str(), &info) != 0 && errno == ENOENT)
			throw l::GenerationUnavailable("store directory does not exist yet");
		try {
			handle.directory = x::open_publisher_directory(handle.store, handle.publisher);
		} catch (const std::bad_alloc &) {
			throw;
		} catch (...) {
			throw Failure{DETENT_UNTRUSTED};
		}
	}
	x::ChangeMapping mapping;
	x::ChangeMapping::Open opened;
	try {
		opened = x::ChangeMapping::open(handle.directory, handle.publisher, mapping);
	} catch (const x::ChangeRefused &) {
		throw Failure{DETENT_SOURCE_FAILURE};
	}
	if (opened == x::ChangeMapping::Open::Absent) {
		// D4 rule 5: a store with `selected` and no `change` is refused; with neither it is empty.
		struct stat info{};
		if (::fstatat(handle.directory, "selected", &info, AT_SYMLINK_NOFOLLOW) == 0)
			throw Failure{DETENT_SOURCE_FAILURE};
		throw l::GenerationUnavailable("store has no change file yet");
	}
	DETENT_WINDOW(ChangeMapped);
	change = new Change{std::move(mapping), 0};
}

// The publisher half of D5. Only the holder of `refreshing` runs it. `want` attaches the whole-store
// entry when the view has none; `force` is detent_refresh, which ignores the retry clocks.
Outcome refresh_locked(detent_reader &handle, bool want, bool force) noexcept {
	DETENT_WINDOW(Refreshing);
	const auto s = handle.current.load(std::memory_order_relaxed);
	const View *installed = handle.ring[s % ring_size].view.load(std::memory_order_relaxed);
	auto &next = handle.ring[(s + 1) % ring_size];
	// Checked before attaching, so a refusal has built nothing to free.
	if (auto *old = next.view.load(std::memory_order_relaxed)) {
		if (next.sum() != 0) {
			handle.refused.fetch_add(1, std::memory_order_relaxed);
			DETENT_COUNT(refused_publishes, 1);
			handle.backoff = std::clamp(handle.backoff * 2, backoff_min_ns, backoff_max_ns);
			handle.retry_at.store(now_ns() + handle.backoff, std::memory_order_relaxed);
			return Outcome::Refused;
		}
		next.view.store(nullptr, std::memory_order_relaxed);
		handle.free_view(old);
		DETENT_WINDOW(AfterFree);
	}
	View *built = nullptr;
	Generation *fresh = nullptr;
	Change *new_change = nullptr;
	std::uint64_t attempted = UINT64_MAX; // the store word this attempt read the table at
	try {
		Change *change = installed ? installed->change : nullptr;
		if (!change || force) {
			if (!change && !force) {
				// Q6 (a): with no store mapped, retry the open at most once per 200 ms.
				const auto now = now_ns();
				if (now < handle.store_try_at.load(std::memory_order_relaxed))
					return Outcome::Unchanged;
				handle.store_try_at.store(now + store_retry_ns, std::memory_order_relaxed);
			}
			if (change) {
				// Limit 2: detent_refresh re-stats `change` and remaps a replaced file.
				struct stat info{};
				if (::fstatat(handle.directory, x::change_name, &info, AT_SYMLINK_NOFOLLOW) != 0 ||
				    std::uint64_t(info.st_dev) != change->mapping.device() || std::uint64_t(info.st_ino) != change->mapping.inode())
					open_store(handle, new_change);
			} else
				open_store(handle, new_change);
			if (new_change)
				change = new_change;
		}
		// D4 rule 1: load the store word and the sequences before reading the table.
		const auto store_word = change->mapping.store().load(std::memory_order_acquire);
		attempted = store_word;
		const x::ChangeWord *sequence = change->mapping.sequence(whole_store_slot);
		const auto seen = sequence ? sequence->load(std::memory_order_acquire) : 0;
		std::optional<l::Descriptor> table;
		try {
			table = x::PreparedReader::selected(handle.directory, handle.publisher);
		} catch (const l::GenerationUnavailable &) {
		}
		const Entry *had = installed ? installed->find({}) : nullptr;
		built = new View;
		built->change = change;
		built->store_seen = store_word;
		built->table = table.has_value();
		if (table && (had || want)) {
			if (!sequence) {
				// A slot past the mapping: only the winner remaps (D4 rule 4).
				Change *grown = nullptr;
				open_store(handle, grown);
				if (new_change)
					delete new_change;
				new_change = grown;
				built->change = change = grown;
				sequence = change->mapping.sequence(whole_store_slot);
				if (!sequence)
					throw Failure{DETENT_SOURCE_FAILURE};
			}
			Generation *generation = nullptr;
			if (had && had->generation->attached.descriptor.generation == table->generation)
				generation = had->generation;
			else {
				DETENT_WINDOW(Attaching);
				auto attached = x::PreparedReader::attach(handle.directory, handle.publisher,
				                                          l::ViewLimits::prepared());
				const auto &cursor = attached.descriptor.cursor;
				if (had) {
					// A4: no regression within an incarnation.
					const auto &previous = had->generation->attached.descriptor.cursor;
					if (previous.same_identity(cursor) && cursor.revision < previous.revision)
						throw Failure{DETENT_SOURCE_FAILURE};
				}
				if (handle.storage && *handle.storage != attached.storage)
					throw Failure{DETENT_SOURCE_FAILURE}; // a store identity change
				fresh = new Generation(std::move(attached), handle.directory, handle.publisher, handle.heap,
				                       handle.mappings);
				generation = fresh;
			}
			built->entries.push_back({generation, sequence, seen});
		}
		if (installed && built->change == installed->change && built->store_seen == installed->store_seen &&
		    built->table == installed->table && built->entries.size() == installed->entries.size() &&
		    (built->entries.empty() || (built->entries[0].generation == installed->entries[0].generation &&
		                                built->entries[0].seen == installed->entries[0].seen))) {
			delete built;
			return Outcome::Unchanged;
		}
	} catch (...) {
		const auto status = classify(std::current_exception());
#ifdef DETENT_READER_TEST_HOOKS
		try {
			std::rethrow_exception(std::current_exception());
		} catch (const std::exception &error) {
			failure_text = error.what();
		} catch (const Failure &failure) {
			failure_text = "status " + std::to_string(int(failure.status));
		} catch (...) {
			failure_text = "unknown";
		}
#endif
		delete built;
		delete fresh;
		delete new_change;
		handle.failed_status.store(status, std::memory_order_relaxed);
		// Remembered at the word the attempt read (D4): a publish since then retries it.
		if (attempted == UINT64_MAX && installed && installed->change)
			attempted = installed->change->mapping.store().load(std::memory_order_acquire);
		if (attempted != UINT64_MAX)
			handle.failed_store.store(attempted, std::memory_order_release);
		return Outcome::Failed;
	}
	// Commit: count the new view's members, install it, then free what no reader holds.
	for (const auto &entry : built->entries)
		++entry.generation->views;
	++built->change->views;
	if (fresh) {
		handle.generations.fetch_add(1, std::memory_order_relaxed);
		handle.attached.fetch_add(1, std::memory_order_relaxed);
		DETENT_COUNT(namespaces_attached, 1);
		if (!handle.storage)
			handle.storage = fresh->attached.storage;
	}
	next.view.store(built, std::memory_order_release);
	handle.current.store(s + 1, std::memory_order_seq_cst);
	handle.publishes.fetch_add(1, std::memory_order_relaxed);
	handle.backoff = 0;
	handle.retry_at.store(0, std::memory_order_relaxed);
	handle.failed_store.store(UINT64_MAX, std::memory_order_relaxed);
	for (std::size_t k = 0; k != ring_size; ++k) {
		auto &slot = handle.ring[k];
		if (&slot == &next)
			continue;
		if (auto *old = slot.view.load(std::memory_order_relaxed); old && slot.sum() == 0) {
			DETENT_WINDOW(Freeing);
			slot.view.store(nullptr, std::memory_order_relaxed);
			handle.free_view(old);
		}
	}
	return Outcome::Published;
}

// A failure remembered at this store word answers until the word moves or detent_refresh runs; with
// no store mapped, until the next open attempt is due (Q6).
bool remembered(const detent_reader &handle, const View *view, detent_status &status) noexcept {
	if (!view || !view->change) {
		if (now_ns() >= handle.store_try_at.load(std::memory_order_relaxed))
			return false;
		status = detent_status(handle.failed_status.load(std::memory_order_relaxed));
		if (status == DETENT_OK)
			status = DETENT_UNAVAILABLE;
		return true;
	}
	const auto failed = handle.failed_store.load(std::memory_order_acquire);
	if (failed == UINT64_MAX || view->change->mapping.store().load(std::memory_order_acquire) != failed)
		return false;
	status = detent_status(handle.failed_status.load(std::memory_order_relaxed));
	return true;
}

// One lookup in `ns`: answers from the namespace's generation in the installed view, switching or
// attaching first when its sequence moved (D2, D4). `answer` must not throw.
template <class Answer> detent_status lookup(detent_reader &handle, std::string_view ns, Answer &&answer) noexcept {
	const char frame = 0;
	const auto stripe = stripe_of(&frame);
	for (;;) {
		Pin pin(stripe);
		const View *view = pin.acquire(handle);
		const Entry *entry = view ? view->find(ns) : nullptr;
		if (entry) {
			if (entry->sequence->load(std::memory_order_acquire) == entry->seen)
				return answer(*entry->generation);
			detent_status failed;
			if (remembered(handle, view, failed) ||
			    now_ns() < handle.retry_at.load(std::memory_order_relaxed) || !handle.try_refreshing())
				return answer(*entry->generation); // stale, never waiting (D4)
			pin.release();
			refresh_locked(handle, true, false);
			handle.release_refreshing();
			continue;
		}
		detent_status failed;
		if (remembered(handle, view, failed))
			return failed;
		if (view && view->change && !view->table &&
		    view->change->mapping.store().load(std::memory_order_acquire) == view->store_seen)
			return DETENT_UNAVAILABLE;
		if (now_ns() < handle.retry_at.load(std::memory_order_relaxed))
			return DETENT_UNAVAILABLE;
		pin.release();
		if (handle.try_refreshing()) {
			const auto outcome = refresh_locked(handle, true, false);
			handle.release_refreshing();
			if (outcome == Outcome::Published)
				continue;
			Pin again(stripe);
			const View *now = again.acquire(handle);
			if (const Entry *found = now ? now->find(ns) : nullptr)
				return answer(*found->generation);
			if (outcome == Outcome::Failed)
				return detent_status(handle.failed_status.load(std::memory_order_relaxed));
			return DETENT_UNAVAILABLE;
		}
		// A namespace's first attach waits for the winner, so no lookup answers its default for losing.
		handle.refreshing.wait(true, std::memory_order_acquire);
	}
}

template <class T> detent_status fail(detent_status status, T *out, T fallback) noexcept {
	if (out)
		*out = fallback;
	return status;
}
} // namespace

extern "C" {
uint32_t detent_abi_version(void) { return DETENT_ABI_VERSION; }

const char *detent_status_name(detent_status status) {
	switch (status) {
	case DETENT_OK: return "ok";
	case DETENT_MISSING: return "missing";
	case DETENT_WRONG_TYPE: return "wrong_type";
	case DETENT_INVALID_NAME: return "invalid_name";
	case DETENT_NOT_OPENED: return "not_opened";
	case DETENT_SOURCE_FAILURE: return "source_failure";
	case DETENT_UNAVAILABLE: return "unavailable";
	case DETENT_UNTRUSTED: return "untrusted";
	case DETENT_INVALID_ARGUMENT: return "invalid_argument";
	case DETENT_NO_MEMORY: return "no_memory";
	case DETENT_IO: return "io";
	}
	return "unknown";
}

detent_status detent_open(const detent_open_options *options, detent_reader **out) {
	if (!out)
		return DETENT_INVALID_ARGUMENT;
	*out = nullptr;
	constexpr auto required = offsetof(detent_open_options, publisher_uid) + sizeof(uint32_t);
	if (!options || options->size < required || !options->store.data || !options->store.size ||
	    view_of(options->store).find('\0') != std::string_view::npos ||
	    (options->namespace_count && !options->namespaces))
		return DETENT_INVALID_ARGUMENT;
	detent_reader *handle = nullptr;
	try {
		handle = new detent_reader;
		handle->store.assign(options->store.data, options->store.size);
		handle->publisher = uid_t(options->publisher_uid);
		for (std::size_t i = 0; i != options->namespace_count; ++i) {
			const auto ns = view_of(options->namespaces[i]);
			if (!options->namespaces[i].data || !valid_part(ns)) {
				delete handle;
				return DETENT_INVALID_ARGUMENT;
			}
			handle->namespaces.emplace_back(ns);
		}
		std::sort(handle->namespaces.begin(), handle->namespaces.end());
		handle->namespaces.erase(std::unique(handle->namespaces.begin(), handle->namespaces.end()),
		                         handle->namespaces.end());
		constexpr auto unbounded = std::numeric_limits<std::size_t>::max() / 4;
		handle->heap = std::make_shared<d::StorageLedger>(unbounded);
		handle->mappings = std::make_shared<d::StorageLedger>(unbounded);
	} catch (...) {
		delete handle;
		return DETENT_NO_MEMORY;
	}
	// Directory trust once, `change` and the table; nothing attached (D2). An empty store opens (Q6).
	handle->refreshing.store(true, std::memory_order_relaxed);
	const auto outcome = refresh_locked(*handle, false, false);
	handle->refreshing.store(false, std::memory_order_relaxed);
	if (outcome == Outcome::Failed) {
		const auto status = detent_status(handle->failed_status.load(std::memory_order_relaxed));
		if (status != DETENT_UNAVAILABLE) {
			delete handle;
			return status;
		}
		handle->store_try_at.store(now_ns() + store_retry_ns, std::memory_order_relaxed);
	}
	try {
		register_handle(handle);
	} catch (...) {
		delete handle;
		return DETENT_NO_MEMORY;
	}
	*out = handle;
	return DETENT_OK;
}

detent_status detent_eval(detent_reader *handle, detent_str name, detent_str hash, detent_str switch_value,
                          bool fallback, bool *out) {
	if (!handle || !out)
		return fail(DETENT_INVALID_ARGUMENT, out, fallback);
	std::string_view ns, key;
	if (!split(name, ns, key))
		return fail(DETENT_INVALID_NAME, out, fallback);
	if (!opened(*handle, ns))
		return fail(DETENT_NOT_OPENED, out, fallback);
	const auto h = optional_of(hash), sw = optional_of(switch_value);
	bool value = fallback;
	const auto status = lookup(*handle, ns, [&](const Generation &generation) noexcept {
		try {
			const auto definition = generation.state.definition(ns, key);
			if (!definition)
				return DETENT_MISSING;
			if (!definition->boolean())
				return DETENT_WRONG_TYPE;
			value = l::evaluate(*definition, ns, key, h, sw);
			return DETENT_OK;
		} catch (...) {
			return classify(std::current_exception());
		}
	});
	*out = status == DETENT_OK ? value : fallback;
	return status;
}

detent_status detent_get(detent_reader *handle, detent_str name, int64_t fallback, int64_t *out) {
	if (!handle || !out)
		return fail(DETENT_INVALID_ARGUMENT, out, fallback);
	std::string_view ns, key;
	if (!split(name, ns, key))
		return fail(DETENT_INVALID_NAME, out, fallback);
	if (!opened(*handle, ns))
		return fail(DETENT_NOT_OPENED, out, fallback);
	int64_t value = fallback;
	const auto status = lookup(*handle, ns, [&](const Generation &generation) noexcept {
		try {
			const auto definition = generation.state.definition(ns, key);
			if (!definition)
				return DETENT_MISSING;
			if (definition->boolean())
				return DETENT_WRONG_TYPE;
			value = definition->default_int();
			return DETENT_OK;
		} catch (...) {
			return classify(std::current_exception());
		}
	});
	*out = status == DETENT_OK ? value : fallback;
	return status;
}

detent_status detent_read(detent_reader *handle, detent_str name, detent_str hash, detent_str switch_value,
                          detent_value *out) {
	const detent_value none{};
	if (!handle || !out)
		return fail(DETENT_INVALID_ARGUMENT, out, none);
	std::string_view ns, key;
	if (!split(name, ns, key))
		return fail(DETENT_INVALID_NAME, out, none);
	if (!opened(*handle, ns))
		return fail(DETENT_NOT_OPENED, out, none);
	const auto h = optional_of(hash), sw = optional_of(switch_value);
	detent_value value = none;
	const auto status = lookup(*handle, ns, [&](const Generation &generation) noexcept {
		try {
			const auto definition = generation.state.definition(ns, key);
			if (!definition)
				return DETENT_MISSING;
			if (definition->boolean())
				value = {DETENT_KIND_BOOL, l::evaluate(*definition, ns, key, h, sw), 0};
			else
				value = {DETENT_KIND_INT, false, definition->default_int()};
			return DETENT_OK;
		} catch (...) {
			return classify(std::current_exception());
		}
	});
	*out = status == DETENT_OK ? value : none;
	return status;
}

detent_status detent_revision(detent_reader *handle, detent_str name_space, uint64_t *out) {
	if (!handle || !out)
		return fail(DETENT_INVALID_ARGUMENT, out, uint64_t(0));
	const auto ns = view_of(name_space);
	if (!name_space.data || !valid_part(ns))
		return fail(DETENT_INVALID_NAME, out, uint64_t(0));
	if (!opened(*handle, ns))
		return fail(DETENT_NOT_OPENED, out, uint64_t(0));
	uint64_t revision = 0;
	const auto status = lookup(*handle, ns, [&](const Generation &generation) noexcept {
		revision = generation.attached.descriptor.cursor.revision;
		return DETENT_OK;
	});
	*out = status == DETENT_OK ? revision : 0;
	return status;
}

detent_status detent_refresh(detent_reader *handle) {
	if (!handle)
		return DETENT_INVALID_ARGUMENT;
	if (!handle->try_refreshing())
		return DETENT_OK; // another thread is refreshing
	const auto outcome = refresh_locked(*handle, false, true);
	const auto status = outcome == Outcome::Failed
	                        ? detent_status(handle->failed_status.load(std::memory_order_relaxed))
	                        : DETENT_OK;
	handle->release_refreshing();
	return status;
}

void detent_close(detent_reader *handle) {
	if (!handle)
		return;
	unregister_handle(handle);
	delete handle;
}
} // extern "C"

#ifdef DETENT_READER_TEST_HOOKS
namespace detent::reader::testing {
void install(void (*hook)(Window, void *) noexcept, void *context) noexcept {
	window_context = context;
	window_hook = hook;
}
const char *last_failure() noexcept { return failure_text.c_str(); }
Stats stats(detent_reader *handle) noexcept {
	constexpr auto relaxed = std::memory_order_relaxed;
	Stats stats{handle->live_views(), handle->publishes.load(relaxed), handle->refused.load(relaxed),
	            handle->attached.load(relaxed), handle->generations.load(relaxed), handle->current.load(relaxed)};
	static_assert(std::size(Stats{}.pins) == ring_size);
	for (std::size_t k = 0; k != ring_size; ++k)
		stats.pins[k] = handle->ring[k].sum();
	return stats;
}
} // namespace detent::reader::testing
#endif
