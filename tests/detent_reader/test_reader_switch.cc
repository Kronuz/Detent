// Generation switch under load, injected parks, no threads and fork (reader.md T3, T6, D5, D10).
// DETENT_READER_SWITCH_SECONDS (default 60) and DETENT_READER_SWITCH_PUBLISHES (default 1000) size T3.
#include "detent/reader.h"
#include "detent_testing/generation_writer.hh"
#include "testing.hh"
#include <cstdio>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/mman.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
#ifdef __APPLE__
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

namespace t = detent::reader::testing;
using namespace std::chrono_literals;
namespace {
void check(bool value, int line) {
	if (!value) {
		std::cerr << "reader switch invariant at line " << line << std::endl;
		std::_Exit(1);
	}
}
#define CHECK(value) check(value, __LINE__)
detent_str S(std::string_view s) { return {s.data(), s.size()}; }
std::string knobs(std::int64_t rev) {
	return R"({"schema_version":1,"knobs":{"t:rev":{"type":"int","default":)" + std::to_string(rev) +
	       R"(},"t:flag":{"type":"bool","default":true}}})";
}
std::uint64_t env(const char *name, std::uint64_t fallback) {
	const char *value = std::getenv(name);
	return value && *value ? std::strtoull(value, nullptr, 10) : fallback;
}

// A thread arms one window; the hook parks it there until the test releases it.
struct Park {
	std::atomic<int> state{0}; // 0 armed, 1 parked, 2 released
};
thread_local t::Window armed_window{};
thread_local Park *armed = nullptr;
thread_local t::Window then_window{}; // armed once the first park is released
thread_local Park *then = nullptr;
void hook(t::Window at, void *) noexcept {
	if (!armed || at != armed_window)
		return;
	auto *park = std::exchange(armed, nullptr);
	park->state.store(1);
	while (park->state.load() != 2)
		std::this_thread::sleep_for(100us);
	armed = std::exchange(then, nullptr);
	armed_window = then_window;
}
void wait_parked(Park &park) {
	for (auto deadline = std::chrono::steady_clock::now() + 30s; park.state.load() != 1;) {
		CHECK(std::chrono::steady_clock::now() < deadline);
		std::this_thread::sleep_for(100us);
	}
}
void release(Park &park) { park.state.store(2); }
std::thread parked(Park &park, t::Window at, std::function<void()> body, Park *next = nullptr,
                   t::Window next_at = {}) {
	std::thread thread([&park, at, next, next_at, body = std::move(body)] {
		armed_window = at;
		armed = &park;
		then_window = next_at;
		then = next;
		body();
	});
	wait_parked(park);
	return thread;
}

std::int64_t get(detent_reader *h) {
	std::int64_t out = -1;
	CHECK(detent_get(h, S("t:rev"), -1, &out) == DETENT_OK);
	return out;
}
std::size_t threads() {
#ifdef __APPLE__
	thread_act_array_t list;
	mach_msg_type_number_t count = 0;
	CHECK(task_threads(mach_task_self(), &list, &count) == KERN_SUCCESS);
	for (mach_msg_type_number_t i = 0; i != count; ++i)
		mach_port_deallocate(mach_task_self(), list[i]);
	vm_deallocate(mach_task_self(), vm_address_t(list), count * sizeof(thread_act_t));
	return count;
#else
	std::size_t count = 0;
	for ([[maybe_unused]] const auto &entry : std::filesystem::directory_iterator("/proc/self/task"))
		++count;
	return count;
#endif
}
// Joined threads can stay listed briefly while the kernel finishes their exit; a thread the core
// started never leaves, so waiting for the count to settle keeps the check exact.
bool threads_settle(std::size_t expected) {
	for (auto deadline = std::chrono::steady_clock::now() + 5s; threads() != expected;) {
		if (std::chrono::steady_clock::now() >= deadline)
			return false;
		std::this_thread::sleep_for(1ms);
	}
	return true;
}
std::size_t descriptors() {
	std::size_t count = 0;
	for (int fd = 0; fd != 4096; ++fd)
		count += ::fcntl(fd, F_GETFD) != -1;
	return count;
}
// Mappings of files under `store`.
std::size_t mappings(const std::filesystem::path &store) {
	const auto prefix = std::filesystem::canonical(store).string();
	std::size_t count = 0;
#ifdef __APPLE__
	mach_vm_address_t address = 0;
	for (;;) {
		mach_vm_size_t size = 0;
		vm_region_extended_info_data_t info;
		mach_msg_type_number_t length = VM_REGION_EXTENDED_INFO_COUNT;
		mach_port_t object;
		if (mach_vm_region(mach_task_self(), &address, &size, VM_REGION_EXTENDED_INFO,
		                   vm_region_info_t(&info), &length, &object) != KERN_SUCCESS)
			break;
		// Only file-backed regions have a pager; naming the rest is slow under ASan's shadow regions.
		char path[PROC_PIDPATHINFO_MAXSIZE];
		if (info.external_pager && proc_regionfilename(::getpid(), address, path, sizeof path) > 0 &&
		    std::string_view(path).starts_with(prefix))
			++count;
		address += size;
	}
#else
	std::ifstream maps("/proc/self/maps");
	for (std::string line; std::getline(maps, line);)
		count += line.find(prefix) != std::string::npos;
#endif
	return count;
}
struct Opened {
	detent_reader *handle = nullptr;
	explicit Opened(const std::filesystem::path &store) {
		const auto path = store.string();
		detent_open_options options{sizeof options, S(path), nullptr, 0, uint32_t(::getuid())};
		CHECK(detent_open(&options, &handle) == DETENT_OK);
	}
	~Opened() { detent_close(handle); }
};

// T3: 16 threads read while a publisher process writes generations; values never go backwards and
// never come from the future, every status is OK, at most four views live, the core starts no
// thread, and descriptors and mappings return to their pre-open counts after close. The publisher
// is a child process so this process's threads, descriptors and mappings are the reader's alone.
struct Shared {
	std::atomic<std::int64_t> ceiling{0}; // set before each publish: no reader may see more
	std::atomic<bool> go{false}, done{false};
};
void switching(const std::filesystem::path &root) {
	const auto store = root / "switch";
	const auto seconds = env("DETENT_READER_SWITCH_SECONDS", 60);
	const auto publishes = env("DETENT_READER_SWITCH_PUBLISHES", 1000);
	auto *shared = static_cast<Shared *>(
	    ::mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0));
	CHECK(shared != MAP_FAILED);
	new (shared) Shared;
	const pid_t publisher = ::fork();
	CHECK(publisher >= 0);
	if (publisher == 0) {
		{
			detent::testing::GenerationWriter writer(store);
			shared->ceiling = 1;
			if (writer.publish_json(knobs(1)) != 1)
				std::_Exit(4);
			while (!shared->go.load())
				std::this_thread::sleep_for(1ms);
			for (std::uint64_t rev = 2; rev <= publishes; ++rev) {
				shared->ceiling.store(std::int64_t(rev));
				if (writer.publish_json(knobs(std::int64_t(rev))) != rev)
					std::_Exit(5);
			}
		}
		shared->done = true;
		std::_Exit(0);
	}
	for (auto deadline = std::chrono::steady_clock::now() + 60s;
	     !std::filesystem::exists(store / "selected") || !std::filesystem::exists(store / "change");)
		CHECK(std::chrono::steady_clock::now() < deadline);
	std::this_thread::sleep_for(100ms);
	std::thread([] {}).join(); // a sanitizer runtime starts its own thread on the first pthread_create
	const auto fds = descriptors(), maps = mappings(store), before = threads();
	std::atomic<bool> stop{false};
	std::atomic<std::uint64_t> lookups{0}, max_live{0}, refreshes{0}, unavailable{0};
	{
		Opened r(store);
		CHECK(threads() == before);
		std::vector<std::thread> readers;
		for (int i = 0; i != 16; ++i)
			readers.emplace_back([&] {
				std::int64_t last = 0;
				std::uint64_t mine = 0;
				while (!stop.load(std::memory_order_relaxed)) {
					const auto value = get(r.handle);
					CHECK(value >= last && value >= 1 && value <= shared->ceiling.load());
					last = value;
					bool flag = false;
					CHECK(detent_eval(r.handle, S("t:flag"), {nullptr, 0}, {nullptr, 0}, false, &flag) == DETENT_OK &&
					      flag);
					++mine;
				}
				// Every reader reaches the last publish once publishing stops.
				for (auto deadline = std::chrono::steady_clock::now() + 30s; get(r.handle) != std::int64_t(publishes);)
					CHECK(std::chrono::steady_clock::now() < deadline);
				lookups += mine;
			});
		std::thread monitor([&] {
			while (!stop.load(std::memory_order_relaxed)) {
				const auto stats = t::stats(r.handle);
				CHECK(stats.live_views <= 4);
				if (stats.live_views > max_live.load())
					max_live = stats.live_views;
				// A publish storm can outrun attach's retries: unavailable is truthful, anything else is not.
				if (const auto status = detent_refresh(r.handle); status == DETENT_UNAVAILABLE)
					++unavailable;
				else if (status != DETENT_OK) {
					std::fprintf(stderr, "refresh status %d: %s\n", int(status), t::last_failure());
					CHECK(false);
				}
				++refreshes;
				std::this_thread::sleep_for(5ms);
			}
		});
		const auto start = std::chrono::steady_clock::now();
		shared->go = true;
		int status = 0;
		while (!shared->done.load() || std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds)) {
			if (!shared->done.load() && ::waitpid(publisher, &status, WNOHANG) == publisher) {
				std::fprintf(stderr, "publisher exited early: status %d\n", status); // never sets `done`
				std::_Exit(2);
			}
			std::this_thread::sleep_for(10ms);
		}
		const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		stop = true;
		for (auto &reader : readers)
			reader.join();
		monitor.join();
		CHECK(::waitpid(publisher, &status, 0) == publisher);
		CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
		const auto stats = t::stats(r.handle);
		std::cout << "T3: " << lookups.load() << " lookup pairs, " << stats.publishes << " view publishes, "
		          << stats.refused << " refused, " << refreshes.load() << " refreshes (" << unavailable.load() << " unavailable), max live views "
		          << max_live.load() << ", " << publishes << " generations in " << elapsed << " s\n";
		CHECK(stats.live_views <= 4 && stats.generations >= 1 && stats.generations <= 4);
		CHECK(threads_settle(before));
		const auto held_fds = descriptors(), held_maps = mappings(store);
		if (held_fds <= fds || held_maps <= maps)
			std::fprintf(stderr, "held: descriptors %zu (before %zu), mappings %zu (before %zu)\n", held_fds, fds,
			             held_maps, maps);
		CHECK(held_fds > fds);
		CHECK(held_maps > maps);
	}
	CHECK(threads_settle(before) && descriptors() == fds && mappings(store) == maps);
	::munmap(shared, sizeof(Shared));
}

