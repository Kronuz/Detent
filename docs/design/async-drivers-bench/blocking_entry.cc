// Review F5: what the blocking entry must do for a shared-backend Store. An append stages its
// record into the core's TurnDriver and completes only at that turn's barrier
// (store.h:328-333, turn_driver.h:24-26): staged, it is neither done nor in flight, so no
// primitive the caller's thread drives can finish it, and a sync_wait over it with no one
// running the TurnDriver blocks forever. Committing the turn inline on the same thread
// completes it, with no io_context, scheduler or extra thread. Prints what each step observed; exits nonzero if
// an observation differs from that.
#include "probe.hh"

#include "completion/posix.h"
#include "journal/store.h"
#include "journal/turn_driver.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
namespace j = kronuz::journal;

namespace {
#define CHECK(c)                                                                     \
	do {                                                                             \
		if (!(c)) {                                                                  \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
			std::exit(1);                                                            \
		}                                                                            \
	} while (0)

const j::AdmissionLimits admission{{1024ull * 1024 * 1024, 65536}, {8ull * 1024 * 1024, 3}, {512ull * 1024 * 1024, 4}, 64, 3};
const j::TurnLogOptions turn_options{std::uint64_t{1} << 20, 32 << 10, 1, 4, 1 << 20};

fs::path directory(const fs::path& root, const std::string& name) {
	auto path = root / name;
	fs::create_directories(path);
	fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace);
	return path;
}
j::Identity identity(char tag) {
	j::Identity id{};
	id[0] = tag;
	return id;
}
}  // namespace

int main() {
	const fs::path root = fs::path(std::getenv("BENCH_DIR") ? std::getenv("BENCH_DIR") : "bench-data") / "blocking-entry";
	fs::remove_all(root);
	fs::create_directories(root);
	const int threads = probe::threads();
	{
		auto log_io = std::make_shared<j::PosixIO>(directory(root, "log"));
		j::TurnDriver turns(*log_io, identity('L'), turn_options);
		auto io = std::make_shared<j::PosixIO>(directory(root, "g1"));
		j::Store store(io, turns, 1, admission);
		store.create(identity('J'));
		while (!store.accounting()) { store.inventory_step(128); }
		while (!turns.idle()) { turns.commit(); }
		CHECK(store.shared());

		const auto before = store.frontier().sequence;
		auto permit = store.reserve_append(j::AdmissionClass::Normal, 64);
		CHECK(permit);
		auto append = store.begin_append(std::move(*permit), "one record");
		std::printf("staged: done=%d in_flight=%d turn_pending=%d\n", append->done(), append->in_flight(), !turns.idle());
		CHECK(!append->done() && !append->in_flight() && !turns.idle());

		// The blocking entry's loop: when nothing else can run, commit the pending turn here.
		std::size_t commits = 0;
		while (!append->done() && !turns.idle()) {
			turns.commit();
			++commits;
		}
		const auto frontier = append->result();
		std::printf("turn committed inline: commits=%zu done=%d sequence=%llu->%llu threads_added=%d\n", commits, append->done(),
			static_cast<unsigned long long>(before), static_cast<unsigned long long>(frontier.sequence), probe::threads() - threads);
		CHECK(append->done() && frontier.sequence == before + 1 && probe::threads() == threads);
	}
	fs::remove_all(root);
	std::printf("blocking_entry: ok\n");
	return 0;
}
