// First-read gate (A16, docs/perf/first-read-gate.sh) and its attribution: one fresh-process first
// read, built from each tree. Built as first_read_core (FIRST_READ_CORE) it reads through
// libdetent_reader; otherwise through the pre-S6 Client's steps, and it also builds and publishes the
// gate's store.
// Usage: first_read_core --core DIR NAME EXPECTED                    detent_open, first detent_get
//        first_read_phases --client DIR NAMESPACE KEY EXPECTED       the Client's first read
//        first_read_phases --phases DIR NAMESPACE KEY EXPECTED       the same, trust walk split out
//        first_read_phases --micro DIR ITERATIONS                    warm per-operation means
//        first_read_phases --generate DIR NAMESPACES KNOBS           the gate's store
//        first_read_phases --publish DIR NAMESPACES                  a running publisher, until killed
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <unistd.h>
#include <utility>
#include <vector>

using Clock = std::chrono::steady_clock;
namespace {
template <class Integer> Integer integer(std::string_view text) {
	Integer value{};
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (error != std::errc{} || end != text.data() + text.size())
		throw std::invalid_argument("invalid integer");
	return value;
}
struct Mark {
	Clock::time_point at;
	std::int64_t user_us, system_us, minor_faults;
};
Mark mark() {
	rusage usage{};
	::getrusage(RUSAGE_SELF, &usage);
	const auto us = [](const timeval &t) { return std::int64_t(t.tv_sec) * 1000000 + t.tv_usec; };
	return {Clock::now(), us(usage.ru_utime), us(usage.ru_stime), usage.ru_minflt};
}
std::int64_t ns(Clock::time_point from, Clock::time_point to) {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(to - from).count();
}
unsigned components(const std::filesystem::path &path) {
	unsigned count = 0;
	for (const auto &part : std::filesystem::absolute(path).relative_path())
		count += !part.empty();
	return count;
}
using Marks = std::vector<std::pair<const char *, Mark>>;
// One JSON line: each phase's wall time, CPU and minor faults, then total_ns; the caller closes it.
void print(const std::filesystem::path &directory, const Marks &marks) {
	std::cout << "{\"components\":" << components(directory);
	for (std::size_t i = 1; i < marks.size(); ++i) {
		const auto &[phase, at] = marks[i];
		const auto &before = marks[i - 1].second;
		std::cout << ",\"" << phase << "_ns\":" << ns(before.at, at.at) << ",\"" << phase
				  << "_user_us\":" << at.user_us - before.user_us << ",\"" << phase
				  << "_system_us\":" << at.system_us - before.system_us << ",\"" << phase
				  << "_faults\":" << at.minor_faults - before.minor_faults;
	}
	std::cout << ",\"total_ns\":" << ns(marks.front().second.at, marks.back().second.at);
}
} // namespace

#ifdef FIRST_READ_CORE
#include "detent/reader.h"
int main(int argc, char **argv) try {
	if (argc != 5 || std::string_view(argv[1]) != "--core")
		throw std::invalid_argument("use --core DIR NAME EXPECTED");
	const std::string_view directory(argv[2]), name(argv[3]);
	const auto expected = integer<std::int64_t>(argv[4]);
	Marks marks{{"start", mark()}};
	const detent_open_options options{sizeof options, {directory.data(), directory.size()}, nullptr, 0,
									  std::uint32_t(::getuid())};
	detent_reader *reader = nullptr;
	if (const auto status = detent_open(&options, &reader); status != DETENT_OK)
		throw std::runtime_error(std::string("detent_open: ") + detent_status_name(status));
	marks.emplace_back("open", mark());
	std::int64_t value = 0;
	const auto status = detent_get(reader, {name.data(), name.size()}, {}, {}, -1, &value);
	marks.emplace_back("lookup", mark());
	if (status != DETENT_OK || value != expected)
		throw std::runtime_error(std::string("first value differs: ") + detent_status_name(status));
	print(directory, marks);
	std::cout << "}\n";
	detent_close(reader);
	return 0;
} catch (const std::exception &e) {
	std::cerr << "first_read_core: " << e.what() << '\n';
	return 1;
}
#else
#include "detent_testing/generation_writer.hh"
#include "generation_file_access.hh"
#include "lazy_page_access.hh"
#include "prepared_reader.hh"
#include "resources.hh"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <fcntl.h>
#include <sha256.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <thread>

namespace l = detent::local;
namespace x = l::detail;

