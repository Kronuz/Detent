#include "detent/client.hh"
#include "detent/detail/file.hh"
#include "detent/hashing.hh"
#include "detent/json_patch.hh"
#include "detent/json_schema.hh"
#include "detent/validation.hh"

#include <random.hh>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace detent {

namespace {

std::int64_t steady_ms_now() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
	           std::chrono::steady_clock::now().time_since_epoch())
	    .count();
}

const char* category_name(ErrorCategory c) noexcept {
	switch (c) {
		case ErrorCategory::Uninitialized: return "Uninitialized";
		case ErrorCategory::InvalidName: return "InvalidName";
		case ErrorCategory::MissingKnob: return "MissingKnob";
		case ErrorCategory::WrongType: return "WrongType";
		case ErrorCategory::ConversionFailed: return "ConversionFailed";
	}
	return "Unknown";
}

// Section 9 logging rules: panic messages include the knob name and error
// category, never hash_val/switch_val or snapshot contents.
[[noreturn]] void panic(const KnobError& err) {
	std::cerr << "detent: fatal: " << category_name(err.category)
	          << " for knob '" << err.name << "'";
	if (!err.detail.empty()) std::cerr << " (" << err.detail << ")";
	std::cerr << std::endl;
	std::abort();
}


} // namespace

std::string KnobError::what() const {
	std::string s = category_name(category);
	s += ": ";
	s += name;
	if (!detail.empty()) {
		s += " (";
		s += detail;
		s += ")";
	}
	return s;
}

Client::Client(InitOptions options)
	: max_knobs_(options.max_knobs)
	, source_id_(options.source_id)
	, refresh_alert_after_(options.refresh_alert_after)
	, on_update_(options.on_update) {
	if (options.poll_interval <= std::chrono::milliseconds::zero()) {
		throw std::invalid_argument("detent: poll_interval must be positive");
	}
	// 1. Optional baked defaults. An explicitly configured file must be valid.
	if (!options.defaults_path.empty()) {
		std::string text;
		try {
			text = detail::read_file(options.defaults_path, kDefaultMaxBytes);
		} catch (const std::exception& e) {
			throw std::runtime_error(std::string("detent: defaults file: ") + e.what());
		}
		auto parsed = parse_defaults(text, "defaults", max_knobs_);
		if (!parsed) {
			throw std::runtime_error("detent: invalid defaults file: " + parsed.error().detail);
		}
		defaults_ = std::move(parsed).value();
	}

	// 2. CLI overrides: startup-fatal, names offending entry explicitly.
	auto overrides = parse_cli_overrides(options.cli_overrides);
	if (!overrides) {
		throw std::runtime_error("detent: " + overrides.error());
	}
	if (!overrides.value().values.empty()) {
		override_table_ = std::make_shared<const OverrideTable>(std::move(overrides).value());
	}

	// Section 11's "combining X with Y is a startup error" pattern,
	// generalized: exactly one KnobSource stream per client instance.
	if (options.source_path && options.source_poll) {
		throw std::runtime_error("detent: source_path and source_poll are mutually exclusive");
	}

	// 3. Publish v0 before starting any source watcher (Section 4 startup contract).
	current_.store(std::make_shared<const Snapshot>(defaults_));
	accepted_version_.store(0);
	last_success_steady_ms_.store(steady_ms_now());

	// 4. Single background updater, only if a source is configured. Source
	// health starts false; the loop flips it once the target is actually
	// reachable (Section 9: "Source unavailable at startup -> serve version-0
	// defaults; log and increment refresh failure").
	if (options.source_path) {
		updater_thread_.emplace(
			[this, path = *options.source_path, interval = options.poll_interval](std::stop_token st) {
				run_file_source(st, path, interval);
			});
	} else if (options.source_poll) {
		updater_thread_.emplace(
			[this, poll = *options.source_poll, interval = options.poll_interval](std::stop_token st) {
				run_generic_source(st, poll, interval);
			});
	}
}

Client::~Client() {
	if (updater_thread_) {
		updater_thread_->request_stop();
		// std::jthread's destructor joins; request_stop() just wakes it sooner
		// than the next poll_interval tick.
	}
}

Client::Client(Snapshot in_memory_snapshot)
	: max_knobs_(in_memory_snapshot.knobs.size())
	, source_id_("test")
	, refresh_alert_after_(std::chrono::minutes{5}) {
	defaults_ = in_memory_snapshot;
	current_.store(std::make_shared<const Snapshot>(std::move(in_memory_snapshot)));
	accepted_version_.store(0);
	last_success_steady_ms_.store(steady_ms_now());
}

