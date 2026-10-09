#include "detent_distribution/messagepack.hh"
#include "detent_local/builder.hh"
#include "detent_local/reader.hh"
#include "storage_metrics.hh"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sys/resource.h>
#include <unistd.h>
#include <vector>
namespace d = detent::distribution;
namespace l = detent::local;
namespace m = d::messagepack;
using Clock = std::chrono::steady_clock;
std::int64_t micros(Clock::duration value) {
	return std::chrono::duration_cast<std::chrono::microseconds>(value).count();
}
std::uint64_t rss() {
	rusage r{};
	getrusage(RUSAGE_SELF, &r);
#ifdef __APPLE__
	return r.ru_maxrss;
#else
	return std::uint64_t(r.ru_maxrss) * 1024;
#endif
}
int main(int argc, char **argv) {
	try {
		auto requested = argc >= 2 ? std::stoull(argv[1]) : 500000;
		std::uint64_t refreshes = 0;
		if (argc == 4 && std::string_view(argv[2]) == "--refresh")
			refreshes = std::stoull(argv[3]);
		else if (argc > 2)
			throw std::invalid_argument("expected record count [--refresh repetitions]");
		if (requested > 500000 || refreshes > 1000000)
			throw std::invalid_argument("benchmark count exceeds limit");
		std::uint32_t count = requested;
		if (!count || count > 500000)
			throw std::invalid_argument("record count must be 1..500000");
		auto integers = (count + 3) / 4, overrides = count - integers;
		d::Cursor cursor;
		cursor.cluster.fill('c');
		cursor.group.fill('g');
		cursor.restore_epoch.fill('e');
		cursor.scope.fill('s');
		cursor.revision = 1;
		std::string wire;
		auto a = [&](auto n) { m::array(wire, n); };
		auto u = [&](auto n) { m::unsigned_integer(wire, n); };
		auto s = [&](std::string_view text) { m::string(wire, text); };
		a(7);
		u(1);
		u(1);
		a(2);
		u(1);
		u(1);
		a(5);
		for (auto *id : {&cursor.cluster, &cursor.group, &cursor.restore_epoch})
			m::string(wire, {id->data(), id->size()}, true);
		m::string(wire, {cursor.scope.data(), cursor.scope.size()}, true);
		u(1);
		a(4);
		u(1);
		u(count);
		u(overrides);
		u(3 + std::uint64_t(count) * 13 + std::uint64_t(overrides) * 4);
		a(1);
		a(2);
		s("app");
		u(1);
		a(count);
		for (std::uint32_t i = 0; i < count; ++i) {
			char digits[7];
			auto end = std::to_chars(digits, digits + 7, i).ptr;
			std::string key = "app:key";
			key.append(6 - (end - digits), '0');
			key.append(digits, end);
			a(4);
			u(0);
			s(key);
			u(1);
			if (i % 4 == 0) {
				a(2);
				u(1);
				m::signed_integer(wire, -std::int64_t(i));
			} else {
				a(4);
				u(0);
				wire.push_back(i % 2 ? '\xc3' : '\xc2');
				u(25);
				a(1);
				a(3);
				s("beta");
				u(1);
				u(17);
			}
		}
		auto start = Clock::now();
		d::SnapshotDecoder decoder(cursor, {"app"});
		auto input = std::span<const char>(wire);
		while (!input.empty()) {
			d::WorkBudget b{8192, 128};
			decoder.feed(input, b);
		}
		auto source = std::make_shared<d::ValidatedSnapshot>(decoder.finish());
		auto charged = charged_storage(*source);
		auto decoded = Clock::now();
		l::GenerationId generation;
		generation.fill('a');
		l::ImageBuilder builder(source, generation);
		std::uint64_t turns = 0;
		std::int64_t maximum_turn = 0;
		while (!builder.done()) {
			l::ValidationBudget b;
			auto before = Clock::now();
			builder.step(b);
			maximum_turn = std::max(maximum_turn, micros(Clock::now() - before));
			++turns;
		}
		auto built = Clock::now();
		auto bytes = builder.bytes();
		l::ImageValidator validator(bytes, std::span<const char>(*bytes));
		while (!validator.done()) {
			l::ValidationBudget b;
			validator.step(b);
		}
		auto view = validator.take();
		auto validated = Clock::now();
		std::uint64_t oracle = 0;
		constexpr int reads = 100000;
		std::vector<std::pair<std::string, std::uint32_t>> queries;
		queries.reserve(reads);
		for (int i = 0; i < reads; ++i) {
			auto index = (std::uint64_t(i) * 7919 % integers) * 4;
			char digits[7];
			auto end = std::to_chars(digits, digits + 7, index).ptr;
			std::string key = "key";
			key.append(6 - (end - digits), '0');
			key.append(digits, end);
			queries.emplace_back(std::move(key), index);
		}
		auto lookup_begin = Clock::now();
		for (const auto &[key, index] : queries) {
			auto result = view.get("app", key);
			if (!result || result.value() != -std::int64_t(index))
				throw std::runtime_error("lookup oracle failed");
			oracle += index;
		}

		auto finished = Clock::now();
		std::int64_t refresh_us = 0, refresh_cpu_us = 0;
		if (refreshes) {
			auto directory = std::filesystem::current_path() / ".scratch" /
							 ("local-image-refresh-" + std::to_string(::getpid()));
			std::filesystem::create_directories(directory);
			std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
			auto write = [&](const auto &path, std::span<const char> contents) {
				std::ofstream output(path, std::ios::binary | std::ios::trunc);
				output.write(contents.data(), contents.size());
				output.close();
				if (!output)
					throw std::runtime_error("benchmark fixture write failed");
				std::filesystem::permissions(path, std::filesystem::perms::owner_read |
													   std::filesystem::perms::owner_write);
			};
			auto name = l::generation_name(generation);
			write(directory / (name + ".image"), *bytes);
			write(directory / (name + ".lease"), {});
			write(directory / "selected", l::encode_descriptor(builder.descriptor()));
			{
				l::GenerationReader reader(directory);
				if (!reader.refresh())
					throw std::runtime_error("initial refresh failed");
				auto before = Clock::now();
				auto cpu = std::clock();
				for (std::uint64_t i = 0; i < refreshes; ++i)
					if (reader.refresh())
						throw std::runtime_error("unchanged refresh oracle failed");
				refresh_cpu_us = (std::clock() - cpu) * 1000000.0 / CLOCKS_PER_SEC;
				refresh_us = micros(Clock::now() - before);
			}
			std::filesystem::remove_all(directory);
		}
		std::uint64_t retirement_turns = 0;
		std::int64_t maximum_retirement_turn = 0;
		auto retirement_start = Clock::now();
		for (;;) {
			d::WorkBudget budget;
			auto before = Clock::now();
			bool done = source->discard(budget);
			maximum_retirement_turn = std::max(maximum_retirement_turn, micros(Clock::now() - before));
			++retirement_turns;
			if (done)
				break;
		}
		auto retirement_us = micros(Clock::now() - retirement_start);
		std::cout << "{\"records\":" << count << ",\"wire_bytes\":" << wire.size()
				  << ",\"image_bytes\":" << bytes->size() << ",\"decode_us\":" << micros(decoded - start)
				  << ",\"build_us\":" << micros(built - decoded)
				  << ",\"validate_us\":" << micros(validated - built) << ",\"lookup_calls\":" << reads
				  << ",\"lookup_us\":" << micros(finished - lookup_begin) << ",\"build_turns\":" << turns
				  << ",\"max_build_turn_us\":" << maximum_turn << ",\"peak_rss_bytes\":" << rss()
				  << ",\"charged_decode_capacity\":" << charged << ",\"retirement_us\":" << retirement_us
				  << ",\"retirement_turns\":" << retirement_turns
				  << ",\"max_retirement_turn_us\":" << maximum_retirement_turn
				  << ",\"retained_decode_capacity\":" << charged_storage(*source) << ",\"oracle\":" << oracle
				  << ",\"refresh_calls\":" << refreshes << ",\"refresh_us\":" << refresh_us
				  << ",\"refresh_cpu_us\":" << refresh_cpu_us << "}\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
