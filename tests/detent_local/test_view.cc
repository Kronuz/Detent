#include "detent/hashing.hh"
#include "detent_local/generation.hh"
#include "detent_local/view.hh"
#include <algorithm>
#include <chrono>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sha256.h>
#include <stdexcept>
#include <sys/file.h>
#include <thread>
#include <unistd.h>
namespace l = detent::local;
void check(bool value) {
	if (!value)
		throw std::runtime_error("local view invariant failed");
}
std::shared_ptr<std::string> image() {
	l::Header h;
	h.layout = l::calculate_layout(1, 2, 2, 3 + 8 + 5 + 4 + 9);
	h.cursor.cluster.fill('c');
	h.cursor.group.fill('g');
	h.cursor.restore_epoch.fill('e');
	h.cursor.scope.fill('s');
	h.cursor.revision = 9;
	auto bytes = std::make_shared<std::string>(h.layout.length, '\0');
	auto header = l::encode_header(h);
	std::copy(header.begin(), header.end(), bytes->begin());
	std::span<char> out(*bytes);
	auto set = [&](auto offset, auto value, unsigned width) { l::write_integer(out, offset, value, width); };
	std::uint64_t used = 0;
	auto name = [&](auto row, std::string_view value) {
		set(row, used, 8);
		set(row + 8, value.size(), 4);
		std::copy(value.begin(), value.end(), bytes->begin() + h.layout.string_offset + used);
		used += value.size();
	};
	name(h.layout.namespace_offset, "app");
	set(h.layout.namespace_offset + 16, 9, 8);
	auto r = h.layout.record_offset;
	name(r, "app:bool");
	set(r + 16, 8, 8);
	set(r + 40, 2, 4);
	set(r + 45, 1, 1);
	set(r + 46, 50, 1);
	auto o = h.layout.override_offset;
	name(o, "force");
	set(o + 13, 1, 1);
	name(o + 24, "ramp");
	set(o + 24 + 12, 1, 1);
	set(o + 24 + 13, 100, 1);
	r += 48;
	name(r, "app:count");
	set(r + 16, 9, 8);
	set(r + 24, std::uint64_t(-17), 8);
	set(r + 32, 2, 8);
	set(r + 44, 1, 1);
	check(used == h.layout.strings);
	return bytes;
}
l::ReadView validate(std::shared_ptr<std::string> bytes) {
	l::ImageValidator validator(bytes, std::span<const char>(*bytes));
	std::size_t turns = 0;
	while (!validator.done()) {
		l::ValidationBudget budget{1, 1};
		validator.step(budget);
		check(++turns < 1000);
	}
	return validator.take();
}
int main() {
	try {
		{
			auto baseline = std::make_shared<detent::distribution::StorageLedger>(1024 * 1024);
			auto account = std::make_shared<detent::distribution::StorageLedger>(16384);
			std::atomic<bool> destroyed{false}, wrong_thread{false};
			detent::distribution::RetirementRuntime runtime(baseline, 1);
			{
				auto domain = runtime.domain(account, 1);
				struct TrackedLease {
					std::shared_ptr<std::string> bytes;
					detent::distribution::RetirementRuntime &runtime;
					std::atomic<bool> &destroyed, &wrong;
					~TrackedLease() {
						wrong.store(!runtime.on_worker());
						destroyed.store(true);
					}
				};
				auto lease = std::make_shared<TrackedLease>(image(), runtime, destroyed, wrong_thread);
				l::ViewLimits limits;
				limits.retirement_domain = &domain;
				l::ImageValidator validator(lease, std::span<const char>(*lease->bytes), limits);
				while (!validator.done()) {
					l::ValidationBudget budget;
					validator.step(budget);
				}
				auto view = validator.take();
				lease.reset();
				auto drain = domain.close();
				check(!drain.done() && domain.residents() == 1);
				auto retained = view;
				view = {};
				check(!destroyed && retained.get("app", "count").value() == -17);
				std::jthread sdk_caller([held = std::move(retained)]() mutable { held = {}; });
				drain.wait();
				check(destroyed && !wrong_thread && domain.residents() == 0);
			}
			runtime.shutdown();
			check(account->used() == 0);
		}
		auto bytes = image();
		auto view = validate(bytes);
		check(view.size() == 2 && view.cursor().revision == 9);
		bytes.reset();
		check(view.get("app", "count").value() == -17);
		check(!view.eval("app", "bool").value());
		check(view.eval("app", "bool", {}, "force").value());
		check(!view.eval("app", "bool", {}, "ramp").value());
		check(view.eval("app", "bool", "host", "ramp").value());
		check(view.eval("app", "bool", "host").value() == (detent::stable_bucket("app:bool", "host") < 50));
		check(view.get("app", "bool").error().category == detent::ErrorCategory::WrongType);
		check(view.get("other", "count").error().category == detent::ErrorCategory::MissingKnob);
		check(!l::ReadView{}.get("app", "count"));
		auto copy = view;
		view = {};
		check(copy.get("app", "count").value() == -17);
		auto good = image();
		auto h = l::decode_header(std::span<const char>(*good));
		for (auto offset :
			 {h.layout.namespace_offset + 12, h.layout.record_offset + 12, h.layout.record_offset + 16,
			  h.layout.record_offset + 32, h.layout.record_offset + 44, h.layout.record_offset + 47,
			  h.layout.override_offset + 12, h.layout.override_offset + 14, h.layout.string_offset}) {
			auto broken = std::make_shared<std::string>(*good);
			(*broken)[offset] = char(0xff);
			bool rejected = false;
			try {
				validate(broken);
			} catch (const std::exception &) {
				rejected = true;
			}
			check(rejected);
		}
		auto wrong = h.cursor;
		wrong.scope.fill('x');
		bool rejected = false;
		try {
			l::ImageValidator v(good, std::span<const char>(*good), {}, wrong);
		} catch (const std::exception &) {
			rejected = true;
		}
		check(rejected);
		l::ImageValidator incomplete(good, std::span<const char>(*good));
		rejected = false;
		try {
			incomplete.take();
		} catch (const std::exception &) {
			rejected = true;
		}
		check(rejected);
		auto directory =
			std::filesystem::current_path() / ".scratch" / ("generation-test-" + std::to_string(::getpid()));
		std::filesystem::create_directories(directory);
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		l::Descriptor descriptor;
		descriptor.generation.fill('a');
		descriptor.length = good->size();
		descriptor.cursor = h.cursor;
		descriptor.profile = h.profile;
		SHA256 sha;
		sha.add(good->data(), good->size());
		unsigned char digest[32];
		sha.getHash(digest);
		std::copy_n(reinterpret_cast<char *>(digest), 32, descriptor.checksum.begin());
		auto raw = l::encode_descriptor(descriptor);
		check(l::decode_descriptor(raw).cursor == descriptor.cursor);
		auto name = l::generation_name(descriptor.generation);
		{
			std::ofstream file(directory / (name + ".image"), std::ios::binary);
			file.write(good->data(), good->size());
		}
		{
			std::ofstream file(directory / (name + ".lease"));
		}
		{
			std::ofstream file(directory / "selected", std::ios::binary);
			file.write(raw.data(), raw.size());
		}
		for (const auto &entry : std::filesystem::directory_iterator(directory))
			std::filesystem::permissions(entry.path(), std::filesystem::perms::owner_read |
														   std::filesystem::perms::owner_write);
		auto mapped = l::open_generation(directory);
		check(mapped.get("app", "count").value() == -17);
		int lease = ::open((directory / (name + ".lease")).c_str(), O_RDONLY | O_CLOEXEC);
		check(lease >= 0);
		check(::flock(lease, LOCK_EX | LOCK_NB) != 0);
		auto retained = mapped;
		mapped = {};
		check(::flock(lease, LOCK_EX | LOCK_NB) != 0);
		retained = {};
		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (::flock(lease, LOCK_EX | LOCK_NB) != 0) {
			check(std::chrono::steady_clock::now() < deadline);
			std::this_thread::yield();
		}
		::close(lease);
		{
			std::fstream file(directory / (name + ".image"), std::ios::binary | std::ios::in | std::ios::out);
			file.put('X');
		}
		rejected = false;
		try {
			l::open_generation(directory);
		} catch (const std::exception &) {
			rejected = true;
		}
		check(rejected);
		std::filesystem::remove_all(directory);
		std::cout << "validated pinned local views passed\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
