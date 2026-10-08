#include "detent_distribution/memory_resource.hh"
#include "detent_local/collector.hh"
#include "detent_local/record_reader.hh"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <source_location>
#include <thread>
#include <unistd.h>
namespace l = detent::local;
namespace c = kronuz::io::completion;
namespace d = detent::distribution;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("record invariant failed at line " + std::to_string(where.line()));
}
template <class Action> void rejects(Action action) {
	bool rejected = false;
	try {
		action();
	} catch (const std::exception &) {
		rejected = true;
	}
	check(rejected);
}
class ShortFile final : public c::File {
  public:
	explicit ShortFile(c::File &file) : file_(file) {}
	std::uint64_t size() override { return file_.size(); }
	std::size_t read_at(std::uint64_t offset, std::span<char> bytes) override {
		return file_.read_at(offset, bytes.first(std::min<std::size_t>(1, bytes.size())));
	}
	std::size_t write_at(std::uint64_t offset, std::string_view bytes) override {
		return file_.write_at(offset, bytes);
	}
	void truncate(std::uint64_t length) override { file_.truncate(length); }
	void sync() override { file_.sync(); }
	c::EntryInspection inspect() override { return file_.inspect(); }
	bool try_lease(c::LeaseMode mode) override { return file_.try_lease(mode); }
	void close() override { file_.close(); }

