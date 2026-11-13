// Repair of a damaged generation (namespace design D10.3, step S3b), through the coordinator against a scripted
// upstream over a 100K-knob store in ten namespaces:
//   heal       T5's damage (100 flips) found by the scrub heals with no operator action, while a reader that mapped
//              the damaged generation keeps serving what it could read and never reads a wrong value;
//   delta      a delta whose path crosses a damaged page asks for a Replace and heals, scrub disabled;
//   unchanged  a repair at an unchanged cursor publishes; an ordinary same-cursor snapshot still does not;
//   relay      the same unchanged repair under a relay bundle, which keeps its cache;
//   scrub-worker  the scrub's worker hand-off at a repair and at shutdown, and a delta that lands mid-pass
//              (scrub-carry) leaves the progress over the packs it shares.
#include "../../src/detent_delivery/coordinator_internal.hh"
#include "detent_delivery/coordinator.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_distribution/messagepack.hh"
#include "detent_local/paged_layout.hh"
#include "detent_relay/bundle.hh"
#include "prepared_reader.hh"
#include "scripted_upstream.hh"
#include "leased_paged_root.hh"
#include "recovery_context.hh"
#include "segment_names.hh"
#include "task_support.hh"
#include <asio.hpp>
#include <algorithm>
#include <atomic>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <source_location>
#include <thread>
#include <unistd.h>

