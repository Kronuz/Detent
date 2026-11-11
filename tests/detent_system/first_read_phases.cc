// First-read attribution for the A15 gate (docs/perf/first-read-gate.md): the pre-S6 Client's
// first read split into the steps it takes, built from each tree's own reader like first_read_export.
// Usage: first_read_phases --phases DIR NAMESPACE KEY EXPECTED   one fresh-process sample
//        first_read_phases --micro DIR ITERATIONS                warm per-operation means
#include "generation_file_access.hh"
#include "lazy_page_access.hh"
#include "prepared_reader.hh"
#include "resources.hh"
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <sha256.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace l = detent::local;
namespace x = l::detail;
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
unsigned components(const std::filesystem::path &path) {
	unsigned count = 0;
	for (const auto &part : std::filesystem::absolute(path).relative_path())
		count += !part.empty();
	return count;
}
void phases(char **argv) {
	const std::filesystem::path directory(argv[2]);
	const std::string_view name_space(argv[3]), key(argv[4]);
	const auto expected = integer<std::int64_t>(argv[5]);
	const auto uid = ::getuid();
	Mapping mapping;
	x::LazyPageTesting::install(on_window, &mapping);
	std::vector<std::pair<const char *, Mark>> marks{{"start", mark()}};
	(void)l::resources::domain(l::ViewLimits::prepared());
	marks.emplace_back("runtime", mark());
	::close(x::open_publisher_directory(directory, uid));
	marks.emplace_back("trust_cold", mark());
	::close(x::open_publisher_directory(directory, uid));
	marks.emplace_back("trust_warm", mark());
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
	std::cout << "{\"components\":" << components(directory);
	for (std::size_t i = 1; i < marks.size(); ++i) {
		const auto &[phase, at] = marks[i];
		const auto &before = marks[i - 1].second;
		std::cout << ",\"" << phase << "_ns\":" << ns(before.at, at.at) << ",\"" << phase
				  << "_user_us\":" << at.user_us - before.user_us << ",\"" << phase
				  << "_system_us\":" << at.system_us - before.system_us << ",\"" << phase
				  << "_faults\":" << at.minor_faults - before.minor_faults;
	}
	std::cout << ",\"total_ns\":" << ns(marks.front().second.at, marks.back().second.at)
			  << ",\"mapping_ns\":" << mapping.ns << ",\"mappings\":" << mapping.count << "}\n";
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
	if (argc == 6 && std::string_view(argv[1]) == "--phases")
		phases(argv);
	else if (argc == 4 && std::string_view(argv[1]) == "--micro")
		micro(argv);
	else
		throw std::invalid_argument("use --phases DIR NAMESPACE KEY EXPECTED or --micro DIR ITERATIONS");
	return 0;
} catch (const std::exception &e) {
	std::cerr << "first_read_phases: " << e.what() << '\n';
	return 1;
}