Result<bool, KnobError> Client::eval(
	std::string_view name,
	std::optional<std::string_view> hash_val,
	std::optional<std::string_view> switch_val) const {
	if (!is_valid_knob_name(name)) {
		return KnobError{ErrorCategory::InvalidName, std::string(name), "malformed knob name"};
	}

	// Step 2: CLI override, cheapest path -- one shared_ptr null-check.
	if (override_table_) {
		auto it = override_table_->values.find(name);
		if (it != override_table_->values.end()) {
			if (const auto* b = std::get_if<bool>(&it->second)) {
				metrics_.reads_ok.fetch_add(1, std::memory_order_relaxed);
				return *b;
			}
			metrics_.reads_wrong_type.fetch_add(1, std::memory_order_relaxed);
			return KnobError{ErrorCategory::WrongType, std::string(name), "override is int; eval() requires bool"};
		}
	}

	// Step 3: one atomic acquire, held for the whole call via `snap`.
	auto snap = current_.load();
	auto it = snap->knobs.find(name);
	if (it == snap->knobs.end()) {
		metrics_.reads_missing.fetch_add(1, std::memory_order_relaxed);
		return KnobError{ErrorCategory::MissingKnob, std::string(name), "not present in current snapshot"};
	}

	const auto* def = std::get_if<BoolDef>(&it->second);
	if (def == nullptr) {
		metrics_.reads_wrong_type.fetch_add(1, std::memory_order_relaxed);
		return KnobError{ErrorCategory::WrongType, std::string(name), "knob is int; eval() requires bool"};
	}

	// Section 8 precedence: group match, then rollout, then default.
	if (switch_val) {
		auto git = def->overrides.find(*switch_val);
		if (git != def->overrides.end()) {
			metrics_.reads_ok.fetch_add(1, std::memory_order_relaxed);
			if (const auto* forced = std::get_if<bool>(&git->second)) return *forced;
			return hash_val ? stable_bucket(name, *hash_val) < std::get<GroupRollout>(git->second).rollout_pct
			                : def->default_value;
		}
	}
	if (def->rollout_pct && hash_val) {
		auto bucket = stable_bucket(name, *hash_val);
		metrics_.reads_ok.fetch_add(1, std::memory_order_relaxed);
		return bucket < *def->rollout_pct;
	}
	metrics_.reads_ok.fetch_add(1, std::memory_order_relaxed);
	return def->default_value;
}

Result<std::int64_t, KnobError> Client::get(
	std::string_view name,
	std::optional<std::string_view> /*switch_val, reserved and ignored in v1*/) const {
	if (!is_valid_knob_name(name)) {
		return KnobError{ErrorCategory::InvalidName, std::string(name), "malformed knob name"};
	}

	if (override_table_) {
		auto it = override_table_->values.find(name);
		if (it != override_table_->values.end()) {
			if (const auto* v = std::get_if<std::int64_t>(&it->second)) {
				metrics_.reads_ok.fetch_add(1, std::memory_order_relaxed);
				return *v;
			}
			metrics_.reads_wrong_type.fetch_add(1, std::memory_order_relaxed);
			return KnobError{ErrorCategory::WrongType, std::string(name), "override is bool; get() requires int"};
		}
	}

	auto snap = current_.load();
	auto it = snap->knobs.find(name);
	if (it == snap->knobs.end()) {
		metrics_.reads_missing.fetch_add(1, std::memory_order_relaxed);
		return KnobError{ErrorCategory::MissingKnob, std::string(name), "not present in current snapshot"};
	}

	const auto* def = std::get_if<IntDef>(&it->second);
	if (def == nullptr) {
		metrics_.reads_wrong_type.fetch_add(1, std::memory_order_relaxed);
		return KnobError{ErrorCategory::WrongType, std::string(name), "knob is bool; get() requires int"};
	}

	metrics_.reads_ok.fetch_add(1, std::memory_order_relaxed);
	return def->default_value;
}

bool Client::eval_or_panic(
	std::string_view name,
	std::optional<std::string_view> hash_val,
	std::optional<std::string_view> switch_val) const {
	auto r = eval(name, hash_val, switch_val);
	if (!r) panic(r.error());
	return r.value();
}

