#include "detentd.hh"

#include "detent/json_patch.hh"
#include "detent/detail/file.hh"
#include "raft_delegate.hh"

#include <bus.h>
#include <length.h>

#include <asio.hpp>
#include <http_conditional.h>
#include <http_compression.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iterator>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <thread>

namespace detentd {

namespace {

int status_for(detent::UpdateRejectReason reason) {
	switch (reason) {
		case detent::UpdateRejectReason::Version: return 409; // stale/duplicate
		case detent::UpdateRejectReason::Oversize: return 413;
		case detent::UpdateRejectReason::Transport: return 503; // no quorum decision within the wait timeout
		default: return 400; // Io/Parse/Schema/Checksum
	}
}

std::string json_escape(std::string_view s) {
	std::string out;
	out.reserve(s.size());
	for (char c : s) {
		out += (c == '"' || c == '\\') ? '\'' : c;
	}
	return out;
}

} // namespace

Distributor::Distributor(Options options) : options_(std::move(options)) {
	max_diff_history_ = options_.max_diff_history;

	router_.route("PUT", "/v1/snapshot",
	              [this](const http::Request& q, http::ResponseWriter& r, const http::Params& p) {
		              route_put_snapshot(q, r, p);
	              });
	router_.route("PATCH", "/v1/snapshot",
	              [this](const http::Request& q, http::ResponseWriter& r, const http::Params& p) {
		              route_patch_snapshot(q, r, p);
	              });
	router_.route("GET", "/v1/snapshot",
	              [this](const http::Request& q, http::ResponseWriter& r, const http::Params& p) {
		              route_get_snapshot(q, r, p);
	              });
	router_.route("GET", "/v1/health",
	              [this](const http::Request& q, http::ResponseWriter& r, const http::Params& p) {
		              route_get_health(q, r, p);
	              });

	load_persisted();

	if (options_.cluster) {
		const auto& c = *options_.cluster;
		bus_ = std::make_unique<cluster::Bus>(
			1, 0, c.token, kMaxRaftWireType,
			[this](int wire_type, std::string_view content, const asio::ip::udp::endpoint& /*from*/) {
				on_bus_message(wire_type, content);
			});
		reactor::UdpOptions udp_opts;
		udp_opts.multicast_group = c.multicast_group;
		udp_opts.multicast_interface = c.multicast_interface;
		// Same-host multi-process cluster (our test/demo setup) needs loopback
		// delivery; a real multi-host deployment would leave this false.
		udp_opts.multicast_loop = true;
		bus_->set_options(udp_opts);

		// The restart-safety silence window only protects against a stale
		// vote from a PRIOR run; a genuine first launch has none, so it
		// gets zero silence instead of the usual 3x election_max. A marker
		// file (not Raft state) tells the two apart -- see CLUSTERING.md.
		auto marker_path = options_.persist_path;
		marker_path += ".cluster_marker";
		bool has_run_before = std::filesystem::exists(marker_path);
		if (!has_run_before) {
			std::ofstream(marker_path).close();
		}
		auto startup_silence = has_run_before
			? std::chrono::duration_cast<std::chrono::steady_clock::duration>(
				  std::chrono::duration<double>(3.0 * c.raft_config.election_max))
			: std::chrono::steady_clock::duration::zero();
		// A peer not heard from (any Raft message) within this window is
		// treated as dead (DetentRaftDelegate::is_alive) -- a multiple of
		// heartbeat_timeout tolerates ordinary jitter, not just one miss.
		auto liveness_timeout = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<double>(4.0 * c.raft_config.heartbeat_timeout));
		raft_delegate_ = std::make_unique<DetentRaftDelegate>(
			*this, *bus_, c.self_address, c.peers, startup_silence, liveness_timeout);
		raft_ = std::make_unique<cluster::Raft<RaftNode>>(bus_->io().get_executor(), c.raft_config, raft_delegate_.get());

		bus_->start(c.multicast_port);
		raft_->start(); // "Call once the transport is up" -- raft.h's own start() contract
	}
}

