// The relay's pages tier at scale (dense distribution D7): memory and build time for a cold build,
// head moves, and the page window they leave, through NamespaceRelay's public API over a scripted
// upstream that serves real history files.
//   detent_relay_window_bench DIR NAMESPACES KEYS MOVE...   KEYS per namespace (history files
//   hold at most 500,000 changes); MOVE: s<N> N scattered keys, c<N> N clustered keys
// A cold pages reader of every namespace builds them all; each move writes one seal to the first
// namespace and asks a cold pages reader of it, which builds its head within the answer. Prints
// per step: build wall and CPU ms, live heap, resident and peak MB.
#include "detent_relay/namespace_relay.hh"
#include "history_format.hh"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mach/mach.h>
#include <malloc/malloc.h>
#include <sys/resource.h>
#include <sys/stat.h>

namespace d = detent::distribution;
namespace r = detent::relay;
namespace h = detent::authority::history;
namespace c = kronuz::io::completion;

namespace {
d::Identity identity(char fill) {
	d::Identity out;
	out.fill(fill);
	return out;
}
const d::Identity cluster = identity('C');

class Upstream final : public r::Upstream {
  public:
	std::uint64_t seal = 1;
	void create(const std::string &name, std::size_t keys) {
		auto &n = namespaces_[name];
		n.base = {name, identity('i'), 0, keys, {}};
		n.base.changes.reserve(keys);
		for (std::size_t i = 0; i < keys; ++i)
			n.base.changes.push_back(change(name, i, i + 1, int(i)));
		n.files.push_back(add(r::MirrorFile::Kind::Base, "base-" + name, 0, keys, h::encode_base(n.base)));
		n.base.changes.clear();
		n.base.changes.shrink_to_fit();
		++seal;
	}
	void write(const std::string &name, const std::vector<std::size_t> &keys, int salt) {
		auto &n = namespaces_.at(name);
		const auto first = n.files.back().last + 1;
		h::Block block{name, identity('i'), first, first + keys.size() - 1, {}};
		for (std::size_t j = 0; j < keys.size(); ++j)
			block.changes.push_back(change(name, keys[j], first + j, salt + int(keys[j])));
		n.files.push_back(add(r::MirrorFile::Kind::Section, "seal-" + std::to_string(seal) + "#" + name, block.first,
							  block.last, h::encode_block(block)));
		++seal;
	}
	r::MirrorTable layouts(std::span<const std::string> names) override {
		r::MirrorTable out{0, seal, {}};
		for (const auto &name : names)
			if (const auto found = namespaces_.find(name); found != namespaces_.end())
				out.entries.push_back({name, identity('i'), seal, seal, found->second.base.last, "policy-1",
									   found->second.files});
		return out;
	}
	std::string fetch(std::string_view, const r::MirrorFile &file) override { return bytes_.at(file.name); }
	std::string frames(std::span<const r::Position>) override { return d::encode_session_frames(std::nullopt, {}); }

  private:
	struct Namespace {
		h::Block base;
		std::vector<r::MirrorFile> files;
	};
	static h::Change change(const std::string &name, std::size_t key, std::uint64_t revision, int value) {
		char text[16];
		std::snprintf(text, sizeof text, "k%07zu", key);
		detent::KnobDef definition{std::int64_t(value)};
		if (key % 7 == 3)
			definition.overrides.emplace("group-" + std::to_string(key % 5), std::int64_t(value * 3));
		return {name + ":" + text, revision, detent::authority::encode_definition(definition)};
	}
	r::MirrorFile add(r::MirrorFile::Kind kind, std::string name, std::uint64_t first, std::uint64_t last,
					  std::string bytes) {
		r::MirrorFile file{kind, name, first, last, bytes.size(), r::digest_of(bytes)};
		bytes_[std::move(name)] = std::move(bytes);
		return file;
	}
	std::map<std::string, Namespace> namespaces_;
	std::map<std::string, std::string> bytes_;
};

double resident_mb() {
	mach_task_basic_info_data_t info{};
	mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
	task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count);
	return double(info.resident_size) / (1 << 20);
}
double peak_mb() {
	rusage usage{};
	getrusage(RUSAGE_SELF, &usage);
	return double(usage.ru_maxrss) / (1 << 20); // bytes on macOS
}
// Live heap bytes: what the process holds now, which resident memory does not show once freed pages stay mapped.
double heap_mb() {
	malloc_statistics_t stats{};
	malloc_zone_statistics(nullptr, &stats);
	return double(stats.size_in_use) / (1 << 20);
}
double cpu_ms() {
	rusage usage{};
	getrusage(RUSAGE_SELF, &usage);
	return (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1e3 + (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1e3;
}
} // namespace

int main(int argc, char **argv) {
	if (argc < 4) {
		std::fprintf(stderr, "usage: detent_relay_window_bench DIR NAMESPACES KEYS MOVE...\n");
		return 2;
	}
	try {
		const std::filesystem::path dir = argv[1];
		const std::size_t spaces = std::stoull(argv[2]), keys = std::stoull(argv[3]);
		std::filesystem::remove_all(dir);
		std::filesystem::create_directories(dir);
		::chmod(dir.c_str(), 0700);
		Upstream up;
		std::vector<std::string> names;
		for (std::size_t i = 0; i < spaces; ++i) {
			char name[16];
			std::snprintf(name, sizeof name, "app%03zu", i);
			names.emplace_back(name);
			up.create(names.back(), keys);
		}
		std::vector<r::Position> every;
		for (const auto &name : names)
			every.push_back({name, std::nullopt, true});
		const std::vector<r::Position> first{every.front()};
		r::NamespaceRelay relay(std::make_shared<c::PosixIO>(dir, 0644), cluster, up);
		relay.sync(names);
		std::printf("%zu namespaces of %zu keys; start: heap %.0f MB, resident %.0f MB, peak %.0f MB\n", spaces, keys, heap_mb(), resident_mb(),
					peak_mb());
		std::uint64_t sent = 0;
		auto step = [&](const char *what, const std::vector<r::Position> &asked) {
			const auto wall = std::chrono::steady_clock::now();
			const auto cpu = cpu_ms();
			const auto body = relay.frames(asked);
			const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - wall).count();
			sent = body.size();
			std::printf("%-8s build %7.0f ms wall %7.0f ms cpu; heap %6.0f MB, resident %6.0f MB, peak %6.0f MB; "
						"builds %llu, answer %llu B\n",
						what, ms, cpu_ms() - cpu, heap_mb(), resident_mb(), peak_mb(),
						static_cast<unsigned long long>(relay.pages().builds), static_cast<unsigned long long>(sent));
			std::fflush(stdout);
		};
		step("cold", every);
		for (int i = 4; i < argc; ++i) {
			const std::string move = argv[i];
			const auto count = std::stoull(move.substr(1));
			std::vector<std::size_t> changed;
			if (move[0] == 'c') {
				const auto first = (keys / 7 * i) % (keys - count + 1);
				for (std::size_t k = first; k < first + count; ++k)
					changed.push_back(k);
			} else
				for (std::size_t k = 0; k < count; ++k)
					changed.push_back((k * (keys / count) + i) % keys);
			std::sort(changed.begin(), changed.end());
			up.write(names.front(), changed, 1000 * i);
			relay.sync(names);
			step(move.c_str(), first);
		}
		std::filesystem::remove_all(dir);
	} catch (const std::exception &error) {
		std::fprintf(stderr, "detent_relay_window_bench: %s\n", error.what());
		return 1;
	}
	return 0;
}
