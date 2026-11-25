// The oracle scenario of Xapiand's completion-flush-mode durability review (P5): one scenario
// through every PosixIO sync entry point, run under interpose.dylib, which logs every fsync,
// fdatasync, F_FULLFSYNC and F_BARRIERFSYNC the process makes, the test seam bypassed. -DOLD_API
// builds against completion before FlushMode (with full_flush_ordering, or with default options
// under -DOLD_DEFAULT, as Detent built it); otherwise FlushMode::full, or plain with argv[2] ==
// "plain". -DNO_SYNC_DATA leaves out the steps completion before efc368f lacks.
//   oracle_scenario DIRECTORY [full|plain] 2> log
#include "completion/api.h"
#include "completion/native_completion.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace c = kronuz::io::completion;
namespace j = kronuz::journal;

namespace {
void mark(const char* what) { std::fflush(stdout); dprintf(2, "MARK %s\n", what); }
class OnePrimitive final : public c::Operation {
  public:
	OnePrimitive(c::IO& io, c::Request request) : io_(io), request_(std::move(request)) {}
	const c::Request& request() const override { return request_; }
	void submitted() override { submitted_ = true; }
	bool complete(c::Result result) noexcept override { result_ = std::move(result); done_ = true; return true; }
	bool done() const noexcept override { return done_; }
	bool in_flight() const noexcept override { return submitted_ && !done_; }
	c::IO& io() const noexcept override { return io_; }
	c::Result result_;
  private:
	c::IO& io_;
	c::Request request_;
	bool submitted_ = false, done_ = false;
};
c::Request request_for(c::Kind kind, std::shared_ptr<c::File> one, std::vector<std::shared_ptr<c::File>> group) {
	c::Request request;
	request.token.operation = 1;
	request.kind = kind;
	if (kind == c::Kind::Sync
#ifndef NO_SYNC_DATA
		|| kind == c::Kind::SyncData
#endif
	) { request.file = std::move(one); }
	else if (kind != c::Kind::DirectorySync) { request.files = std::move(group); }
	return request;
}
void synchronously(c::IO& io, c::Request request) {
	OnePrimitive operation(io, std::move(request));
	c::drive_synchronously(operation);
	if (operation.result_.error) { std::rethrow_exception(operation.result_.error); }
}
void queued(c::IO& io, c::Request request) {
	auto operation = std::make_shared<OnePrimitive>(io, std::move(request));
	c::NativeQueue queue;
	if (!queue.submit(operation)) { throw std::runtime_error("queue refused"); }
	for (;;) {
		if (auto completion = queue.poll()) { completion->operation->complete(std::move(completion->completion)); break; }
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	if (operation->result_.error) { std::rethrow_exception(operation->result_.error); }
}
class Foreign final : public j::File {
  public:
	explicit Foreign(std::unique_ptr<j::File> inner) : inner_(std::move(inner)) {}
	std::uint64_t size() override { return inner_->size(); }
	std::size_t read_at(std::uint64_t offset, std::span<char> bytes) override { return inner_->read_at(offset, bytes); }
	std::size_t write_at(std::uint64_t offset, std::string_view bytes) override { return inner_->write_at(offset, bytes); }
	void truncate(std::uint64_t size) override { inner_->truncate(size); }
	void sync() override { inner_->sync(); }
  private:
	std::unique_ptr<j::File> inner_;
};
std::unique_ptr<j::PosixIO> open_io(const std::filesystem::path& dir, [[maybe_unused]] bool plain) {
	std::filesystem::create_directory(dir);
	std::filesystem::permissions(dir, std::filesystem::perms::owner_all);
#ifdef OLD_API
	j::PosixIOOptions options;
#ifndef OLD_DEFAULT
	options.full_flush_ordering = true;
#endif
	return std::make_unique<j::PosixIO>(dir, 0600, options);
#else
	return std::make_unique<j::PosixIO>(dir, j::PosixIOOptions{plain ? j::FlushMode::plain : j::FlushMode::full});
#endif
}
} // namespace

int main(int argc, char** argv) {
	if (argc < 2) { std::cerr << "usage: oracle_scenario DIRECTORY [full|plain]\n"; return 2; }
	const bool plain = argc > 2 && std::string(argv[2]) == "plain";
	std::cout << "sizeof(detail::PosixFile) = " << sizeof(j::detail::PosixFile) << '\n';
	const std::filesystem::path base(argv[1]);
	std::filesystem::remove_all(base);
	std::filesystem::create_directories(base);
	auto io = open_io(base / "one", plain);
	auto other = open_io(base / "two", plain);
	mark("acquire_owner(both, create_exclusive)");
	auto owner = io->acquire_owner(c::OwnerLockOptions{c::OwnerLockMode::both, c::OwnerFile::create_exclusive});
	std::shared_ptr<j::File> a = io->create_exclusive("a"), b = io->create_exclusive("b"), x = other->create_exclusive("x");
	for (auto* f : {a.get(), b.get(), x.get()}) { f->write_at(0, "bytes"); }
	std::vector<j::File*> group{a.get(), b.get()}, none;
	mark("File::sync a"); a->sync();
	mark("File::sync_data a"); a->sync_data();
	mark("sync_files a b"); io->sync_files(group);
#ifndef NO_SYNC_DATA
	mark("sync_data_files a b"); io->sync_data_files(group);
#endif
	mark("sync_files_and_directory a b"); io->sync_files_and_directory(group);
	mark("sync_files_and_directory none"); io->sync_files_and_directory(none);
	mark("order_files_and_directory a b"); io->order_files_and_directory(group);
	mark("order_files_and_directory none"); io->order_files_and_directory(none);
	mark("sync_directory (owner held)"); io->sync_directory();
	{
		Foreign foreign(io->open_existing("a"));
		std::vector<j::File*> mixed{&foreign, b.get()};
		mark("order_files_and_directory foreign(a) b"); io->order_files_and_directory(mixed);
		mark("sync_files_and_directory foreign(a) b"); io->sync_files_and_directory(mixed);
	}
	{
		std::vector<j::File*> cross{a.get(), x.get()};
		mark("sync_files a x (x from the second backend, same mode)"); io->sync_files(cross);
		mark("order_files_and_directory a x"); io->order_files_and_directory(cross);
	}
	{
		auto reusable = io->make_closed_file();
		io->create_exclusive_into("r", *reusable);
		reusable->write_at(0, "bytes");
		mark("reusable File::sync r"); reusable->sync();
		mark("reusable File::sync_data r"); reusable->sync_data();
	}
	const std::vector<std::shared_ptr<c::File>> shared_group{a, b};
	for (auto [route, run] : {std::pair{"drive_synchronously", &synchronously}, std::pair{"native queue", &queued}}) {
		for (auto [name, kind] : {std::pair{"Sync a", c::Kind::Sync},
#ifndef NO_SYNC_DATA
				 std::pair{"SyncData a", c::Kind::SyncData},
#endif
				 std::pair{"DirectorySync", c::Kind::DirectorySync}, std::pair{"SyncFiles a b", c::Kind::SyncFiles},
				 std::pair{"OrderFiles a b", c::Kind::OrderFiles}}) {
			mark((std::string(route) + ": " + name).c_str());
			run(*io, request_for(kind, a, shared_group));
		}
	}
	owner.reset();
	mark("sync_directory (no owner)"); io->sync_directory();
	mark("order_files_and_directory none (no owner)"); io->order_files_and_directory(none);
	mark("sync_files_and_directory none (no owner)"); io->sync_files_and_directory(none);
	mark("owner_file create alone (file mode)");
	std::filesystem::remove(base / "one" / "owner.lock");
	auto file_owner = io->acquire_owner(c::OwnerLockOptions{c::OwnerLockMode::file, c::OwnerFile::create_exclusive});
	mark("end");
	return 0;
}