Distributor::~Distributor() {
	// bus_'s reactor thread calls into raft_ via on_bus_message(); stop it
	// (joins the thread) before raft_/raft_delegate_ destruct, or the
	// thread can still be running while raft_ is torn down.
	if (bus_) bus_->stop();
}

void Distributor::handle(const http::Request& request, http::ResponseWriter& response) {
	requests_served_.fetch_add(1, std::memory_order_relaxed);
	router_.handle(request, response);
}

bool Distributor::should_offload(const http::Request& request) const {
	// GET handlers never block (in-memory read under a shared lock); running
	// them inline on the reactor avoids the offload round trip entirely and
	// is what lets a poll-based fleet scale on a handful of reactor threads.
	// PUT/PATCH do file I/O, validation, and (when clustered) BLOCK waiting
	// on Raft consensus (submit_command()'s pending_cv_.wait_for, up to
	// kCommandWaitTimeout) -- definitely not reactor-safe, so they keep the
	// default off-reactor path.
	return request.method != "GET";
}

namespace {

// Caps a PUT body at kMaxPutBodyBytes as bytes arrive, instead of letting the
// framework's default buffering accept an arbitrary Content-Length in full
// before json_schema's own size check ever gets a chance to run. Throwing
// from write() propagates out through the connection's body-feed loop and
// is caught by serve_connection's outer handler, which drops the connection
// -- no polite 413 (the sink API has no such signal), but the point is
// bounding memory, and an abrupt close is a normal, defensible response to
// a client sending far more than any legitimate publisher would.
constexpr std::size_t kMaxPutBodyBytes = 16u * 1024 * 1024; // matches json_schema's kDefaultMaxBytes

class CappedBodySink : public http::BodySink {
public:
	CappedBodySink(std::string& target, std::size_t max_bytes) : target_(target), max_bytes_(max_bytes) {}
	void write(std::string_view chunk) override {
		if (target_.size() + chunk.size() > max_bytes_) {
			throw std::runtime_error("detent distributor: request body exceeds size limit");
		}
		target_.append(chunk);
	}
	void end() override {}

private:
	std::string& target_;
	std::size_t max_bytes_;
};

} // namespace

std::unique_ptr<http::BodySink> Distributor::on_request_body(http::Request& request) {
	if (request.method == "PUT" || request.method == "PATCH") {
		return std::make_unique<CappedBodySink>(request.body, kMaxPutBodyBytes);
	}
	return nullptr; // GET/health: no meaningful body; default (empty) buffering is fine
}

std::uint64_t Distributor::current_version() const {
	std::shared_lock lock(snapshot_mutex_);
	return current_version_;
}