namespace {
struct Mapping {
	Clock::time_point miss;
	std::int64_t ns = 0, count = 0;
};
void on_window(x::LazyPageWindow window, void *context) noexcept {
	auto &mapping = *static_cast<Mapping *>(context);
	if (window == x::LazyPageWindow::Miss)
		mapping.miss = Clock::now();
	else if (window == x::LazyPageWindow::Publish) {
		mapping.ns += ns(mapping.miss, Clock::now());
		++mapping.count;
	}
}
// The Client's refresh worker: waits on a stop token until destruction.
struct Worker {
	std::mutex mutex;
	std::condition_variable_any wake;
	std::optional<std::jthread> thread;
	void start() {
		thread.emplace([this](std::stop_token stop) {
			std::unique_lock lock(mutex);
			wake.wait_for(lock, stop, std::chrono::milliseconds(200), [] { return false; });
		});
	}
};
// The Client's first read; `walks` adds the directory trust walk cold and warm ahead of attach.
void phases(char **argv, bool walks) {
	const std::filesystem::path directory(argv[2]);
	const std::string_view name_space(argv[3]), key(argv[4]);
	const auto expected = integer<std::int64_t>(argv[5]);
	const auto uid = ::getuid();
	Mapping mapping;
	x::LazyPageTesting::install(on_window, &mapping);
	Marks marks{{"start", mark()}};
	(void)l::resources::domain(l::ViewLimits::prepared());
	marks.emplace_back("runtime", mark());
	if (walks) {
		::close(x::open_publisher_directory(directory, uid));
		marks.emplace_back("trust_cold", mark());
		::close(x::open_publisher_directory(directory, uid));
		marks.emplace_back("trust_warm", mark());
	}
	auto generation = x::PreparedReader::open(directory, uid, l::ViewLimits::prepared());
	marks.emplace_back("attach", mark());
	Worker worker;
	worker.start();
	marks.emplace_back("thread", mark());
	const auto value = generation.view.try_get(name_space, key);
	marks.emplace_back("lookup", mark());
	if (!value || !*value || value->value() != expected)
		throw std::runtime_error("first value differs");
	x::LazyPageTesting::install(nullptr, nullptr);
	print(directory, marks);
	std::cout << ",\"mapping_ns\":" << mapping.ns << ",\"mappings\":" << mapping.count << "}\n";
}
// The gate's store: KNOBS int knobs `n<i % NAMESPACES>:k<i>` = 3i + 1, and `n0:tick` = 0.
std::vector<std::string> scope(std::uint64_t namespaces) {
	std::vector<std::string> names;
	for (std::uint64_t j = 0; j != namespaces; ++j)
		names.push_back("n" + std::to_string(j));
	std::sort(names.begin(), names.end());
	return names;
}
void generate(char **argv) {
	const auto namespaces = integer<std::uint64_t>(argv[3]), knobs = integer<std::uint64_t>(argv[4]);
	std::string json = R"({"schema_version":1,"knobs":{"n0:tick":{"default":0})";
	for (std::uint64_t i = 0; i != knobs; ++i)
		json += ",\"n" + std::to_string(i % namespaces) + ":k" + std::to_string(i) +
				"\":{\"default\":" + std::to_string(3 * i + 1) + "}";
	json += "}}";
	detent::testing::GenerationWriter writer(argv[2]);
	std::cout << "{\"revision\":" << writer.publish_json(json) << ",\"namespaces\":" << namespaces
			  << ",\"knobs\":" << knobs << "}\n";
}
// A running publisher: a one-knob delta to `n0` every 20 ms, collected every 16, until killed.
[[noreturn]] void publish(char **argv) {
	detent::testing::GenerationWriter writer(argv[2]);
	const auto names = scope(integer<std::uint64_t>(argv[3]));
	for (std::int64_t tick = 1;; ++tick) {
		writer.publish_delta(names, "n0", {{"n0:tick", tick}}, {});
		if (tick == 1)
			std::cout << "publishing" << std::endl;
		if (tick % 16 == 0)
			writer.collect();
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}
template <class F> double mean_ns(std::uint64_t iterations, F &&f) {
	for (std::uint64_t i = 0; i != std::min<std::uint64_t>(iterations, 16); ++i)
		f();
	const auto begin = Clock::now();
	for (std::uint64_t i = 0; i != iterations; ++i)
		f();
	return double(ns(begin, Clock::now())) / double(iterations);
}
void micro(char **argv) {
	const std::filesystem::path directory(argv[2]);
	const auto iterations = integer<std::uint64_t>(argv[3]);
	const auto uid = ::getuid();
	std::filesystem::path pack, lease;
	for (const auto &entry : std::filesystem::directory_iterator(directory)) {
		if (pack.empty() && entry.path().extension() == ".pack")
			pack = entry.path();
		if (lease.empty() && entry.path().extension() == ".lease")
			lease = entry.path();
	}
	x::RecoveryFile dir(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
	x::RecoveryFile selected(::openat(dir.fd, "selected", O_RDONLY | O_CLOEXEC));
	x::RecoveryFile held(::open(lease.c_str(), O_RDONLY | O_CLOEXEC));
	x::RecoveryFile payload(::open(pack.c_str(), O_RDONLY | O_CLOEXEC));
	struct stat info{};
	if (dir.fd < 0 || selected.fd < 0 || held.fd < 0 || payload.fd < 0 || ::fstat(payload.fd, &info) != 0)
		throw std::runtime_error("micro inputs missing");
	std::array<char, 176> record{};
	std::vector<char> page(4096, 'p');
	std::cout << "{\"components\":" << components(directory) << ",\"iterations\":" << iterations;
	const auto row = [&](const char *name, double value) { std::cout << ",\"" << name << "_ns\":" << value; };
	row("getrusage", mean_ns(iterations, [] { rusage u{}; ::getrusage(RUSAGE_SELF, &u); }));
	row("fstatat", mean_ns(iterations, [&] { struct stat s{}; ::fstatat(dir.fd, "selected", &s, AT_SYMLINK_NOFOLLOW); }));
	row("openat_close", mean_ns(iterations, [&] { ::close(::openat(dir.fd, "selected", O_RDONLY | O_CLOEXEC | O_NOFOLLOW)); }));
	row("fstat", mean_ns(iterations, [&] { struct stat s{}; ::fstat(selected.fd, &s); }));
	row("pread_176", mean_ns(iterations, [&] { (void)::pread(selected.fd, record.data(), record.size(), 0); }));
	row("acl_file", mean_ns(iterations, [&] { (void)detent::internal::mode_only_acl(selected.fd); }));
	row("acl_directory", mean_ns(iterations, [&] {
		(void)detent::internal::acceptable_acl(dir.fd, detent::internal::AclPolicy::DenialsAllowed);
	}));
	row("flock_sh_un", mean_ns(iterations, [&] { ::flock(held.fd, LOCK_SH | LOCK_NB); ::flock(held.fd, LOCK_UN); }));
	row("mmap_touch_munmap", mean_ns(iterations, [&] {
		auto *bytes = static_cast<volatile char *>(::mmap(nullptr, info.st_size, PROT_READ, MAP_SHARED, payload.fd, 0));
		(void)bytes[0];
		::munmap(const_cast<char *>(bytes), info.st_size);
	}));
	row("sha256_4k", mean_ns(iterations, [&] {
		SHA256 hash;
		hash.add(page.data(), page.size());
		std::array<unsigned char, 32> digest{};
		hash.getHash(digest.data());
	}));
	row("thread_create_join", mean_ns(iterations, [] { std::jthread([] {}); }));
	row("worker_start_stop", mean_ns(iterations, [] { Worker w; w.start(); }));
	row("trust_walk", mean_ns(iterations, [&] { ::close(x::open_publisher_directory(directory, uid)); }));
	row("attach", mean_ns(iterations, [&] { (void)x::PreparedReader::open(directory, uid, l::ViewLimits::prepared()); }));
	std::cout << "}\n";
}
} // namespace

int main(int argc, char **argv) try {
	const std::string_view mode = argc > 1 ? argv[1] : "";
	if (argc == 6 && (mode == "--phases" || mode == "--client"))
		phases(argv, mode == "--phases");
	else if (argc == 4 && mode == "--micro")
		micro(argv);
	else if (argc == 5 && mode == "--generate")
		generate(argv);
	else if (argc == 4 && mode == "--publish")
		publish(argv);
	else
		throw std::invalid_argument("use --client|--phases DIR NAMESPACE KEY EXPECTED, --micro DIR ITERATIONS, "
									"--generate DIR NAMESPACES KNOBS or --publish DIR NAMESPACES");
	return 0;
} catch (const std::exception &e) {
	std::cerr << "first_read_phases: " << e.what() << '\n';
	return 1;
}
#endif