// T3's injected parks: a reader parked between its `current` load and its increment across four
// publishes, and one parked mid-lookup; each resumes without reading a freed view (ASan), and the
// publishes the pinned slot refuses are counted.
void parks(const std::filesystem::path &root) {
	const auto store = root / "parks";
	detent::testing::GenerationWriter writer(store);
	std::int64_t rev = 1;
	CHECK(writer.publish_json(knobs(rev)) == 1);
	Opened r(store);
	CHECK(get(r.handle) == 1);
	auto publish = [&] {
		++rev;
		CHECK(writer.publish_json(knobs(rev)) == std::uint64_t(rev));
		CHECK(detent_refresh(r.handle) == DETENT_OK);
	};
	{
		Park park;
		std::int64_t seen = 0;
		auto reader = parked(park, t::Window::BeforePin, [&] { seen = get(r.handle); });
		for (int i = 0; i != 4; ++i)
			publish();
		CHECK(t::stats(r.handle).refused == 0 && t::stats(r.handle).live_views == 1);
		release(park);
		reader.join();
		CHECK(seen == rev); // its recheck failed, so it retried on the newest view
	}
	{
		// The recheck (D5): a reader that loaded `current` before a publish keeps no pin on the slot
		// that publish found unpinned and is freeing; it pins the new slot instead.
		Park before, pinned, freeing;
		std::int64_t seen = 0;
		const auto s = t::stats(r.handle).current;
		auto reader = parked(
		    before, t::Window::BeforePin, [&] { seen = get(r.handle); }, &pinned, t::Window::Pinned);
		++rev;
		CHECK(writer.publish_json(knobs(rev)) == std::uint64_t(rev));
		auto publisher = parked(freeing, t::Window::Freeing, [&] { CHECK(detent_refresh(r.handle) == DETENT_OK); });
		release(before);
		wait_parked(pinned);
		const auto stats = t::stats(r.handle);
		CHECK(stats.current == s + 1 && stats.pins[s % 4] == 0 && stats.pins[(s + 1) % 4] == 1);
		release(freeing);
		publisher.join();
		release(pinned);
		reader.join();
		CHECK(seen == rev && t::stats(r.handle).live_views == 1);
	}
	{
		Park park;
		std::int64_t seen = 0;
		const auto pinned_rev = rev;
		auto reader = parked(park, t::Window::Pinned, [&] { seen = get(r.handle); });
		const auto refused = t::stats(r.handle).refused;
		for (int i = 0; i != 6; ++i)
			publish();
		const auto stats = t::stats(r.handle);
		// The ring wraps to the pinned slot after three publishes; the rest are refused.
		CHECK(stats.refused == refused + 3 && stats.live_views == 2);
		CHECK(get(r.handle) == pinned_rev + 3); // the installed view still answers
		release(park);
		reader.join();
		CHECK(seen == pinned_rev); // the pinned view stayed mapped
		CHECK(detent_refresh(r.handle) == DETENT_OK && get(r.handle) == rev);
		CHECK(t::stats(r.handle).live_views == 1);
	}
}