detent::Result<std::uint64_t, detent::UpdateError> Distributor::resolve_and_commit(SnapshotResolver resolve) {
	std::uint64_t accepted_version = 0;
	{
		std::unique_lock lock(snapshot_mutex_);
		// Resolving WHILE this lock is held (not before acquiring it) is
		// what makes a PATCH's merge race-free against a concurrent PUT or
		// PATCH: it always merges onto the most recently committed state,
		// never a state read moments earlier under a since-released lock
		// -- see resolve_and_commit's declaration comment (detentd.hh).
		auto resolved = resolve(current_snapshot_, has_snapshot_);
		if (!resolved) return resolved.error();
		auto new_snapshot = std::move(resolved).value();
		if (has_snapshot_ && new_snapshot.version <= current_version_) {
			return detent::UpdateError{detent::UpdateRejectReason::Version, "stale or duplicate version"};
		}
		auto new_bytes = detent::serialize_snapshot(new_snapshot);
		// Persist *before* committing the in-memory state, not after: the
		// previous order updated current_bytes_/current_version_/
		// has_snapshot_ first and persisted second, so a persist failure
		// (confirmed: std::filesystem::rename() throws on a missing/
		// unwritable persist directory) threw out of this function with
		// the live state already mutated -- the HTTP layer then answered
		// the PUT with 500, while GET/health had already started serving
		// the "accepted" update. persist_locked() runs before any of
		// current_bytes_/current_version_/current_snapshot_ is touched, so
		// on failure the exception propagates with the in-memory state
		// completely unchanged, matching the 500 the caller sees.
		persist_locked(new_bytes, new_snapshot.version);

		// Cache the diff from the OLD state to the new one -- before
		// current_snapshot_ is overwritten below, since diff_snapshots
		// needs both -- for the next poller whose last-known version is
		// exactly this update's base (see current_diff_bytes_'s comment,
		// detentd.hh). Nothing to diff against on the very first accepted
		// snapshot (has_snapshot_ still false here).
		if (has_snapshot_) {
			auto diff = detent::diff_snapshots(current_snapshot_, new_snapshot);
			current_diff_bytes_ = detent::serialize_patch(diff);
			compress_into(current_diff_bytes_, current_diff_bytes_gzip_, current_diff_bytes_zstd_);
			current_diff_base_version_ = current_version_;
			has_diff_ = true;

			diff_history_.push_back(StepDiff{current_version_, diff});
			while (diff_history_.size() > max_diff_history_) diff_history_.pop_front();

			// Every composed_diff_cache_ entry targets the version we're
			// about to leave behind -- none of them are valid against the
			// new current_version_, so the whole cache is stale now, not
			// just the entries diff_history_ itself evicted above.
			composed_diff_cache_.clear();
		} else {
			has_diff_ = false;
		}

		current_bytes_ = std::move(new_bytes);
		compress_into(current_bytes_, current_bytes_gzip_, current_bytes_zstd_);
		current_version_ = new_snapshot.version;
		current_etag_ = http::weak_etag(current_bytes_); // computed once per update, not per GET
		current_snapshot_ = std::move(new_snapshot);
		has_snapshot_ = true;
		accepted_version = current_version_;
	}

	puts_accepted_.fetch_add(1, std::memory_order_relaxed);
	return accepted_version;
}

detent::Result<std::uint64_t, detent::UpdateError> Distributor::try_accept(std::string_view bytes) {
	auto parsed = detent::parse_source_snapshot(bytes, "distributor", options_.max_knobs);
	if (!parsed) return parsed.error();
	auto snapshot = std::move(parsed).value();
	return resolve_and_commit(
		[&snapshot](const detent::Snapshot&, bool) -> detent::Result<detent::Snapshot, detent::UpdateError> {
			return std::move(snapshot);
		});
}

detent::Result<std::uint64_t, detent::UpdateError> Distributor::try_accept_patch(std::string_view patch_bytes) {
	auto parsed_patch = detent::parse_snapshot_patch(patch_bytes, "distributor", options_.max_knobs);
	if (!parsed_patch) return parsed_patch.error();
	auto patch = std::move(parsed_patch).value();
	auto max_knobs = options_.max_knobs;
	return resolve_and_commit(
		[&patch, max_knobs](const detent::Snapshot& current, bool has_current)
			-> detent::Result<detent::Snapshot, detent::UpdateError> {
			if (!has_current) {
				return detent::UpdateError{detent::UpdateRejectReason::Schema, "cannot PATCH: no snapshot published yet"};
			}
			return detent::apply_patch(current, patch, max_knobs);
		});
}