std::int64_t Client::get_or_panic(std::string_view name, std::optional<std::string_view> switch_val) const {
	auto r = get(name, switch_val);
	if (!r) panic(r.error());
	return r.value();
}

HealthSnapshot Client::health() const noexcept {
	auto snap = current_.load();
	HealthSnapshot h;
	h.initialized = true;
	h.source_connected = metrics_.source_connected.load(std::memory_order_relaxed);
	h.accepted_version = accepted_version_.load(std::memory_order_relaxed);
	auto now_ms = steady_ms_now();
	auto last_ms = last_success_steady_ms_.load(std::memory_order_relaxed);
	h.staleness_seconds = static_cast<double>(now_ms - last_ms) / 1000.0;
	h.bool_knobs = snap->bool_count();
	h.int_knobs = snap->int_count();
	h.cli_overrides = override_table_ ? override_table_->values.size() : 0;
	return h;
}

// Section 10 "default: file watcher", simplified to portable polling (see
// README/AGENTS notes: no cross-platform inotify/FSEvents wrapper exists yet
// in this ecosystem). Publishers still atomically rename a fully-written
// sibling file over the target, so a full read after a change never
// observes a torn write; the poll cadence is the coalescing window in place
// of Section 10's literal 100ms event debounce.
void Client::run_file_source(std::stop_token stop, std::filesystem::path path, std::chrono::milliseconds poll_interval) {
	std::filesystem::file_time_type last_mtime{};
	std::uintmax_t last_size = 0;
	bool have_baseline = false;

	while (!stop.stop_requested()) {
		std::error_code ec;
		auto mtime = std::filesystem::last_write_time(path, ec);
		auto size = ec ? 0 : std::filesystem::file_size(path, ec);
		if (ec) {
			metrics_.refresh_failures.fetch_add(1, std::memory_order_relaxed);
			metrics_.source_connected.store(false, std::memory_order_relaxed);
		} else {
			metrics_.source_connected.store(true, std::memory_order_relaxed);
			// Section 7: "Parse/schema errors are not retried until a new
			// source event or file change arrives" -- gate the read on
			// mtime *or size* changing since the last poll (not since the
			// last *successful* swap, which was the bug: a rejected update
			// never advanced this baseline, so a persistently-malformed
			// file got re-read and re-parsed every single poll forever,
			// contrary to the spec). mtime-only was also insufficient on
			// its own: a publisher that reuses an exact timestamp across
			// an atomic rename (confirmed: same last_write_time for v1 and
			// v2) left the client stuck on the old version indefinitely.
			// mtime-or-size together catch both real classes of "the file
			// changed" without re-reading unconditionally on every poll.
			if (!have_baseline || mtime != last_mtime || size != last_size) {
				last_mtime = mtime;
				last_size = size;
				have_baseline = true;
				try {
					auto bytes = detail::read_file(path, kDefaultMaxBytes);
					try_apply_source_bytes(bytes);
				} catch (const std::exception&) {
					metrics_.refresh_failures.fetch_add(1, std::memory_order_relaxed);
				}
			}
		}

		// Sleep in small slices so request_stop() is observed promptly.
		sleep_with_stop_check(stop, poll_interval);
	}
}

void Client::sleep_with_stop_check(std::stop_token stop, std::chrono::milliseconds duration) {
	auto slice = std::min(duration, std::chrono::milliseconds{50});
	for (auto waited = std::chrono::milliseconds{0}; waited < duration && !stop.stop_requested(); waited += slice) {
		std::this_thread::sleep_for(slice);
	}
}