// T6's fork cases: the child sees every one of ten publishes and the parent finishes its parked work.
struct Child {
	pid_t pid;
	int to_child, from_child;
	std::int64_t current; // the child's `current` at fork, before it does anything
};
Child fork_child(detent_reader *handle) {
	int down[2], up[2];
	CHECK(::pipe(down) == 0 && ::pipe(up) == 0);
	const pid_t pid = ::fork();
	CHECK(pid >= 0);
	if (pid == 0) {
		::close(down[1]);
		::close(up[0]);
		const std::int64_t initial = std::int64_t(t::stats(handle).current);
		if (::write(up[1], &initial, sizeof initial) != sizeof initial)
			std::_Exit(3);
		std::int64_t expected;
		while (::read(down[0], &expected, sizeof expected) == sizeof expected) {
			std::int64_t value = -1;
			// The child's own lookups switch; a refused publish would leave it behind.
			for (int attempt = 0; attempt != 200 && value != expected; ++attempt) {
				detent_refresh(handle);
				value = -1;
				detent_get(handle, S("t:rev"), -1, &value);
				if (value != expected)
					std::this_thread::sleep_for(1ms);
			}
			const auto stats = t::stats(handle);
			const std::int64_t reply[2] = {value, std::int64_t(stats.live_views)};
			if (::write(up[1], reply, sizeof reply) != sizeof reply)
				std::_Exit(3);
		}
		std::_Exit(0);
	}
	::close(down[0]);
	::close(up[1]);
	Child child{pid, down[1], up[0], -1};
	pollfd ready{child.from_child, POLLIN, 0};
	CHECK(::poll(&ready, 1, 30000) == 1);
	CHECK(::read(child.from_child, &child.current, sizeof child.current) == sizeof child.current);
	return child;
}
std::int64_t ask(Child &child, std::int64_t expected) {
	CHECK(::write(child.to_child, &expected, sizeof expected) == sizeof expected);
	pollfd ready{child.from_child, POLLIN, 0};
	CHECK(::poll(&ready, 1, 30000) == 1);
	std::int64_t reply[2];
	CHECK(::read(child.from_child, reply, sizeof reply) == sizeof reply);
	CHECK(reply[1] <= 4);
	return reply[0];
}
void finish(Child &child) {
	::close(child.to_child);
	int status = 0;
	CHECK(::waitpid(child.pid, &status, 0) == child.pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
	::close(child.from_child);
}
// Ten publishes in the parent, each seen by the child.
std::size_t ten(detent::testing::GenerationWriter &writer, std::int64_t &rev, Child &child) {
	std::size_t seen = 0;
	for (int i = 0; i != 10; ++i) {
		++rev;
		CHECK(writer.publish_json(knobs(rev)) == std::uint64_t(rev));
		seen += ask(child, rev) == rev;
	}
	return seen;
}
void forks(const std::filesystem::path &root) {
	const auto store = root / "fork";
	detent::testing::GenerationWriter writer(store);
	std::int64_t rev = 1;
	CHECK(writer.publish_json(knobs(rev)) == 1);
	Opened r(store);
	CHECK(get(r.handle) == 1);
	auto publish = [&] {
		++rev;
		CHECK(writer.publish_json(knobs(rev)) == std::uint64_t(rev));
		CHECK(detent_refresh(r.handle) == DETENT_OK);
	};
	{
		// A parent thread parked mid-lookup: its count would pin its slot forever in the child.
		Park park;
		std::int64_t seen = 0;
		auto reader = parked(park, t::Window::Pinned, [&] { seen = get(r.handle); });
		const auto current = t::stats(r.handle).current;
		auto child = fork_child(r.handle);
		CHECK(child.current == std::int64_t(current));
		CHECK(ten(writer, rev, child) == 10);
		finish(child);
		release(park);
		reader.join();
		CHECK(seen == rev); // its sequence moved: it switched before answering (D4)
		CHECK(detent_refresh(r.handle) == DETENT_OK && get(r.handle) == rev);
	}
	{
		// A parent thread holding `refreshing` idle: the prepare handler waits it out.
		Park park;
		auto holder = parked(park, t::Window::Refreshing, [&] { CHECK(detent_refresh(r.handle) == DETENT_OK); });
		std::thread releaser([&] {
			std::this_thread::sleep_for(50ms);
			release(park);
		});
		auto child = fork_child(r.handle);
		releaser.join();
		holder.join();
		CHECK(ten(writer, rev, child) == 10);
		finish(child);
		CHECK(get(r.handle) == rev);
	}
	{
		// A parent publish parked after free_view (fork-midfree): the child forks after it completes.
		// A lookup pinned through three publishes loses `refreshing` to a parked holder, so it answers
		// from its pinned view and leaves that view in the next slot with no count for the holder to free.
		Park pin, hold, freed;
		std::int64_t seen = 0;
		const auto pinned_rev = rev;
		auto reader = parked(pin, t::Window::Pinned, [&] { seen = get(r.handle); });
		for (int i = 0; i != 3; ++i)
			publish();
		++rev;
		CHECK(writer.publish_json(knobs(rev)) == std::uint64_t(rev));
		auto publisher = parked(
		    hold, t::Window::Refreshing, [&] { CHECK(detent_refresh(r.handle) == DETENT_OK); }, &freed,
		    t::Window::AfterFree);
		release(pin);
		reader.join();
		CHECK(seen == pinned_rev && t::stats(r.handle).live_views == 2);
		const auto current = t::stats(r.handle).current;
		release(hold);
		wait_parked(freed);
		CHECK(t::stats(r.handle).live_views == 1 && t::stats(r.handle).current == current);
		std::thread releaser([&] {
			std::this_thread::sleep_for(50ms);
			release(freed);
		});
		auto child = fork_child(r.handle);
		releaser.join();
		publisher.join();
		CHECK(t::stats(r.handle).current == current + 1 && get(r.handle) == rev);
		// The child forked only after the parked publish finished: no half-finished publish.
		CHECK(child.current == std::int64_t(current + 1));
		CHECK(ask(child, rev) == rev);
		CHECK(ten(writer, rev, child) == 10);
		finish(child);
		CHECK(detent_refresh(r.handle) == DETENT_OK && get(r.handle) == rev);
	}
}
} // namespace
int main(int argc, char **argv) {
	const auto root = std::filesystem::current_path() / ".scratch" / ("reader-switch-" + std::to_string(::getpid()));
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{root};
	t::install(hook, nullptr);
	const std::string only = argc > 1 ? argv[1] : "";
	// The switch case forks its publisher first, while this process has no other thread.
	if (only.empty() || only == "switch")
		switching(root);
	if (only.empty() || only == "parks")
		parks(root);
	if (only.empty() || only == "forks")
		forks(root);
	std::cout << "ok\n";
	return 0;
}