namespace {
// Envelope for a command riding Raft's replicated log: correlation id +
// 1-byte kind marker + the raw body verbatim (see CLUSTERING.md).
constexpr char kEnvelopeKindPut = 'P';
constexpr char kEnvelopeKindPatch = 'H';

std::string make_envelope(const std::string& correlation_id, bool is_patch, std::string_view body) {
	std::string envelope = cluster::serialise_string(correlation_id);
	envelope += is_patch ? kEnvelopeKindPatch : kEnvelopeKindPut;
	envelope.append(body);
	return envelope;
}

struct ParsedEnvelope {
	std::string correlation_id;
	bool is_patch = false;
	std::string_view body;
};

// nullopt on a malformed envelope (defensive; a log entry is just bytes).
std::optional<ParsedEnvelope> parse_envelope(const std::string& envelope) {
	const char* p = envelope.data();
	const char* end = p + envelope.size();
	std::string_view correlation_sv;
	if (!cluster::unserialise_string(&p, end, correlation_sv)) return std::nullopt;
	if (p >= end) return std::nullopt; // missing kind byte
	char kind = *p;
	++p;
	if (kind != kEnvelopeKindPut && kind != kEnvelopeKindPatch) return std::nullopt;
	return ParsedEnvelope{std::string(correlation_sv), kind == kEnvelopeKindPatch,
	                       std::string_view(p, static_cast<std::size_t>(end - p))};
}

// Covers a follower-submitted command's extra forward + commit round trip
// without tying up an HTTP worker thread indefinitely on a wedged cluster.
constexpr auto kCommandWaitTimeout = std::chrono::seconds{10};

} // namespace

detent::Result<std::uint64_t, detent::UpdateError> Distributor::submit_command(std::string_view body, bool is_patch) {
	if (!raft_) {
		// Unclustered: unchanged single-node fast path, no consensus round trip.
		return is_patch ? try_accept_patch(body) : try_accept(body);
	}

	// Any node accepts the write; add_command() forwards to the real
	// leader if this isn't it, and apply() still fires here once committed
	// (see CLUSTERING.md). X-Detent-Leader is a latency hint only.
	// session_id_ makes this unique across restarts (see its comment,
	// detentd.hh) -- command_counter_ alone is not enough.
	auto correlation_id = options_.cluster->self_address + "#" + std::to_string(session_id_) + "#" +
		std::to_string(command_counter_.fetch_add(1, std::memory_order_relaxed));
	auto envelope = make_envelope(correlation_id, is_patch, body);

	{
		std::lock_guard<std::mutex> lock(pending_mutex_);
		pending_commands_[correlation_id]; // register before submitting; apply() can never race ahead
	}

	raft_->add_command(envelope);

	std::unique_lock<std::mutex> lock(pending_mutex_);
	bool signaled = pending_cv_.wait_for(lock, kCommandWaitTimeout, [&] {
		auto it = pending_commands_.find(correlation_id);
		return it != pending_commands_.end() && it->second.done;
	});
	auto it = pending_commands_.find(correlation_id);
	if (!signaled || it == pending_commands_.end()) {
		if (it != pending_commands_.end()) pending_commands_.erase(it); // reclaim the slot; give up waiting
		return detent::UpdateError{
			detent::UpdateRejectReason::Transport,
			"no quorum decision within " + std::to_string(kCommandWaitTimeout.count()) + "s; retry"};
	}
	auto result = std::move(it->second.result);
	pending_commands_.erase(it);
	return result;
}

void Distributor::on_raft_apply(const std::string& command) {
	auto parsed = parse_envelope(command);
	if (!parsed) return; // malformed; best-effort, drop it

	auto result = parsed->is_patch ? try_accept_patch(parsed->body) : try_accept(parsed->body);
	if (!result) puts_rejected_.fetch_add(1, std::memory_order_relaxed);

	// Only the originating node has a pending slot; every other member
	// just applied the mutation above with nobody waiting.
	std::lock_guard<std::mutex> lock(pending_mutex_);
	auto it = pending_commands_.find(parsed->correlation_id);
	if (it != pending_commands_.end()) {
		it->second.result = std::move(result);
		it->second.done = true;
		pending_cv_.notify_all();
	}
}

void Distributor::on_raft_leader_changed(std::string leader_address) {
	std::lock_guard<std::mutex> lock(leader_mutex_);
	leader_address_ = std::move(leader_address);
}

