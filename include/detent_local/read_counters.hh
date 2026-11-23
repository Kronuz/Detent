#pragma once
#include <cstdint>

// First-read and lookup counters (docs/mechanisms/read-counters.md). With
// DETENT_READ_COUNTERS undefined, DETENT_COUNT expands to nothing and
// snapshot() is all zeros: no cell, no instruction, no argument evaluation.
#ifdef DETENT_READ_COUNTERS
#include <atomic>
#define DETENT_COUNT(field, amount)                                                                          \
	(void)::detent::local::counters::cells().field.fetch_add(static_cast<std::uint64_t>(amount),               \
	                                                         std::memory_order_relaxed)
#else
#define DETENT_COUNT(field, amount) ((void)0)
#endif

namespace detent::local::counters {
struct Snapshot {
	std::uint64_t directory_trust = 0; // open_publisher_directory calls
	std::uint64_t record_opens = 0;    // generation records opened (selected, lease, image, metadata, ...)
	std::uint64_t table_bytes = 0;     // bytes read from generation records
	std::uint64_t segment_opens = 0;   // segment payload files opened for mapping
	std::uint64_t pages_verified = 0;  // pages checked by SHA-256 at first touch
	std::uint64_t bytes_hashed = 0;    // bytes fed to SHA-256 on the read path
	std::uint64_t namespaces_attached = 0; // reader core: namespace generations attached
	std::uint64_t refused_publishes = 0;   // reader core: publishes refused by a pinned slot
	bool operator==(const Snapshot &) const = default;
};
inline constexpr bool enabled =
#ifdef DETENT_READ_COUNTERS
    true;
#else
    false;
#endif
#ifdef DETENT_READ_COUNTERS
struct Cells {
	std::atomic<std::uint64_t> directory_trust{0}, record_opens{0}, table_bytes{0}, segment_opens{0},
	    pages_verified{0}, bytes_hashed{0}, namespaces_attached{0}, refused_publishes{0};
};
inline Cells &cells() noexcept {
	static Cells value;
	return value;
}
inline Snapshot snapshot() noexcept {
	auto &c = cells();
	constexpr auto order = std::memory_order_relaxed;
	return {c.directory_trust.load(order), c.record_opens.load(order), c.table_bytes.load(order),
	        c.segment_opens.load(order),   c.pages_verified.load(order), c.bytes_hashed.load(order),
	        c.namespaces_attached.load(order), c.refused_publishes.load(order)};
}
inline void reset() noexcept {
	auto &c = cells();
	constexpr auto order = std::memory_order_relaxed;
	c.directory_trust.store(0, order);
	c.record_opens.store(0, order);
	c.table_bytes.store(0, order);
	c.segment_opens.store(0, order);
	c.pages_verified.store(0, order);
	c.bytes_hashed.store(0, order);
	c.namespaces_attached.store(0, order);
	c.refused_publishes.store(0, order);
}
#else
inline Snapshot snapshot() noexcept { return {}; }
inline void reset() noexcept {}
#endif
inline Snapshot operator-(const Snapshot &a, const Snapshot &b) noexcept {
	return {a.directory_trust - b.directory_trust, a.record_opens - b.record_opens,
	        a.table_bytes - b.table_bytes,         a.segment_opens - b.segment_opens,
	        a.pages_verified - b.pages_verified,   a.bytes_hashed - b.bytes_hashed,
	        a.namespaces_attached - b.namespaces_attached, a.refused_publishes - b.refused_publishes};
}
} // namespace detent::local::counters
