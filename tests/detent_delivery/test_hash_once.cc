// Each tier hashes its own build once (namespace design D12.2, step S4), through the coordinator against a
// scripted upstream, with the process-wide hash counters read as differences:
//   agent, relay  a cold bootstrap of a store of `knobs` knobs publishes revision 1; the tier's page and pack
//                 hashing is exactly its build's (nothing re-validates it), and everything it hashed, frames
//                 included, is at most 1.2 times the bytes its build hashed;
//   delta         the delta to revision 2, over the base that tier just built, hashes only the pages it writes;
//   restart       a restart over the store the tier built hashes no page or pack;
//   damage        a pack damaged on disk after the tier's own publication is still caught, by the scrub, and
//                 healed by a repair, with no restart in between.
// usage: test_delivery_hash_once CERTIFICATES MODE [KNOBS]
#include "detent_delivery/hash_stats.hh"
#include "scripted_upstream.hh"
#include "prepared_reader.hh"
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <random>
#include <set>

using namespace scripted_upstream;

namespace {
std::uint64_t pack_file_bytes(const fs::path &store, std::size_t &count) {
	std::uint64_t bytes = 0;
	count = 0;
	for (const auto &entry : fs::directory_iterator(store))
		if (entry.path().filename().string().starts_with("segment-") && entry.path().extension() == ".pack") {
			bytes += fs::file_size(entry.path());
			++count;
		}
	return bytes;
}
std::string line(std::string_view tier, const t::HashStats &h, std::uint64_t packs_bytes, std::size_t packs) {
	const auto built = h.built_bytes();
	std::ostringstream out;
	out << "hash-once " << tier << " knobs=" << knobs << " packs=" << packs << " pack_file_bytes=" << packs_bytes
		<< " built_page_bytes=" << h.built.page_bytes << " built_pack_bytes=" << h.built.pack_bytes
		<< " hashed_page_bytes=" << h.hashed.page_bytes << " hashed_pack_bytes=" << h.hashed.pack_bytes
		<< " metadata_bytes=" << h.hashed.metadata_bytes << " frame_bytes=" << h.frame.bytes
		<< " checked_bytes=" << h.checked_bytes() << " total_bytes=" << h.total_bytes() << " total_over_built="
		<< (built ? double(h.total_bytes()) / double(built) : 0.0) << " total_over_pack_bytes="
		<< (packs_bytes ? double(h.total_bytes()) / double(packs_bytes) : 0.0);
	return out.str();
}
// Nothing a tier built is hashed again: its page and pack hashing is its build's, and metadata is a rounding
// error. The agent hashes each frame once on receipt, which keeps its whole total within 1.2 times its build's;
// the relay also encodes the frames it serves (raw and zstd), so its frame bytes are reported, not bounded.
void expect_built_once(const t::HashStats &h, bool relay) {
	check(h.built.page_bytes > 0 && h.built.pack_bytes > 0);
	check(h.hashed.page_bytes == h.built.page_bytes && h.hashed.pack_bytes == h.built.pack_bytes);
	check(h.hashed.metadata_bytes * 50 <= h.built_bytes());
	check(h.frame.bytes > 0);
	check(relay || h.total_bytes() * 5 <= h.built_bytes() * 6);
}

void bootstrap(const fs::path &certificates, const fs::path &root, bool relay) {
	const auto store = root / "store";
	fs::create_directories(store);
	fs::permissions(store, fs::perms::owner_all);
	Server server(certificates);
	const auto before = t::hash_stats();
	t::HashStats delta;
	run_phase(certificates, store, server, {.scrub_rate = 0, .relay = relay, .fresh = true},
			  [&](task::asio_scheduler &scheduler, auto coordinator, auto, auto, auto) -> task::task<void> {
				  check(explain(co_await until(scheduler, 240s, [&] {
					  const auto status = coordinator->status();
					  return status.acknowledged == cursor() && status.recoverable == cursor();
				  }), coordinator, server));
				  delta = coordinator->status().hash - before;
			  });
	std::size_t packs = 0;
	const auto bytes = pack_file_bytes(store, packs);
	std::cout << line(relay ? "relay" : "agent", delta, bytes, packs) << '\n';
	expect_built_once(delta, relay);
	check(delta.built.pack_bytes == bytes);
	Reader reader(store);
	const auto reads = reader.read(1);
	check(reads.ok == std::uint64_t(knobs) && !reads.failed && !reads.wrong && !reads.other);
}

// The delta to revision 2 over the base the tier built in this process.
void delta(const fs::path &certificates, const fs::path &root, bool relay) {
	const auto store = root / "store";
	fs::create_directories(store);
	fs::permissions(store, fs::perms::owner_all);
	Server server(certificates);
	t::HashStats base, update;
	run_phase(certificates, store, server, {.scrub_rate = 0, .relay = relay, .fresh = true},
			  [&](task::asio_scheduler &scheduler, auto coordinator, auto, auto, auto) -> task::task<void> {
				  check(explain(co_await until(scheduler, 240s, [&] {
					  const auto status = coordinator->status();
					  return status.acknowledged == cursor() && status.recoverable == cursor();
				  }), coordinator, server));
				  base = coordinator->status().hash;
				  server.resume = Server::Resume::Delta;
				  check(explain(co_await until(scheduler, 120s, [&] {
					  const auto status = coordinator->status();
					  return status.recoverable && status.recoverable->revision == 2;
				  }), coordinator, server));
				  update = coordinator->status().hash - base;
				  check(server.deltas == 1);
				  std::size_t packs = 0;
				  const auto bytes = pack_file_bytes(store, packs);
				  std::cout << line(relay ? "relay-delta" : "agent-delta", update, bytes, packs) << std::endl;
				  // The delta writes pages and packs and hashes nothing else of the base it reads.
				  check(update.built.page_bytes > 0 && update.built.pack_bytes > 0);
				  check(update.hashed.page_bytes == update.built.page_bytes &&
						update.hashed.pack_bytes == update.built.pack_bytes);
				  check(update.hashed.metadata_bytes * 50 <= update.built_bytes());
			  });
	Reader reader(store);
	const auto reads = reader.read(2);
	check(reads.ok == std::uint64_t(knobs) && !reads.failed && !reads.wrong && !reads.other);
}

// A restart over the store a tier built trusts it: no page or pack is hashed, whatever the first run hashed.
void restart(const fs::path &certificates, const fs::path &root) {
	const auto store = root / "store";
	fs::create_directories(store);
	fs::permissions(store, fs::perms::owner_all);
	{
		Server server(certificates);
		run_phase(certificates, store, server, {.scrub_rate = 0, .fresh = true},
				  [&](task::asio_scheduler &scheduler, auto coordinator, auto, auto, auto) -> task::task<void> {
					  check(explain(co_await until(scheduler, 240s, [&] {
						  const auto status = coordinator->status();
						  return status.acknowledged == cursor() && status.recoverable == cursor();
					  }), coordinator, server));
				  });
	}
	Server server(certificates);
	const auto before = t::hash_stats();
	t::HashStats started;
	run_phase(certificates, store, server, {.scrub_rate = 0},
			  [&](task::asio_scheduler &scheduler, auto coordinator, auto, auto, auto) -> task::task<void> {
				  check(explain(co_await until(scheduler, 240s, [&] {
					  const auto status = coordinator->status();
					  return status.prepared_ready || status.recoverable == cursor();
				  }), coordinator, server));
				  started = coordinator->status().hash - before;
			  });
	std::cout << "hash-once restart knobs=" << knobs << " page_bytes=" << started.hashed.page_bytes
			  << " pack_bytes=" << started.hashed.pack_bytes << " metadata_bytes=" << started.hashed.metadata_bytes << '\n';
	check(started.hashed.page_bytes == 0 && started.hashed.pack_bytes == 0);
}

// A pack damaged on disk after this tier's own publication, with the process running: no first touch has
// gated it, so the scrub reports it, a repair publishes, and every knob reads right afterwards.
void damage(const fs::path &certificates, const fs::path &root) {
	const auto store = root / "store";
	fs::create_directories(store);
	fs::permissions(store, fs::perms::owner_all);
	Server server(certificates);
	std::set<fs::path> damaged;
	t::CoordinatorStatus final_status;
	run_phase(certificates, store, server, {.scrub_rate = 1'000'000'000, .fresh = true},
			  [&](task::asio_scheduler &scheduler, auto coordinator, auto, auto, auto) -> task::task<void> {
				  check(explain(co_await until(scheduler, 240s, [&] {
					  const auto status = coordinator->status();
					  return status.acknowledged == cursor() && status.recoverable == cursor();
				  }), coordinator, server));
				  std::vector<fs::path> packs;
				  for (const auto &entry : fs::directory_iterator(store))
					  if (entry.path().filename().string().starts_with("segment-") && entry.path().extension() == ".pack")
						  packs.push_back(entry.path());
				  std::sort(packs.begin(), packs.end());
				  check(!packs.empty());
				  std::mt19937 random(2026);
				  for (int n = 0; n < 100; ++n) {
					  const auto &pack = packs[random() % packs.size()];
					  const int fd = ::open(pack.c_str(), O_RDWR | O_CLOEXEC);
					  check(fd >= 0);
					  const auto at = static_cast<off_t>(random() % fs::file_size(pack));
					  char byte = 0;
					  check(::pread(fd, &byte, 1, at) == 1);
					  byte = static_cast<char>(byte ^ (1 << (at % 8)));
					  check(::pwrite(fd, &byte, 1, at) == 1);
					  ::close(fd);
					  damaged.insert(pack);
				  }
				  check(explain(co_await until(scheduler, 120s, [&] { return coordinator->status().repair.completed >= 1; }),
							    coordinator, server));
				  final_status = coordinator->status();
				  check(settled(final_status));
				  check(final_status.scrub.damaged_packs >= 1);
				  check(!final_status.repair.pending);
				  std::cout << "hash-once damage knobs=" << knobs << " damaged_packs=" << damaged.size()
							<< " scrub_passes=" << final_status.scrub.passes << " repairs=" << final_status.repair.completed << '\n';
			  });
	Reader reader(store);
	const auto reads = reader.read(1);
	check(reads.ok == std::uint64_t(knobs) && !reads.failed && !reads.wrong && !reads.other);
}
} // namespace

int main(int argc, char **argv) {
	try {
		check(argc == 3 || argc == 4);
		const fs::path certificates(argv[1]);
		const std::string mode = argv[2];
		compression = true;
		if (argc == 4)
			knobs = std::atoi(argv[3]);
		const auto root = scratch(mode.c_str());
		struct Cleanup {
			fs::path path;
			~Cleanup() { fs::remove_all(path); }
		} cleanup{root};
		if (mode == "agent")
			bootstrap(certificates, root, false);
		else if (mode == "relay")
			bootstrap(certificates, root, true);
		else if (mode == "agent-delta")
			delta(certificates, root, false);
		else if (mode == "relay-delta")
			delta(certificates, root, true);
		else if (mode == "restart")
			restart(certificates, root);
		else if (mode == "damage")
			damage(certificates, root);
		else
			throw std::invalid_argument("unknown mode");
		std::cout << "ok\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "test_delivery_hash_once: " << error.what() << '\n';
		return 1;
	}
}