bool Distributor::is_leader() const {
	if (!raft_) return true; // unclustered: the single-node fast path is always "the leader"
	std::lock_guard<std::mutex> lock(leader_mutex_);
	return !leader_address_.empty() && leader_address_ == options_.cluster->self_address;
}

std::string Distributor::leader_address() const {
	if (!raft_) return options_.cluster ? options_.cluster->self_address : std::string{};
	std::lock_guard<std::mutex> lock(leader_mutex_);
	return leader_address_;
}

void Distributor::compress_into(std::string_view identity, std::string& gzip_out, std::string& zstd_out) {
	gzip_out.clear();
	zstd_out.clear();
	http::CompressionOptions opts{};
	if (identity.size() < opts.min_size) return; // not worth it, same threshold the http library itself uses
	auto zstd = http::encode("zstd", identity, opts);
	if (!zstd.empty() && zstd.size() < identity.size()) zstd_out = std::move(zstd);
	auto gzip = http::encode("gzip", identity, opts);
	if (!gzip.empty() && gzip.size() < identity.size()) gzip_out = std::move(gzip);
}

void Distributor::write_negotiated(
	const http::Request& request, http::ResponseWriter& response, int status,
	const std::string& identity, const std::string& gzip_body, const std::string& zstd_body,
	std::string_view content_type) {
	http::CompressionOptions opts{};
	auto coding = http::negotiate_encoding(request.header("Accept-Encoding"), opts);
	if (coding == "zstd" && !zstd_body.empty()) {
		response.set_header("Content-Encoding", "zstd");
		response.set_header("Vary", "Accept-Encoding");
		response.send(status, zstd_body, content_type);
	} else if (coding == "gzip" && !gzip_body.empty()) {
		response.set_header("Content-Encoding", "gzip");
		response.set_header("Vary", "Accept-Encoding");
		response.send(status, gzip_body, content_type);
	} else {
		response.send(status, identity, content_type);
	}
}

void Distributor::persist_locked(std::string_view bytes, std::uint64_t version) {
	if (options_.persist_path.empty()) return;
	(void)version; // not written to disk separately; it's inside `bytes` itself
	auto tmp = options_.persist_path;
	tmp += ".tmp";
	{
		std::ofstream out(tmp, std::ios::binary);
		out << bytes;
	}
	std::filesystem::rename(tmp, options_.persist_path); // throws on failure, deliberately uncaught here
}

void Distributor::load_persisted() {
	if (options_.persist_path.empty()) return;
	if (!std::filesystem::exists(options_.persist_path)) return;
	auto bytes = detent::detail::read_file(options_.persist_path, detent::kDefaultMaxBytes);
	auto parsed = detent::parse_source_snapshot(bytes, "distributor", options_.max_knobs);
	if (parsed) {
		current_version_ = parsed.value().version;
		current_etag_ = http::weak_etag(bytes); // see current_etag_'s comment (detentd.hh)
		compress_into(bytes, current_bytes_gzip_, current_bytes_zstd_);
		current_snapshot_ = std::move(parsed).value();
		current_bytes_ = std::move(bytes);
		has_snapshot_ = true;
		// has_diff_ stays false: there is no "previous" state from before
		// this restart to diff against. The first update accepted after
		// startup diffs against this loaded snapshot, same as any other.
	}
	// A corrupt persisted file starts empty rather than crashing the process;
	// the next valid PUT recovers it. An operator can inspect/replace the file.
}

void Distributor::on_bus_message(int wire_type, std::string_view content) {
	if (!raft_) return;
	auto msg = raft_message_for_wire_type(wire_type);
	if (!msg) return; // outside Raft's range; Bus's own max_type already drops these, defensive here too
	raft_->on_message(*msg, content);
}