namespace d = detent::distribution;
namespace l = detent::local;
namespace t = detent::delivery;
namespace x = l::detail;
namespace m = d::messagepack;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {
using namespace scripted_upstream;

// 100 single-byte flips at random offsets of random packs, as T5.
struct Damage {
	std::vector<fs::path> packs;
	std::map<fs::path, std::vector<char>> original;
	std::vector<std::pair<fs::path, std::size_t>> flips;
	std::set<fs::path> damaged;
	explicit Damage(const fs::path &store) {
		for (const auto &entry : fs::directory_iterator(store))
			if (entry.path().filename().string().starts_with("segment-") && entry.path().extension() == ".pack")
				packs.push_back(entry.path());
		std::sort(packs.begin(), packs.end());
		check(packs.size() >= 3);
		for (const auto &pack : packs) {
			std::ifstream in(pack, std::ios::binary);
			original[pack] = {std::istreambuf_iterator<char>(in), {}};
		}
		std::mt19937 random(2026);
		for (int n = 0; n < 100; ++n) {
			const auto &pack = packs[random() % packs.size()];
			flips.emplace_back(pack, random() % original[pack].size());
			damaged.insert(pack);
		}
	}
	void apply() const {
		for (const auto &[pack, at] : flips) {
			const int fd = ::open(pack.c_str(), O_RDWR | O_CLOEXEC);
			check(fd >= 0);
			char byte = 0;
			check(::pread(fd, &byte, 1, static_cast<off_t>(at)) == 1);
			byte = static_cast<char>(byte ^ (1 << (at % 8)));
			check(::pwrite(fd, &byte, 1, static_cast<off_t>(at)) == 1);
			::close(fd);
		}
	}
};

// A reader of the store that reads it over and over until stopped; a read that worked keeps working and none
// returns a wrong value.
struct Watcher {
	std::atomic<bool> stop{false};
	std::atomic<std::uint64_t> rounds{0};
	std::atomic<bool> regressed{false}, wrong{false};
	std::vector<char> first_good;
	std::uint64_t ok_first = 0, failed_first = 0;
	std::unique_ptr<Reader> reader;
	std::jthread thread;
	Watcher(const fs::path &store, unsigned revision) : reader(std::make_unique<Reader>(store, true)) {
		const auto first = reader->read(revision);
		first_good = first.good;
		ok_first = first.ok;
		failed_first = first.failed;
		check(!first.wrong && !first.other);
		thread = std::jthread([this, revision] {
			while (!stop) {
				const auto reads = reader->read(revision);
				if (reads.wrong || reads.other)
					wrong = true;
				for (int i = 0; i < knobs; ++i)
					if (first_good[i] && !reads.good[i])
						regressed = true;
				++rounds;
			}
		});
	}
	void finish() {
		stop = true;
		if (thread.joinable())
			thread.join();
		reader.reset();
	}
	~Watcher() { finish(); }
};

// T5's damage over the coordinator: the scrub reports, a repair publishes, every read is right afterwards, the
// damaged packs are collected and the scrub runs clean; a reader that mapped the damaged generation keeps serving.
void heal(const fs::path &certificates, const fs::path &root, bool close_deferred = false) {
	const auto store = root / "store";
	seed(certificates, store);
	Damage damage(store);
	damage.apply();
	const auto old_packs = packs_in(store);
	const auto before = l::read_generation_selection(store).descriptor;
	l::SegmentId orphan_id{};
	orphan_id.fill('z');
	const x::SegmentNames orphan(orphan_id);
	{
		const std::string bytes(257, 'x');
		const auto owner = l::encode_segment_ownership({orphan_id,
			bytes.size(), l::paged::pack_commitment(bytes)});
		std::ofstream payload(store / orphan.payload_name(), std::ios::binary);
		payload.write(bytes.data(), bytes.size());
		std::ofstream record(store / orphan.owner_name(), std::ios::binary);
		record.write(owner.data(), owner.size());
		check(bool(payload) && bool(record));
	}
	Watcher watcher(store, 1);
	std::cout << "heal: reader before the repair: " << watcher.ok_first << " ok, " << watcher.failed_first
			  << " source failures\n";
	check(watcher.failed_first > 0 && watcher.ok_first > 0);
	Server server(certificates);
	t::CoordinatorStatus final_status;
	run_phase(certificates, store, server, {}, [&](task::asio_scheduler &scheduler, auto coordinator, auto disk, auto, auto) -> task::task<void> {
		check(explain(co_await until(scheduler, 90s, [&] { return coordinator->status().repair.completed >= 1; }), coordinator, server));
		auto status = coordinator->status();
		check(settled(status) && status.repair.requested >= 1 && !status.repair.pending);
		// Later scrub reports leave the forced Replace alone: one forced session carries the repair.
		check(server.forced_opens == 1);
		// The reader that mapped the damaged generation served throughout; a Dense repair can land before its
		// first full round, so wait for one.
		check(co_await until(scheduler, 60s, [&] { return watcher.rounds > 0; }));
		check(!watcher.regressed && !watcher.wrong);
		// The damaged root's lease keeps its packs, while an unrelated orphan is reclaimed.
		check(co_await until(scheduler, 60s, [&] { return !fs::exists(store / orphan.payload_name()); }));
		check(std::all_of(old_packs.begin(), old_packs.end(), [](const auto &pack) { return fs::exists(pack); }));
		check(co_await until(scheduler, 60s, [&] {
			return disk->collection_retry_at() != std::chrono::steady_clock::time_point::max();
		}));
		const auto first_retry = disk->collection_retry_at();
		check(first_retry > std::chrono::steady_clock::now());
		// A second deferred pass backs off and still preserves every leased extent.
		check(co_await until(scheduler, 60s, [&] {
			const auto retry = disk->collection_retry_at();
			return retry != std::chrono::steady_clock::time_point::max() && retry > first_retry;
		}));
		check(disk->collection_retry_at() >= first_retry + 2s);
		for (const auto &pack : old_packs)
			check(fs::exists(pack) && fs::file_size(pack) == damage.original.at(pack).size());
		if (close_deferred) {
			final_status = coordinator->status();
			coordinator->close();
			check(disk->collection_retry_at() == std::chrono::steady_clock::time_point::max());
			co_return;
		}
		watcher.finish();
		const auto repaired = status.selected.descriptor;
		check(repaired && repaired->generation != before.generation && repaired->cursor == before.cursor);
		// A snapshot shares no pack with its base: the repaired generation's packs are all new.
		const auto now = packs_in(store);
		std::size_t fresh = 0;
		for (const auto &pack : now)
			fresh += !old_packs.count(pack);
		check(fresh >= 3 && now.size() == old_packs.size() + fresh);
		// The repair's original request is retried after the reader releases its lease.
		check(co_await until(scheduler, 60s, [&] {
			return std::none_of(old_packs.begin(), old_packs.end(), [](const auto &pack) { return fs::exists(pack); });
		}));
		// The scrub runs clean over the repaired generation.
		const auto passes = coordinator->status().scrub.passes;
		check(co_await until(scheduler, 60s, [&] { return coordinator->status().scrub.passes > passes; }));
		status = coordinator->status();
		check(!status.scrub.source_failure && status.scrub.damaged_packs == 0 && settled(status) && status.repair.completed == 1);
		final_status = status;
	});
	// Every read is right through a reader of the repaired store, and after a restart.
	{
		Reader reader(store);
		const auto reads = reader.read(1);
		check(reads.ok == std::uint64_t(knobs) && !reads.failed && !reads.wrong && !reads.other);
	}
	std::cout << "heal: " << damage.damaged.size() << " damaged packs; repairs requested " << final_status.repair.requested
			  << ", completed " << final_status.repair.completed << "; coordinator failures " << final_status.failures
			  << "; the reader served " << watcher.rounds.load() << " full rounds without a regression; after the repair "
			  << knobs << " reads ok, " << old_packs.size() << " damaged packs collected, scrub clean ("
			  << final_status.scrub.passes << " passes); upstream opens " << server.opens << ", forced "
			  << server.forced_opens << "\n";
}

// A delta that crosses a damaged page asks for a Replace and heals, with the scrub disabled so the first-touch
// failure is the only trigger.
void delta(const fs::path &certificates, const fs::path &root) {
	const auto store = root / "store";
	seed(certificates, store);
	Damage(store).apply();
	const auto old_packs = packs_in(store);
	Server server(certificates);
	server.resume = Server::Resume::Delta;
	t::CoordinatorStatus final_status;
	run_phase(certificates, store, server, {.scrub_rate = 0}, [&](task::asio_scheduler &scheduler, auto coordinator, auto, auto, auto) -> task::task<void> {
		check(explain(co_await until(scheduler, 90s, [&] { return coordinator->status().repair.completed >= 1; }), coordinator, server));
		check(co_await until(scheduler, 30s, [&] {
			const auto status = coordinator->status();
			return status.recoverable && status.recoverable->revision == 2;
		}));
		final_status = coordinator->status();
		check(settled(final_status) && final_status.scrub.passes == 0);
	});
	check(server.deltas >= 1 && server.forced_opens >= 1);
	Reader reader(store);
	const auto reads = reader.read(2);
	check(reads.ok == std::uint64_t(knobs) && !reads.failed && !reads.wrong && !reads.other);
	check(l::read_generation_selection(store).descriptor.cursor.revision == 2);
	std::cout << "delta: the delta was offered " << server.deltas << " time(s), " << server.forced_opens
			  << " forced open(s), the failure counted " << final_status.failures << " time(s); revision 2 selected, "
			  << reads.ok << " reads ok, none failed, " << old_packs.size() << " damaged packs replaced\n";
}

// A repair at an unchanged cursor publishes; an ordinary same-cursor snapshot does not.
void unchanged(const fs::path &certificates, const fs::path &root, bool relay) {
	const auto store = root / "store";
	seed(certificates, store);
	const auto old_packs = packs_in(store);
	const auto before = l::read_generation_selection(store).descriptor;
	Server server(certificates);
	server.resume = Server::Resume::Snapshot;
	run_phase(certificates, store, server, {.scrub_rate = 0, .relay = relay},
			  [&](task::asio_scheduler &scheduler, auto coordinator, auto, auto bundle, auto counting) -> task::task<void> {
		// The ordinary same-cursor snapshot is checked against the state and dropped: nothing is published.
		check(explain(co_await until(scheduler, 60s, [&] { return server.snapshots >= 1 && coordinator->status().acknowledged == cursor(); }), coordinator, server));
		auto status = coordinator->status();
		check(status.selected.descriptor == before && status.repair.requested == 0);
		if (bundle)
			check(bundle->status().ready);
		const auto stages = counting ? counting->stages : 0;
		// A repair request publishes the same content as a fresh generation at the same cursor.
		t::detail::CoordinatorAccess::request_repair(*coordinator);
		check(explain(co_await until(scheduler, 60s, [&] { return coordinator->status().repair.completed == 1; }), coordinator, server));
		status = coordinator->status();
		check(settled(status) && !status.repair.pending);
		check(status.selected.descriptor && status.selected.descriptor->cursor == before.cursor &&
			  status.selected.descriptor->generation != before.generation);
		if (bundle) {
			const auto relay_status = bundle->status();
			check(relay_status.ready && !relay_status.fenced);
			auto offer = bundle->offer({}, true);
			check(offer.decision == d::CacheDecision::Update || offer.decision == d::CacheDecision::Busy);
			// The repair at the selected cursor reached the sink as a repair and its update was not staged.
			check(counting->repairs == 1 && counting->stages == stages);
		}
	});
	Reader reader(store);
	const auto reads = reader.read(1);
	check(reads.ok == std::uint64_t(knobs) && !reads.failed && !reads.wrong && !reads.other);
	check(l::read_generation_selection(store).descriptor.generation != before.generation);
	std::cout << "unchanged" << (relay ? " (relay bundle)" : "") << ": the same-cursor snapshot published nothing; the repair published generation "
			  << "different from the first at cursor " << before.cursor.revision << ", " << reads.ok << " reads ok\n";
}
void repair_reset(const fs::path &certificates, const fs::path &root) {
	const auto store = root / "store";
	seed(certificates, store);
	const auto prior = cursor();
	fixture_epoch = 'f';
	Server server(certificates);
	run_phase(certificates, store, server, {.scrub_rate = 0, .reset_from = prior},
		[&](task::asio_scheduler &scheduler, auto coordinator, auto, auto, auto) -> task::task<void> {
		t::detail::CoordinatorAccess::request_repair(*coordinator);
		check(co_await until(scheduler, 60s, [&] { return coordinator->status().recoverable == cursor(); }));
		const auto status = coordinator->status();
		check(settled(status) && status.repair.requested == 1 && status.repair.completed == 0 && !status.repair.pending);
	});
	Reader reader(store);
	const auto reads = reader.read(1);
	check(reads.ok == std::uint64_t(knobs) && !reads.failed && !reads.wrong && !reads.other);
	fixture_epoch = 'e';
}
void scrub_worker(const fs::path &certificates, const fs::path &root) {
	const auto store = root / "store";
	seed(certificates, store);
	Reader leased(store, true);
	Server server(certificates);
	server.head = 2;
	run_phase(certificates, store, server, {.workers = 2},
		[&](task::asio_scheduler &scheduler, auto coordinator, auto, auto, auto) -> task::task<void> {
		check(co_await until(scheduler, 60s, [&] { return coordinator->status().initialized; }));
		for (bool closing : {false, true}) {
			auto pause = std::make_shared<t::detail::CoordinatorAccess::PreparedProbePause>();
			std::promise<void> resume;
			pause->resume = resume.get_future().share();
			struct Release {
				std::promise<void> &resume;
				~Release() { try { resume.set_value(); } catch (const std::future_error &) {} }
			} release{resume};
			if (!closing)
				Damage(store).apply();
			t::detail::CoordinatorAccess::pause_next_scrub(*coordinator, pause);
			check(co_await until(scheduler, 60s, [&] { return pause->reached.load(std::memory_order_acquire); }));
			if (!closing) {
				t::detail::CoordinatorAccess::request_repair(*coordinator);
				check(co_await until(scheduler, 60s, [&] { return coordinator->status().repair.completed == 1; }));
				resume.set_value();
				const auto passes = coordinator->status().scrub.passes;
				check(co_await until(scheduler, 60s, [&] { return coordinator->status().scrub.passes > passes; }));
				check(coordinator->status().repair.requested == 1 && !coordinator->status().repair.pending);
			} else {
				coordinator->close();
				check(coordinator->status().phase != t::CoordinatorPhase::Closed);
				asio::steady_timer timer(scheduler.executor());
				timer.expires_after(40ms);
				const auto error = co_await task::await_asio<void(asio::error_code)>(
					[&](auto handler) { timer.async_wait(std::move(handler)); });
				check(!error && !coordinator->drained());
				resume.set_value();
			}
		}
	});
}
// A sink that runs a hook when the coordinator binds a built candidate: on the owner executor at phase Building,
// where a scrub report can interleave with a delta's build.
class HookSink final : public t::PublicationSink {
  public:
	void authenticated() noexcept override {}
	void begin_stage(const t::ReceivedUpdate &) override {}
	bool stage_step(d::WorkBudget &) override { return true; }
	void bind_candidate(const l::Descriptor &descriptor, l::DirectoryIdentity) noexcept override {
		if (on_bind)
			on_bind(descriptor);
	}
	bool release_stage(d::WorkBudget &) noexcept override { return true; }
	void selected(const l::RecoveredGeneration &) override { ++selects; }
	void selected_repaired(const l::RecoveredGeneration &) override { ++repairs; }
	void fence() noexcept override { fenced = true; }
	std::function<void(const l::Descriptor &)> on_bind;
	unsigned selects = 0, repairs = 0;
	bool fenced = false;
};
// A repair requested while a delta (which keeps every unchanged pack of the generation that asked for repair) is
// being built must survive that delta's publication: a Replace is asked for and a Repair completes.
void delta_clears(const fs::path &certificates, const fs::path &root) {
	const auto store = root / "store";
	seed(certificates, store);
	const auto old_packs = packs_in(store);
	Server server(certificates);
	auto sink = std::make_shared<HookSink>();
	t::CoordinatorStatus at_delta, final_status;
	bool fired = false;
	t::CoordinatorPhase phase_at_request{};
	run_phase(certificates, store, server, {.scrub_rate = 0, .sink = sink},
		[&](task::asio_scheduler &scheduler, auto coordinator, auto, auto, auto) -> task::task<void> {
		check(co_await until(scheduler, 60s, [&] { return coordinator->status().initialized; }));
		auto *raw = coordinator.get();
		sink->on_bind = [&, raw](const l::Descriptor &candidate) {
			if (!fired && candidate.cursor.revision == 2) {
				fired = true;
				phase_at_request = raw->status().phase;
				t::detail::CoordinatorAccess::request_repair(*raw);
			}
		};
		server.resume = Server::Resume::Delta;
		check(explain(co_await until(scheduler, 60s, [&] {
			const auto status = coordinator->status();
			return status.recoverable && status.recoverable->revision == 2;
		}), coordinator, server));
		at_delta = coordinator->status();
		check(explain(co_await until(scheduler, 60s, [&] { return coordinator->status().repair.completed >= 1; }),
					  coordinator, server));
		final_status = coordinator->status();
		// The collector then removes every pack the damaged generation and the delta shared.
		co_await until(scheduler, 60s, [&] {
			const auto now = packs_in(store);
			return std::none_of(old_packs.begin(), old_packs.end(), [&](const auto &pack) { return now.count(pack); });
		});
	});
	const auto now = packs_in(store);
	std::size_t kept = 0;
	for (const auto &pack : old_packs)
		kept += now.count(pack);
	std::cout << "delta-clears: hook fired " << fired << " at phase " << static_cast<unsigned>(phase_at_request)
			  << "; after the delta: requested " << at_delta.repair.requested << " completed "
			  << at_delta.repair.completed << " pending " << at_delta.repair.pending << "; after the repair: requested "
			  << final_status.repair.requested << " completed " << final_status.repair.completed << " pending "
			  << final_status.repair.pending << "; upstream opens " << server.opens << " forced "
			  << server.forced_opens << " snapshots " << server.snapshots << " deltas " << server.deltas
			  << "; prior packs left after collection " << kept << " of " << old_packs.size() << "\n";
	check(fired && server.deltas == 1);
	check(final_status.repair.requested == 1);
	// The pack-sharing delta does not retire the request: a forced Replace follows and the repair completes.
	check(final_status.repair.completed >= 1 && !final_status.repair.pending && server.forced_opens >= 1);
	check(kept == 0);
}
// A session opened for a Replace stays open after its snapshot; a repair requested while a later delta on it is
// built still gets a Replace, so the session counts as forced only until its first offer is published.
void forced_again(const fs::path &certificates, const fs::path &root) {
	const auto store = root / "store";
	seed(certificates, store);
	Server server(certificates);
	auto sink = std::make_shared<HookSink>();
	t::CoordinatorStatus first, final_status;
	bool fired = false, fenced = true;
	run_phase(certificates, store, server, {.scrub_rate = 0, .sink = sink},
		[&](task::asio_scheduler &scheduler, auto coordinator, auto, auto, auto) -> task::task<void> {
		check(co_await until(scheduler, 60s, [&] { return coordinator->status().initialized; }));
		auto *raw = coordinator.get();
		t::detail::CoordinatorAccess::request_repair(*raw);
		check(explain(co_await until(scheduler, 60s, [&] { return coordinator->status().repair.completed == 1; }),
					  coordinator, server));
		first = coordinator->status();
		sink->on_bind = [&, raw](const l::Descriptor &candidate) {
			if (!fired && candidate.cursor.revision == 2) {
				fired = true;
				t::detail::CoordinatorAccess::request_repair(*raw);
			}
		};
		server.forced_delta = true;
		server.resume = Server::Resume::Delta;
		check(explain(co_await until(scheduler, 60s, [&] { return coordinator->status().repair.completed == 2; }),
					  coordinator, server));
		final_status = coordinator->status();
		// Close fences the sink, so it is read before the phase ends.
		fenced = sink->fenced;
	});
	std::cout << "forced-again: first repair with " << first.repair.requested << " request(s); hook fired " << fired
			  << "; final: requested " << final_status.repair.requested << " completed "
			  << final_status.repair.completed << " pending " << final_status.repair.pending << "; upstream opens "
			  << server.opens << " forced " << server.forced_opens << " snapshots " << server.snapshots
			  << " deltas " << server.deltas << "\n";
	check(fired && server.deltas == 1 && server.forced_opens == 2);
	check(final_status.repair.requested == 2 && !final_status.repair.pending && !fenced);
	check(final_status.selected.descriptor && final_status.selected.descriptor->cursor.revision == 2);
	Reader reader(store);
	const auto reads = reader.read(2);
	check(reads.ok == std::uint64_t(knobs) && !reads.failed && !reads.wrong && !reads.other);
}
// A delta published while a pass is under way keeps what the pass hashed clean over the packs the delta shares:
// after the new root is selected the pass hashes less than that root's packs. (The scripted delta keeps every
// pack of its base and adds one.)
void scrub_carry(const fs::path &certificates, const fs::path &root) {
	const auto store = root / "store";
	seed(certificates, store);
	const auto old_packs = packs_in(store);
	std::uint64_t old_bytes = 0, largest = 0;
	for (const auto &pack : old_packs) {
		old_bytes += fs::file_size(pack);
		largest = std::max<std::uint64_t>(largest, fs::file_size(pack));
	}
	Server server(certificates);
	std::uint64_t at_swap = 0, at_end = 0, fresh_bytes = 0;
	// A pass over the packs takes about twelve seconds; the delta lands once the first pack is done, whichever it is.
	run_phase(certificates, store, server, {.scrub_rate = old_bytes / 12},
		[&](task::asio_scheduler &scheduler, auto coordinator, auto, auto, auto) -> task::task<void> {
		check(co_await until(scheduler, 60s, [&] { return coordinator->status().initialized; }));
		check(explain(co_await until(scheduler, 60s, [&] { return coordinator->status().scrub.bytes_hashed >= largest + 65536; }),
					  coordinator, server));
		server.resume = Server::Resume::Delta;
		check(explain(co_await until(scheduler, 60s, [&] {
			const auto status = coordinator->status();
			return status.recoverable && status.recoverable->revision == 2;
		}), coordinator, server));
		const auto swapped = coordinator->status();
		at_swap = swapped.scrub.bytes_hashed;
		check(swapped.scrub.passes == 0);
		check(explain(co_await until(scheduler, 60s, [&] { return coordinator->status().scrub.passes >= 1; }),
					  coordinator, server));
		at_end = coordinator->status().scrub.bytes_hashed;
		for (const auto &pack : packs_in(store))
			if (!old_packs.count(pack))
				fresh_bytes += fs::file_size(pack);
	});
	const auto new_root = old_bytes + fresh_bytes, after_swap = at_end - at_swap;
	std::cout << "scrub-carry: base packs " << old_bytes << " bytes, new root " << new_root << "; hashed " << at_swap
			  << " at the delta and " << after_swap << " after it, to the end of the pass\n";
	// A restart would hash the whole new root after the swap; a pass that resumes skips what it had done.
	check(after_swap + 200'000 < new_root);
}
} // namespace

int main(int argc, char **argv) {
	try {
		check(argc == 3);
		const fs::path certificates(argv[1]);
		const std::string mode = argv[2];
		const auto root = scratch(mode.c_str());
		struct Cleanup {
			fs::path path;
			~Cleanup() { fs::remove_all(path); }
		} cleanup{root};
		if (mode == "heal")
			heal(certificates, root);
		else if (mode == "lease-close")
			heal(certificates, root, true);
		else if (mode == "delta")
			delta(certificates, root);
		else if (mode == "unchanged")
			unchanged(certificates, root, false);
		else if (mode == "relay")
			unchanged(certificates, root, true);
		else if (mode == "scrub-worker") {
			scrub_worker(certificates, root);
			scrub_carry(certificates, root / "carry");
		}
		else if (mode == "reset")
			repair_reset(certificates, root);
		else if (mode == "delta-clears")
			delta_clears(certificates, root);
		else if (mode == "forced-again")
			forced_again(certificates, root);
		else
			throw std::invalid_argument("unknown mode");
		std::cout << "ok\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "test_delivery_repair: " << error.what() << '\n';
		return 1;
	}
}
