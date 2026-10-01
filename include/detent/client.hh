#pragma once

// Sections 5-9: the instance read/update API. Section 6's read-path pseudocode
// is followed almost line for line: validate name -> check CLI override ->
// acquire snapshot -> lookup -> type-check -> evaluate -> count -> return. No
// lock, allocation, or I/O sits on that path; the only heap traffic is the
// shared_ptr refcount bump/drop already implied by SnapshotRef::load().

#include "detent/errors.hh"
#include "detent/model.hh"
#include "detent/overrides.hh"
#include "detent/result.hh"
#include "detent/snapshot_ref.hh"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <utility>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace detent {

// Section 12: bounded-cardinality counters/gauges. No knob name, hash_val,
// switch_val, path, client ID, or version ever becomes a label -- these are
// plain process-wide counters, not a metrics-library integration.
struct Metrics {
	std::atomic<std::uint64_t> reads_ok{0};
	std::atomic<std::uint64_t> reads_missing{0};
	std::atomic<std::uint64_t> reads_wrong_type{0};
	std::atomic<std::uint64_t> reads_conversion_failed{0};
	std::atomic<std::uint64_t> snapshot_swaps{0};
	std::atomic<std::uint64_t> refresh_failures{0};
	std::atomic<std::uint64_t> stale_updates{0};
	std::atomic<bool> source_connected{false};
};

// Section 12: "Health snapshot" diagnostic payload. Never exposes the live
// map itself -- only copied metadata and counts.
struct HealthSnapshot {
	bool initialized = false;
	bool source_connected = false;
	std::uint64_t accepted_version = 0;
	double staleness_seconds = 0.0;
	std::size_t bool_knobs = 0;
	std::size_t int_knobs = 0;
	std::size_t cli_overrides = 0;
};

struct InitOptions {
	std::filesystem::path defaults_path;                  // optional; empty starts with no knobs
	std::vector<std::string> cli_overrides;                // "name=value" pairs
	std::optional<std::filesystem::path> source_path;      // file source, if any

	// A candidate update from a generic source: either a full snapshot's
	// bytes, or a partial-update ("patch") payload -- see json_patch.hh --
	// representing only the knobs that changed since some earlier version.
	// A patch is applied onto the client's own last-accepted LIVE snapshot
	// (not the merged-over-defaults one readers see), never onto whatever
	// the source claims its own base was, so a source's PATCH support is
	// purely a bandwidth optimization: worst case (the source is confused
	// about what this client last had) is a rejected/stale update, exactly
	// like a full snapshot with a stale version -- never a silently wrong
	// merge, because Client itself owns and never trusts a network peer's
	// idea of "what you already have" any more than it already does for a
	// full snapshot's version number.
	struct SourcePayload {
		std::string bytes;
		bool is_patch = false;
	};

	// A generic pluggable source, the extension seam Section 10 describes for
	// "any publisher that emits valid snapshots" -- e.g. detent_http's
	// make_http_poll_source(). Mutually exclusive with source_path (Section
	// 11's "combining X with Y is a startup error", generalized): the client
	// consumes exactly one KnobSource stream per instance (Section 3).
	//
	// Called repeatedly on the single background updater thread with the
	// currently accepted version (so a source that can serve a diff-since-
	// that-version -- e.g. detentd's cached patch, see detentd.hh -- has
	// what it needs to decide whether to). Contract:
	//   - returns a SourcePayload  -> a candidate update to validate/apply;
	//                                 `is_patch` says which grammar it is
	//   - returns std::nullopt     -> no new data yet (e.g. a 304/timeout);
	//                                 not an error, no backoff
	//   - throws                   -> a transport/connection failure; Client
	//                                 counts it as a refresh failure and
	//                                 applies full-jitter backoff before the
	//                                 next call (Section 7's backoff rule)
	using SourcePoll = std::function<std::optional<SourcePayload>(std::uint64_t after_version)>;
	std::optional<SourcePoll> source_poll;
	std::string source_id = "file";
	std::chrono::milliseconds poll_interval{200};           // positive base poll cadence (file or generic source)
	std::chrono::milliseconds refresh_alert_after{std::chrono::minutes{5}};
	std::size_t max_knobs = 100'000;

	// Invoked synchronously on the background updater thread immediately
	// after a newly accepted snapshot is published (i.e. right after the
	// same atomic store every read already observes) -- not a generic
	// pub/sub bus, just a single hook for a process that needs to react to
	// "the live snapshot changed" without polling accepted_version() itself.
	// Must be fast and must not throw (exceptions here are not caught the
	// way source_poll's are); detent-agent uses this to publish a
	// pre-serialized copy into shared memory for other local processes to
	// read without any per-call work on this process at all.
	using OnUpdate = std::function<void(const Snapshot&)>;
	std::optional<OnUpdate> on_update;
};

class Client {
public:
	// Throws std::runtime_error on invalid/missing defaults or CLI overrides
	// (Section 9: "Fail init(); process integration SHOULD abort startup").
	explicit Client(InitOptions options);

	// Test-only in-memory construction (Section 5 "Test-only client"): no
	// file, no CLI overrides, no background updater. Used by
	// detent::testing::TestKnobs.
	explicit Client(Snapshot in_memory_snapshot);

	~Client();

	Client(const Client&) = delete;
	Client& operator=(const Client&) = delete;

	// --- Section 6/8: read path -------------------------------------------------
	[[nodiscard]] Result<bool, KnobError> eval(
		std::string_view name,
		std::optional<std::string_view> hash_val = std::nullopt,
		std::optional<std::string_view> switch_val = std::nullopt) const;