void Distributor::route_put_snapshot(const http::Request& request, http::ResponseWriter& response,
                                      const http::Params&) {
	auto result = submit_command(request.body, /*is_patch=*/false);
	if (options_.cluster) response.set_header("X-Detent-Leader", leader_address());
	if (!result) {
		puts_rejected_.fetch_add(1, std::memory_order_relaxed);
		response.send(status_for(result.error().reason),
		              "{\"error\":\"" + json_escape(result.error().detail) + "\"}\n", "application/json");
		return;
	}
	response.send(200, "{\"accepted_version\":" + std::to_string(result.value()) + "}\n",
	              "application/json");
}

void Distributor::route_patch_snapshot(const http::Request& request, http::ResponseWriter& response,
                                        const http::Params&) {
	auto result = submit_command(request.body, /*is_patch=*/true);
	if (options_.cluster) response.set_header("X-Detent-Leader", leader_address());
	if (!result) {
		puts_rejected_.fetch_add(1, std::memory_order_relaxed);
		response.send(status_for(result.error().reason),
		              "{\"error\":\"" + json_escape(result.error().detail) + "\"}\n", "application/json");
		return;
	}
	response.send(200, "{\"accepted_version\":" + std::to_string(result.value()) + "}\n",
	              "application/json");
}

void Distributor::route_get_snapshot(const http::Request& request, http::ResponseWriter& response, const http::Params&) {
	std::string etag;
	std::uint64_t version = 0;
	bool has = false;
	{
		std::shared_lock lock(snapshot_mutex_);
		has = has_snapshot_;
		if (has) {
			version = current_version_;
			etag = current_etag_; // cheap: a short fixed-length string, not the body
		}
	}
	if (!has) {
		response.send(404, "no snapshot published yet\n");
		return;
	}

	response.set_header("X-Snapshot-Version", std::to_string(version));
	// Set our own ETag and do the If-None-Match comparison ourselves, using
	// the *cached* value computed once in try_accept()/load_persisted() --
	// not the http library's own automatic enable_conditional() support,
	// which unconditionally re-hashes the whole body on every single GET
	// (found by performance review to cost ~577us/request at a 560KB
	// snapshot, ~2.8 cores at 5K req/s just to rediscover the same
	// validator every time). The library checks has_header("ETag") first
	// and skips its own computation entirely once we've set one, so this
	// is a strict replacement, not a duplicate.
	response.set_header("ETag", etag);
	auto inm = request.header("If-None-Match");
	if (!inm.empty() && http::if_none_match(inm, etag)) {
		response.send(304, ""); // no body copy at all for the common unchanged-poller case
		return;
	}

	// Only reached on an actual body-changed response. A poller that tells
	// us its own last-known version via X-Detent-Base-Version gets a diff
	// instead of the full body when possible: the cached single-step diff
	// if that's exactly one update behind (no work per request), else a
	// diff composed from diff_history_ if it's within that bound, else the
	// full body. Either way, the body is copied out here, not
	// unconditionally at the top, so the far more common 304 case above
	// never pays for it.
	std::string bytes, bytes_gzip, bytes_zstd;
	std::string diff_bytes, diff_gzip, diff_zstd;
	std::uint64_t diff_base_version = 0;
	bool serve_diff = false;
	bool cache_insert_needed = false;
	generation_cache::Cache<std::uint64_t, CachedComposedDiff>::Generation cache_generation_at_compose = 0;
	std::optional<detent::SnapshotPatch> composed;

	auto base_version_header = request.header("X-Detent-Base-Version");
	std::optional<std::uint64_t> requested_base_version;
	if (!base_version_header.empty()) {
		try {
			requested_base_version = std::stoull(std::string(base_version_header));
		} catch (const std::exception&) {
			// Malformed header: treat as absent, fall back to the full body.
		}
	}

	{
		std::shared_lock lock(snapshot_mutex_);
		if (requested_base_version && has_diff_ && *requested_base_version == current_diff_base_version_) {
			diff_bytes = current_diff_bytes_;
			diff_gzip = current_diff_bytes_gzip_;
			diff_zstd = current_diff_bytes_zstd_;
			diff_base_version = current_diff_base_version_;
			serve_diff = true;
		} else if (requested_base_version && !diff_history_.empty() &&
		           *requested_base_version >= diff_history_.front().base_version &&
		           *requested_base_version < current_version_) {
			// Every poller still stuck at this same base_version between
			// now and the next accepted update needs the identical
			// composed result -- check the cache before redoing the
			// compose_patches work (see composed_diff_cache_'s comment,
			// detentd.hh).
			auto lookup = composed_diff_cache_.get_with_generation(*requested_base_version);
			if (lookup.value) {
				diff_bytes = lookup.value->bytes;
				diff_gzip = lookup.value->bytes_gzip;
				diff_zstd = lookup.value->bytes_zstd;
				diff_base_version = *requested_base_version;
				serve_diff = true;
			} else {
				cache_generation_at_compose = lookup.generation;
			}
			if (!serve_diff) {
				auto it = std::find_if(diff_history_.begin(), diff_history_.end(), [&](const StepDiff& step) {
					return step.base_version == *requested_base_version;
				});
				if (it != diff_history_.end()) {
					auto acc = it->patch;
					for (auto next = std::next(it); next != diff_history_.end(); ++next) {
						acc = detent::compose_patches(acc, next->patch);
					}
					composed = std::move(acc);
					diff_base_version = *requested_base_version;
					cache_insert_needed = true;
					diffs_composed_.fetch_add(1, std::memory_order_relaxed);
				} else {
					bytes = current_bytes_;
					bytes_gzip = current_bytes_gzip_;
					bytes_zstd = current_bytes_zstd_;
				}
			}
		} else {
			bytes = current_bytes_;
			bytes_gzip = current_bytes_gzip_;
			bytes_zstd = current_bytes_zstd_;
		}
	}

	if (composed) {
		diff_bytes = detent::serialize_patch(*composed);
		compress_into(diff_bytes, diff_gzip, diff_zstd);
		serve_diff = true;
	}

	if (cache_insert_needed) {
		// try_insert silently drops this if an accepted update (and the
		// clear() that comes with it) landed while we were serializing/
		// compressing outside snapshot_mutex_ -- otherwise this insert
		// would be stale relative to the NEW current_version_.
		composed_diff_cache_.try_insert(
			diff_base_version, CachedComposedDiff{diff_bytes, diff_gzip, diff_zstd}, cache_generation_at_compose);
	}

	if (serve_diff) {
		diffs_served_.fetch_add(1, std::memory_order_relaxed);
		response.set_header("X-Patch-Base-Version", std::to_string(diff_base_version));
		write_negotiated(request, response, 200, diff_bytes, diff_gzip, diff_zstd, "application/merge-patch+json");
		return;
	}
	write_negotiated(request, response, 200, bytes, bytes_gzip, bytes_zstd, "application/json");
}

void Distributor::route_get_health(const http::Request&, http::ResponseWriter& response, const http::Params&) {
	auto uptime =
		std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_time_).count();
	std::uint64_t version = 0;
	bool has = false;
	{
		std::shared_lock lock(snapshot_mutex_);
		has = has_snapshot_;
		version = current_version_;
	}
	std::ostringstream ss;
	ss << "{"
	   << "\"has_snapshot\":" << (has ? "true" : "false") << ","
	   << "\"current_version\":" << version << ","
	   << "\"requests_served\":" << requests_served_.load() << ","
	   << "\"puts_accepted\":" << puts_accepted_.load() << ","
	   << "\"puts_rejected\":" << puts_rejected_.load() << ","
	   << "\"uptime_seconds\":" << uptime;
	if (options_.cluster) {
		ss << ",\"clustered\":true"
		   << ",\"is_leader\":" << (is_leader() ? "true" : "false")
		   << ",\"leader_address\":\"" << json_escape(leader_address()) << "\"";
	} else {
		ss << ",\"clustered\":false";
	}
	ss << "}\n";
	response.send(200, ss.str(), "application/json");
}

} // namespace detentd
