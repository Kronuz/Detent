// A3 read harness (docs/perf/one-knob-type.md), built in both trees: -DONE_KNOB_MAIN selects main's
// eval/get, otherwise the one-knob get. Reads a 1-namespace store exported by first_read_export.
//   one_knob_read first STORE NAME BOOL     one fresh Client, ns from construction to the first value
//   one_knob_read steady STORE CORPUS Q R   R timed passes over Q sampled queries after one warm pass
#include "detent/client.hh"
#include "detent_local/client_source.hh"
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
void require(bool value, const char *what) {
	if (!value)
		throw std::runtime_error(what);
}
std::uint64_t big(const std::string &data, std::size_t at, unsigned width) {
	std::uint64_t value = 0;
	for (unsigned i = 0; i != width; ++i)
		value = value << 8 | static_cast<unsigned char>(data[at + i]);
	return value;
}
struct Knob {
	std::string name;
	bool boolean;   // main's type byte; the one-knob tree ignores it
};
// The corpus's definitions as the 1-namespace store names them, "primary:<local name>".
std::vector<Knob> knobs(const char *path) {
	std::ifstream file(path, std::ios::binary);
	const std::string data{std::istreambuf_iterator<char>(file), {}};
	require(data.size() >= 32 && data.compare(0, 8, "DTCORP01") == 0, "not a DTCORP01 corpus");
	const auto count = big(data, 8, 8), keys = big(data, 16, 8), bodies = big(data, 24, 8);
	const auto key_at = 32 + 4 * (count + 1), body_offsets = key_at + keys, body_at = body_offsets + 4 * (count + 1);
	require(body_at + bodies + count == data.size(), "corpus framing");
	std::vector<Knob> result;
	for (std::size_t i = 0; i != count; ++i) {
		if (data[body_at + bodies + i] != 2)
			continue;
		const auto k0 = big(data, 32 + 4 * i, 4), k1 = big(data, 36 + 4 * i, 4);
		const std::string key = data.substr(key_at + k0, k1 - k0);
		const auto local = key.substr(key.find('\0') + 1);
		const auto body = big(data, body_offsets + 4 * i, 4);
		result.push_back({"primary:" + local.substr(0, local.size() - 1), data[body_at + body + 8] == 1});
	}
	return result;
}
detent::Client open(const char *store) {
	detent::InitOptions options;
	options.read_source = detent::local::make_managed_read_source(store, ::getuid());
	return detent::Client(std::move(options));
}
struct Query {
	const Knob *knob;
	std::string hash;
	std::optional<std::string> group;
};
std::int64_t read(const detent::Client &client, const Query &query) {
	const std::optional<std::string_view> hash = query.hash, group = query.group;
#ifdef ONE_KNOB_MAIN
	if (query.knob->boolean) {
		const auto value = client.eval(query.knob->name, hash, group);
		require(value.has_value(), "read failed");
		return value.value();
	}
	const auto value = client.get(query.knob->name, group);
#else
	const auto value = client.get(query.knob->name, hash, group);
#endif
	require(value.has_value(), "read failed");
	return value.value();
}
} // namespace

int main(int argc, char **argv) try {
	const std::string mode = argc > 1 ? argv[1] : "";
	if (mode == "first" && argc == 5) {
		const Knob knob{argv[3], std::string_view(argv[4]) == "1"};
		const Query query{&knob, "user-1", std::nullopt};
		const auto begin = Clock::now();
		const auto client = open(argv[2]);
		const auto value = read(client, query);
		const auto end = Clock::now();
		std::cout << "{\"first_ns\":" << std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()
				  << ",\"value\":" << value << "}\n";
		return 0;
	}
	require(mode == "steady" && argc == 6, "usage: one_knob_read first STORE NAME BOOL | steady STORE CORPUS Q R");
	const auto all = knobs(argv[3]);
	const auto count = std::stoull(argv[4]), rounds = std::stoull(argv[5]);
	std::mt19937_64 random(0x4f4e454b4e4f42);
	std::vector<Query> queries;
	const std::optional<std::string> groups[] = {std::nullopt, "ramp", "force-on", "unknown"};
	for (std::size_t i = 0; i != count; ++i)
		queries.push_back({&all[random() % all.size()], "user-" + std::to_string(random() % 1000000), groups[i % 4]});
	const auto client = open(argv[2]);
	std::int64_t checksum = 0;
	for (const auto &query : queries)
		checksum += read(client, query);
	const auto &probe = all[all.size() / 2];
	std::cout << "{\"probe\":\"" << probe.name << "\",\"probe_boolean\":" << probe.boolean << ",\"queries\":" << count
			  << ",\"checksum\":" << checksum << ",\"ns_per_read\":[";
	for (std::size_t round = 0; round != rounds; ++round) {
		std::int64_t sum = 0;
		const auto begin = Clock::now();
		for (const auto &query : queries)
			sum += read(client, query);
		const auto end = Clock::now();
		require(sum == checksum, "values changed between passes");
		std::cout << (round ? "," : "")
				  << double(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()) / count;
	}
	std::cout << "]}\n";
	return 0;
} catch (const std::exception &error) {
	std::cerr << "one_knob_read: " << error.what() << '\n';
	return 1;
}