// Section 10's KnobSource contract, generalized to a plain polling function
// instead of a full trait object: "the source emits complete snapshots...
// The client, not the adapter, owns parsing, version checks, merging,
// metrics, backoff, and publication." source_poll() only has to make one
// attempt and report bytes / no-change / failure; this loop owns everything
// else, including the Section 7 "full-jitter exponential backoff: random
// delay in [0, min(30s, 250ms x 2^attempt)]" reconnect rule on failures.
void Client::run_generic_source(std::stop_token stop, InitOptions::SourcePoll source_poll,
                                 std::chrono::milliseconds poll_interval) {
	int attempt = 0;

	while (!stop.stop_requested()) {
		try {
			auto payload = source_poll(accepted_version_.load(std::memory_order_relaxed));
			metrics_.source_connected.store(true, std::memory_order_relaxed);
			if (payload) {
				// Section 7: "Reset attempt count after the next valid
				// swap" -- only on an actual accepted swap, not merely
				// because source_poll() didn't throw. A payload that fails
				// to parse/validate, or that fails the version gate, is
				// not a "valid swap"; resetting attempt=0 for those too
				// (the previous behavior) meant a source that kept
				// returning the *same* rejected/malformed bytes never
				// actually backed off at all.
				bool applied = payload->is_patch ? try_apply_source_patch(payload->bytes)
				                                  : try_apply_source_bytes(payload->bytes);
				if (applied) {
					attempt = 0;
				}
			} else {
				// No new data (e.g. a 304): a successful, connected poll
				// with nothing to apply. Per the SourcePoll contract this
				// is "not an error, no backoff", but it is also not "a
				// valid swap" per Section 7's literal reset condition, so
				// attempt is left as-is -- consistent with the malformed-
				// payload case above rather than a special exception for it.
			}
			sleep_with_stop_check(stop, poll_interval);
		} catch (const std::exception&) {
			metrics_.refresh_failures.fetch_add(1, std::memory_order_relaxed);
			metrics_.source_connected.store(false, std::memory_order_relaxed);
			auto cap = std::chrono::milliseconds{30'000};
			// Multiply by a std::chrono::milliseconds::rep-typed scalar, not a
			// bare `1LL`: on platforms where `long long` and the duration's
			// rep type differ (e.g. Linux/LP64, where int64_t is `long`),
			// `milliseconds * (1LL << n)` produces a *different* duration
			// specialization than `cap`, and std::min<T> requires identical
			// types -- this only failed to compile on Linux, not macOS.
			using Rep = std::chrono::milliseconds::rep;
			auto multiplier = static_cast<Rep>(1) << std::min(attempt, 20);
			auto backoff = std::min(std::chrono::milliseconds{250} * multiplier, cap);
			++attempt;
			auto jittered = random_time(std::chrono::milliseconds{0}, backoff);
			sleep_with_stop_check(stop, jittered);
		}
	}
}

bool Client::try_apply_source_bytes(std::string_view bytes) {
	auto last_version = accepted_version_.load(std::memory_order_relaxed);
	auto parsed = parse_source_snapshot(bytes, source_id_, max_knobs_);
	if (!parsed) {
		metrics_.refresh_failures.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	auto& snap = parsed.value();
	if (snap.version <= last_version) {
		metrics_.stale_updates.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	live_snapshot_ = snap; // kept so a LATER patch has the correct base to merge onto
	auto merged = merge_over_defaults(defaults_, snap);
	auto published = std::make_shared<const Snapshot>(std::move(merged));
	current_.store(published);
	accepted_version_.store(snap.version, std::memory_order_relaxed);
	last_success_steady_ms_.store(steady_ms_now(), std::memory_order_relaxed);
	metrics_.snapshot_swaps.fetch_add(1, std::memory_order_relaxed);
	if (on_update_) (*on_update_)(*published);
	return true;
}

bool Client::try_apply_source_patch(std::string_view patch_bytes) {
	auto last_version = accepted_version_.load(std::memory_order_relaxed);
	auto parsed_patch = parse_snapshot_patch(patch_bytes, source_id_, max_knobs_);
	if (!parsed_patch) {
		metrics_.refresh_failures.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	auto& patch = parsed_patch.value();
	if (patch.version <= last_version) {
		metrics_.stale_updates.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	// Merges onto live_snapshot_ -- this client's OWN last-accepted live
	// state, never anything the source claims as a base -- so a source
	// that's confused about what this client last had produces a wrong (but
	// self-consistent) merge at worst, never trusts a network peer's word
	// for it (same posture as the version-number gate above, generalized to
	// the merge base).
	auto applied = apply_patch(live_snapshot_, patch, max_knobs_);
	if (!applied) {
		metrics_.refresh_failures.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	live_snapshot_ = applied.value();
	auto merged = merge_over_defaults(defaults_, applied.value());
	auto published = std::make_shared<const Snapshot>(std::move(merged));
	current_.store(published);
	accepted_version_.store(patch.version, std::memory_order_relaxed);
	last_success_steady_ms_.store(steady_ms_now(), std::memory_order_relaxed);
	metrics_.snapshot_swaps.fetch_add(1, std::memory_order_relaxed);
	if (on_update_) (*on_update_)(*published);
	return true;
}

} // namespace detent
