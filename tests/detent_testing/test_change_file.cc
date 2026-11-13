// The store's `change` file (reader.md D4, namespace-delivery D8; docs/mechanisms/change-file.md).
#include "change_file.hh"
#include "detent_testing/generation_writer.hh"
#include "prepared_reader.hh"
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace x = detent::local::detail;
namespace {
void check(bool value, int line) {
	if (!value)
		throw std::runtime_error("change file invariant at line " + std::to_string(line));
}
#define CHECK(value) check(value, __LINE__)
template <class F> bool refused(F &&f) {
	try {
		f();
	} catch (const x::ChangeRefused &) {
		return true;
	}
	return false;
}
struct Directory {
	int fd;
	explicit Directory(const std::filesystem::path &path)
	    : fd(::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)) {
		CHECK(fd >= 0);
	}
	~Directory() { ::close(fd); }
};
std::vector<std::uint64_t> words(const x::ChangeWriter &writer) {
	std::vector<std::uint64_t> out;
	for (std::size_t i = 0; i <= writer.slots(); ++i)
		out.push_back(writer.word(i));
	return out;
}
std::vector<std::uint64_t> on_disk(const std::filesystem::path &path) {
	std::ifstream in(path, std::ios::binary);
	std::vector<std::uint64_t> out;
	std::uint64_t word;
	while (in.read(reinterpret_cast<char *>(&word), sizeof word))
		out.push_back(word);
	return out;
}
using W = std::vector<std::uint64_t>;
std::string knobs(std::int64_t rev) {
	return R"({"schema_version":1,"knobs":{"t:rev":{"default":)" + std::to_string(rev) + "}}}";
}
void writer_shape(const std::filesystem::path &root) {
	const auto dir = root / "shape";
	std::filesystem::create_directories(dir);
	Directory d(dir);
	const auto uid = ::getuid();
	{
		x::ChangeWriter writer(d.fd, 1, uid, 0644);
		struct stat info{};
		CHECK(::stat((dir / "change").c_str(), &info) == 0 && info.st_size == 16 && (info.st_mode & 0777) == 0644);
		CHECK(words(writer) == W({0, 0}));
		const std::uint32_t zero[] = {0};
		writer.publish(zero);
		CHECK(words(writer) == W({1, 1}));
		writer.reserve(4);
		CHECK(writer.slots() == 4 && words(writer) == W({1, 1, 0, 0, 0}));
		const std::uint32_t two_three[] = {2, 3};
		writer.publish(two_three);
		CHECK(words(writer) == W({2, 1, 0, 1, 1}));
		writer.publish({});
		CHECK(words(writer) == W({3, 1, 0, 1, 1}));
		writer.bump_all();
		CHECK(words(writer) == W({4, 2, 1, 2, 2}));
		writer.reserve(2); // never shrinks
		CHECK(writer.slots() == 4);
		// A reader's mapping sees the writer's increments with no remap.
		x::ChangeMapping mapped;
		CHECK(x::ChangeMapping::open(d.fd, uid, mapped) == x::ChangeMapping::Open::Mapped);
		CHECK(mapped.slots() == 4 && mapped.store().load() == 4 && mapped.sequence(3)->load() == 2);
		CHECK(mapped.sequence(3) && !mapped.sequence(4));
		writer.publish(two_three);
		CHECK(mapped.store().load() == 5 && mapped.sequence(2)->load() == 3 && mapped.sequence(1)->load() == 1);
		// A slot past the reader's mapping waits for a remap; the old mapping keeps its words.
		writer.reserve(6);
		CHECK(!mapped.sequence(4) && mapped.store().load() == 5);
	}
	CHECK(on_disk(dir / "change") == W({5, 2, 1, 3, 3, 0, 0}));
	{
		// Reopening keeps every word and the size.
		x::ChangeWriter again(d.fd, 1, uid, 0644);
		CHECK(words(again) == W({5, 2, 1, 3, 3, 0, 0}));
	}
	// The writer refuses an unsafe file or arguments.
	bool invalid = false;
	try {
		x::ChangeWriter bad(d.fd, 1, uid, 0666);
	} catch (const std::invalid_argument &) {
		invalid = true;
	}
	CHECK(invalid);
	CHECK(refused([&] { x::ChangeWriter other(d.fd, 1, uid + 1, 0644); }));
	::chmod((dir / "change").c_str(), 0664);
	CHECK(refused([&] { x::ChangeWriter wide(d.fd, 1, uid, 0644); }));
	x::ChangeMapping mapping;
	CHECK(refused([&] { x::ChangeMapping::open(d.fd, uid, mapping); }));
	::chmod((dir / "change").c_str(), 0644);
	CHECK(::truncate((dir / "change").c_str(), 60) == 0);
	CHECK(refused([&] { x::ChangeWriter ragged(d.fd, 1, uid, 0644); }));
	CHECK(refused([&] { x::ChangeMapping::open(d.fd, uid, mapping); }));
	CHECK(::truncate((dir / "change").c_str(), 4) == 0);
	CHECK(refused([&] { x::ChangeMapping::open(d.fd, uid, mapping); }));
	CHECK(::truncate((dir / "change").c_str(), 8) == 0);
	CHECK(x::ChangeMapping::open(d.fd, uid, mapping) == x::ChangeMapping::Open::Mapped && mapping.slots() == 0);
	// Readers refuse a change the publisher does not own (reader.md D4 rule 4).
	CHECK(refused([&] { x::ChangeMapping::open(d.fd, uid + 1, mapping); }));
	CHECK(::link((dir / "change").c_str(), (dir / "second").c_str()) == 0);
	CHECK(refused([&] { x::ChangeMapping::open(d.fd, uid, mapping); }));
	CHECK(refused([&] { x::ChangeWriter linked(d.fd, 1, uid, 0644); }));
	std::filesystem::remove(dir / "second");
	std::filesystem::remove(dir / "change");
	CHECK(x::ChangeMapping::open(d.fd, uid, mapping) == x::ChangeMapping::Open::Absent);
	std::filesystem::create_symlink(dir / "elsewhere", dir / "change");
	{
		std::ofstream(dir / "elsewhere") << std::string(16, '\0');
		::chmod((dir / "elsewhere").c_str(), 0644);
	}
	CHECK(refused([&] { x::ChangeMapping::open(d.fd, uid, mapping); }));
	bool followed = true;
	try {
		x::ChangeWriter link(d.fd, 1, uid, 0644);
	} catch (const std::system_error &) {
		followed = false;
	}
	CHECK(!followed);
}
struct Order {
	std::filesystem::path store;
	std::vector<std::uint64_t> selected, store_word;
	x::ChangeMapping *mapping = nullptr;
};
void agent_store(const std::filesystem::path &root) {
	const auto store = root / "store";
	const auto uid = ::getuid();
	Order order{store, {}, {}};
	x::ChangeTesting::install(
	    [](void *context) noexcept {
		    auto &seen = *static_cast<Order *>(context);
		    // Every increment follows the rename of `selected` it announces (reader.md D4 rule 3).
		    seen.selected.push_back(x::PreparedReader::selection(seen.store, ::getuid()).descriptor.cursor.revision);
		    seen.store_word.push_back(seen.mapping ? seen.mapping->store().load() : 0);
	    },
	    &order);
	{
		detent::testing::GenerationWriter writer(store);
		// Created with the store before any selected, then bumped once at startup; slot 0 comes with the
		// namespace's first publication.
		CHECK(!std::filesystem::exists(store / "selected"));
		CHECK(on_disk(store / "change") == W({1}));
		Directory d(store);
		x::ChangeMapping mapping;
		CHECK(x::ChangeMapping::open(d.fd, uid, mapping) == x::ChangeMapping::Open::Mapped);
		order.mapping = &mapping;
		for (std::int64_t rev = 1; rev <= 3; ++rev) {
			CHECK(writer.publish_json(knobs(rev)) == std::uint64_t(rev));
			CHECK(mapping.store().load() == std::uint64_t(rev) + 1 &&
			      on_disk(store / "change") == W({std::uint64_t(rev) + 1, std::uint64_t(rev)}));
		}
		CHECK(order.selected == W({1, 2, 3}) && order.store_word == W({1, 2, 3}));
		// The store holds owner.lock exclusively while it is open.
		const int lock = ::open((store / "owner.lock").c_str(), O_RDONLY | O_CLOEXEC);
		CHECK(lock >= 0 && ::flock(lock, LOCK_EX | LOCK_NB) == -1 && errno == EWOULDBLOCK);
		bool second = true;
		try {
			detent::testing::GenerationWriter other(store);
		} catch (const std::exception &) {
			second = false;
		}
		CHECK(!second);
		CHECK(on_disk(store / "change") == W({4, 3}));
		::close(lock);
		order.mapping = nullptr;
	}
	{
		const int lock = ::open((store / "owner.lock").c_str(), O_RDONLY | O_CLOEXEC);
		CHECK(lock >= 0 && ::flock(lock, LOCK_EX | LOCK_NB) == 0);
		::close(lock);
	}
	{
		// A restart bumps every word once more before ready, even with nothing published.
		detent::testing::GenerationWriter writer(store);
		CHECK(on_disk(store / "change") == W({5, 4}));
		CHECK(writer.publish_json(knobs(4)) == 4);
		CHECK(on_disk(store / "change") == W({6, 5}));
		CHECK(order.selected.back() == 4);
	}
	x::ChangeTesting::install(nullptr, nullptr);
	// An unsafe change file keeps the store from opening.
	::chmod((store / "change").c_str(), 0622);
	bool opened = true;
	try {
		detent::testing::GenerationWriter writer(store);
	} catch (const std::exception &) {
		opened = false;
	}
	CHECK(!opened);
	::chmod((store / "change").c_str(), 0600);
	// A store from before the change file gains one at its next start.
	std::filesystem::remove(store / "change");
	{
		detent::testing::GenerationWriter writer(store);
		CHECK(on_disk(store / "change") == W({1, 1}) && writer.revision() == 4);
	}
}
} // namespace
int main() {
	const auto root = std::filesystem::current_path() / ".scratch" / ("change-file-" + std::to_string(::getpid()));
	std::filesystem::remove_all(root);
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{root};
	writer_shape(root);
	agent_store(root);
	std::cout << "ok\n";
	return 0;
}