  private:
	c::File &file_;
};
struct Fixture {
	std::shared_ptr<l::CollectorControls> controls;
	ShortFile file;
	l::RecordReader reader;
	explicit Fixture(std::shared_ptr<l::CollectorControls> value)
		: controls(std::move(value)), file(controls->scratch_file()) {}
};
c::EntryInspection valid() {
	c::EntryInspection result;
	result.footprint = {c::EntryKind::Regular, 128, {128}};
	result.device = 1;
	result.inode = 2;
	result.owner = 3;
	result.links = 1;
	result.permissions = 0600;
	return result;
}
class FakeFile final : public c::File {
  public:
	std::uint64_t size() override { return 128; }
	std::size_t read_at(std::uint64_t, std::span<char>) override { return 0; }
	std::size_t write_at(std::uint64_t, std::string_view) override { return 0; }
	void truncate(std::uint64_t) override {}
	void sync() override {}
};
void synthetic() {
	auto file = std::make_shared<FakeFile>();
	for (unsigned scenario = 0; scenario < 26; ++scenario) {
		l::RecordReader reader;
		reader.start("record", 128, 3, {1, 4});
		c::Token token{{}, scenario + 1, 0};
		std::size_t steps = 0;
		while (!reader.done()) {
			auto request = reader.prepare({token.owner, token.operation, ++token.step}, file);
			reader.submitted();
			rejects([&] { reader.start("record", 128, 3, {1, 4}); });
			c::Result stale;
			stale.token = request.token;
			++stale.token.operation;
			check(!reader.complete(std::move(stale)) && reader.in_flight());
			c::Result result;
			result.token = request.token;
			result.inspection = valid();
			if (request.kind == c::Kind::Read)
				result.count = request.destination_bytes.size();
			if (steps == 0) {
				if (scenario == 1)
					result.inspection.reset();
				if (scenario == 2)
					result.inspection->footprint.kind = c::EntryKind::Symlink;
				if (scenario == 3)
					result.inspection->footprint.logical_bytes = 129;
				if (scenario == 4)
					result.inspection->device = 9;
				if (scenario == 5)
					result.inspection->owner = 9;
				if (scenario == 6)
					result.inspection->links = 2;
				if (scenario == 7)
					result.inspection->permissions = 0620;
			}
			if (scenario == 8 && steps == 1)
				result.inspection->inode = 9;
			if (scenario == 9 && steps == 2)
				result.count = 0;
			if (scenario == 10 && steps == 2)
				result.count = 129;
			if (scenario == 11 && steps == 3)
				result.inspection->links = 0;
			if (scenario == 12 && steps == 4)
				result.inspection.reset();
			if (scenario >= 13 && scenario <= 17 && steps == scenario - 13)
				reader.cancel();
			if (scenario == 18 && steps == 3)
				result.inspection->permissions = 0400;
			if (scenario == 19 && steps == 4)
				result.inspection->inode = 9;
			if (scenario == 20 && steps == 2) {
				reader.cancel();
				result.error = std::make_exception_ptr(std::runtime_error("read error"));
			}
			if (scenario >= 21 && steps == scenario - 21)
				result.error = std::make_exception_ptr(std::runtime_error("phase IO error"));
			check(reader.complete(std::move(result)) && !reader.in_flight());
			++steps;
			if (!reader.done()) {
				rejects([&] { reader.prepare(request.token, file); });
				auto wrong_operation = request.token;
				++wrong_operation.operation;
				++wrong_operation.step;
				rejects([&] { reader.prepare(wrong_operation, file); });
			}
			check(steps <= 5);
		}
		const auto expected = scenario == 0	  ? l::RecordReadStatus::Ready
							  : scenario == 1 ? l::RecordReadStatus::Missing
							  : scenario <= 7 ? l::RecordReadStatus::Unsafe
							  : scenario == 9 || scenario == 10 || scenario >= 20
								  ? l::RecordReadStatus::Failed
							  : scenario >= 13 && scenario <= 17 ? l::RecordReadStatus::Cancelled
																 : l::RecordReadStatus::Changed;
		check(reader.status() == expected);
		if (expected == l::RecordReadStatus::Ready)
			check(reader.bytes().size() == 128);
		else
			rejects([&] { reader.bytes(); });
		reader.start("record", 128, 3, {1, 4});
		reader.cancel();
		check(reader.status() == l::RecordReadStatus::Cancelled);
	}
	l::RecordReader reader;
	for (auto name : {"", ".", "..", "/record", "record/name"})
		rejects([&] { reader.start(name, 128, 3, {1, 4}); });
	rejects([&] { reader.start(std::string_view("rec\0ord", 7), 128, 3, {1, 4}); });
	rejects([&] { reader.start(std::string(256, 'x'), 128, 3, {1, 4}); });
	rejects([&] { reader.start("record", 0, 3, {1, 4}); });
	rejects([&] { reader.start("record", 177, 3, {1, 4}); });
	rejects([&] { reader.submitted(); });
}
} // namespace
int main() {
	try {
		synthetic();
		const auto directory =
			std::filesystem::current_path() / ".scratch" / ("record-read-test-" + std::to_string(::getpid()));
		std::filesystem::create_directories(directory);
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		std::array<char, l::RecordReader::capacity> original;
		for (std::size_t at = 0; at < original.size(); ++at)
			original[at] = char(at);
		for (std::size_t length : {64, 128, 176}) {
			const auto path = directory / ("record-" + std::to_string(length));
			std::ofstream stream(path, std::ios::binary);
			stream.write(original.data(), length);
			stream.close();
			std::filesystem::permissions(path, std::filesystem::perms::owner_read |
												   std::filesystem::perms::owner_write);
		}
		auto account = std::make_shared<d::StorageLedger>(32768);
		{
			c::AllocationContext context(d::make_storage_resource(account));
			auto backend = c::make_posix_io(context, directory);
			auto session = l::make_storage_session(context, backend, std::make_shared<int>(1), true);
			auto controls = l::make_collector_controls(session);
			auto state = std::allocate_shared<Fixture>(c::OwnedAllocator<Fixture>(context), controls);
			auto file = std::shared_ptr<c::File>(state, &state->file);
			const auto capacity = account->used();
			auto pressure = account->admit(account->limit() - capacity);
			for (unsigned pass = 0; pass < 100; ++pass) {
				for (std::size_t length : {64, 128, 176}) {
					const auto name = length == 64	  ? "record-64"
									  : length == 128 ? "record-128"
													  : "record-176";
					auto claim = controls->claim(l::CollectorUse::Census);
					backend->open_existing_into(name, controls->scratch_file());
					check(file->try_lease(c::LeaseMode::Shared));
					state->reader.start(name, length, ::geteuid(), session->directory_identity());
					auto token = session->next_operation();
					while (!state->reader.done()) {
						auto request =
							state->reader.prepare({token.owner, token.operation, ++token.step}, file);
						state->reader.submitted();
						check(state->reader.complete(
							kronuz::journal::detail::execute_primitive(*backend, request)));
					}
					check(state->reader.status() == l::RecordReadStatus::Ready);
					check(std::equal(state->reader.bytes().begin(), state->reader.bytes().end(),
									 original.begin()));
					c::Request close;
					close.file = file;
					close.kind = c::Kind::CloseFile;
					check(!kronuz::journal::detail::execute_primitive(*backend, close).error);
					controls->release(claim);
					check(account->used() == account->limit());
				}
			}
			std::cout << "record reader state: " << sizeof(l::RecordReader)
					  << " bytes; 300 saturated-account real reads with one-byte completions\n";
			// Retained request aliases keep the machine and its read destination
			// alive after the caller drops its state, including cancelled originals.
			auto claim = controls->claim(l::CollectorUse::Census);
			backend->open_existing_into("record-128", controls->scratch_file());
			check(file->try_lease(c::LeaseMode::Shared));
			state->reader.start("record-128", 128, ::geteuid(), session->directory_identity());
			auto token = session->next_operation();
			for (unsigned step = 0; step < 2; ++step) {
				auto request = state->reader.prepare({token.owner, token.operation, ++token.step}, file);
				state->reader.submitted();
				check(state->reader.complete(kronuz::journal::detail::execute_primitive(*backend, request)));
			}
			auto request = state->reader.prepare({token.owner, token.operation, ++token.step}, file);
			check(request.kind == c::Kind::Read);
			state->reader.submitted();
			state->reader.cancel();
			std::weak_ptr<Fixture> weak = state;
			state.reset();
			file.reset();
			check(!weak.expired());
			c::Result original;
			std::jthread worker(
				[&] { original = kronuz::journal::detail::execute_primitive(*backend, request); });
			worker.join();
			state = weak.lock();
			check(state && state->reader.in_flight());
			c::Result stale;
			stale.token = original.token;
			--stale.token.step;
			check(!state->reader.complete(std::move(stale)) && state->reader.in_flight());
			check(state->reader.complete(std::move(original)) &&
				  state->reader.status() == l::RecordReadStatus::Cancelled);
			request.kind = c::Kind::CloseFile;
			std::jthread closer(
				[&] { original = kronuz::journal::detail::execute_primitive(*backend, request); });
			closer.join();
			check(!original.error);
			request = {};
			controls->release(claim);
			state.reset();
			check(weak.expired());
			weak.reset();
		}
		check(account->used() == 0);
		std::filesystem::remove_all(directory);
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