	[[nodiscard]] Result<std::int64_t, KnobError> get(
		std::string_view name,
		std::optional<std::string_view> switch_val = std::nullopt) const;

	template <typename T>
	[[nodiscard]] Result<T, KnobError> get_as(
		std::string_view name,
		std::optional<std::string_view> switch_val = std::nullopt) const {
		auto r = get(name, switch_val);
		if (!r) return r.error();
		auto v = r.value();
		// std::in_range<T>, not a hand-rolled bounds check narrowed into
		// int64_t: the previous `v > static_cast<int64_t>(numeric_limits<T>
		// ::max())` was flatly wrong for any T whose max() doesn't fit in
		// int64_t (e.g. T=uint64_t: max() cast to int64_t wraps to -1,
		// rejecting every non-negative value including in-range ones --
		// confirmed by review: get_as<uint64_t> on value 25 returned
		// ConversionFailed). std::in_range handles every signed/unsigned
		// target type correctly without lossy narrowing.
		if (!std::in_range<T>(v)) {
			metrics_.reads_conversion_failed.fetch_add(1, std::memory_order_relaxed);
			return KnobError{ErrorCategory::ConversionFailed, std::string(name), "value does not fit target type"};
		}
		return static_cast<T>(v);
	}

	// Section 5: "*_or_panic ... crash on any KnobError". No call-site
	// fallback API exists anywhere in this class.
	[[nodiscard]] bool eval_or_panic(
		std::string_view name,
		std::optional<std::string_view> hash_val = std::nullopt,
		std::optional<std::string_view> switch_val = std::nullopt) const;

	[[nodiscard]] std::int64_t get_or_panic(
		std::string_view name,
		std::optional<std::string_view> switch_val = std::nullopt) const;

	// The live snapshot itself, not an evaluated value -- for a consumer
	// that needs the whole thing (e.g. detent-agent's shared-memory
	// publisher, seeding its initial publish before any source update has
	// landed yet, since InitOptions::on_update only fires on subsequent
	// accepted updates, not for the baked-defaults snapshot set at
	// construction). Ordinary read-path consumers should use eval()/get(),
	// not this.
	[[nodiscard]] std::shared_ptr<const Snapshot> current_snapshot() const { return current_.load(); }

	[[nodiscard]] HealthSnapshot health() const noexcept;

	const Metrics& metrics() const noexcept { return metrics_; }

private:
	void run_file_source(std::stop_token stop, std::filesystem::path path,
	                      std::chrono::milliseconds poll_interval);
	void run_generic_source(std::stop_token stop, InitOptions::SourcePoll source_poll,
	                         std::chrono::milliseconds poll_interval);
	bool try_apply_source_bytes(std::string_view bytes);
	// Applies a patch (see json_patch.hh) onto live_snapshot_ -- the
	// client's own last-accepted LIVE (pre-merge-over-defaults) snapshot,
	// never the source's claimed base -- then re-merges over defaults_ and
	// publishes, exactly like try_apply_source_bytes's full-replace path.
	// Returns false (no swap, Section 7's backoff-eligible "not a valid
	// swap") on a stale version or a patch that fails to parse/apply.
	bool try_apply_source_patch(std::string_view patch_bytes);
	void sleep_with_stop_check(std::stop_token stop, std::chrono::milliseconds duration);

	Snapshot defaults_;             // baked, version 0; never swapped
	// The last-accepted LIVE (source) snapshot, pre-merge-over-defaults --
	// distinct from current_ (which readers see, and IS the merged form).
	// Only the single background updater thread ever touches this (no
	// concurrent readers of it, unlike current_), so it needs no atomic/
	// lock of its own. Exists so a patch has something correct to merge
	// onto: merging a patch directly onto current_ (the merged snapshot)
	// would be wrong -- deleting a live-overlaid knob must fall back to its
	// baked default, not disappear from the merged view entirely, and only
	// the live (non-merged) snapshot can distinguish "overlaid" from
	// "answered by defaults_ alone". Starts as a version-0, empty-knobs
	// Snapshot, matching try_apply_source_bytes's very first full accept
	// (merge_over_defaults(defaults_, {}) == defaults_ exactly).
	Snapshot live_snapshot_;
	SnapshotRef current_;           // RCU-published merged snapshot
	std::shared_ptr<const OverrideTable> override_table_; // set once at init, then read-only
	std::size_t max_knobs_;
	std::string source_id_;
	std::chrono::milliseconds refresh_alert_after_;

	mutable Metrics metrics_;
	std::atomic<std::uint64_t> accepted_version_{0};
	// steady_clock, not epoch::now()/system_clock: staleness_seconds is a
	// duration, and system_clock can jump backward on an NTP correction,
	// which would make staleness_seconds negative -- a monotonic clock is
	// the correct primitive for "how long since the last successful
	// update", independent of what wall-clock time itself does.
	std::atomic<std::int64_t> last_success_steady_ms_{0};

	std::optional<std::jthread> updater_thread_;
	// Set once at construction from InitOptions::on_update, then only ever
	// read (never reassigned) from the single background updater thread --
	// no lock/atomic needed. Invoked after every successfully accepted
	// update (full or patch), never for the initial baked-defaults publish
	// (see current_snapshot()'s comment for why a caller that needs that
	// too calls it explicitly right after construction).
	std::optional<InitOptions::OnUpdate> on_update_;
};

} // namespace detent
