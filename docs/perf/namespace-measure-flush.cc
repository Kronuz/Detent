// Device flushes per sync group (namespace-measure T4(7)). Built on completion's probe helpers
// (bench/probe.h at completion:ab0f551): FILES new files of FILE_KB are written, then committed
// one way, alone, ROUNDS times; the block device's completed-flush counter is read around each
// commit. Linux only: /sys/block/<disk>/stat field 16, flush requests completed (kernel 5.5 and later).
//
//   namespace-measure-flush DIRECTORY FILES ROUNDS [FILE_KB]
#include "probe.h"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/sysmacros.h>

namespace {
using namespace probe;

// The whole disk's stat file for the device holding `path` (a partition reports to its disk).
std::filesystem::path disk_stat(const std::filesystem::path &path) {
	struct stat st{};
	if (::stat(path.c_str(), &st) != 0)
		throw std::runtime_error("stat " + path.string());
	const auto dev = std::filesystem::path("/sys/dev/block") /
					 (std::to_string(major(st.st_dev)) + ":" + std::to_string(minor(st.st_dev)));
	const auto real = std::filesystem::canonical(dev);
	if (std::filesystem::exists(real / "partition"))
		return real.parent_path() / "stat";
	return real / "stat";
}

std::uint64_t flushes(const std::filesystem::path &stat) {
	std::ifstream in(stat);
	std::vector<std::uint64_t> fields;
	for (std::uint64_t v; in >> v;)
		fields.push_back(v);
	if (fields.size() < 17)
		throw std::runtime_error(stat.string() + " has no flush fields");
	return fields[15]; // field 16, flush requests completed (field 15 is discard ticks)
}

struct Variant {
	const char *name;
	void (*commit)(j::IO &, Batch &);
	std::vector<double> ms, flushes;
};
} // namespace

int main(int argc, char **argv) {
	try {
		if (argc < 4) {
			std::cerr << "usage: namespace-measure-flush DIRECTORY FILES ROUNDS [FILE_KB]\n";
			return 2;
		}
		const std::filesystem::path parent = argv[1];
		const int files = std::atoi(argv[2]), rounds = std::atoi(argv[3]);
		const std::uint64_t bytes = (argc > 4 ? std::atoll(argv[4]) : 1) * 1024;
		ScratchDirectory scratch(parent);
		j::PosixIO io(scratch.path());
		const auto stat = disk_stat(scratch.path());
		std::vector<Variant> variants{
			{"batched (IO::sync_files) + directory",
			 [](j::IO &io, Batch &b) {
				 io.sync_files(b.pointers());
				 io.sync_directory();
			 }, {}, {}},
			{"one full sync each + directory",
			 [](j::IO &io, Batch &b) {
				 for (auto &f : b.files)
					 f->sync();
				 io.sync_directory();
			 }, {}, {}},
		};
		std::cout << files << " files of " << bytes / 1024 << " KB in " << parent.string() << ", " << rounds
				  << " rounds, counter " << stat.string() << "\n";
		for (int round = 0; round < rounds; ++round)
			for (std::size_t v = 0; v < variants.size(); ++v) {
				auto &variant = variants[(v + std::size_t(round)) % variants.size()];
				auto batch = make_batch(io, "f-" + std::to_string(round), files, bytes);
				::sync(); // the writes' own writeback stays outside the counted commit
				const auto before = flushes(stat);
				const auto start = Clock::now();
				variant.commit(io, batch);
				variant.ms.push_back(since_ms(start));
				variant.flushes.push_back(double(flushes(stat) - before));
				discard(io, batch);
			}
		std::printf("%-40s %10s %12s %12s %12s\n", "variant", "median ms", "med flushes", "min flushes",
					"max flushes");
		for (auto &v : variants) {
			auto f = v.flushes;
			const double median_ms = quantile(v.ms, 0.5), median_f = quantile(f, 0.5);
			std::printf("%-40s %10.1f %12.0f %12.0f %12.0f\n", v.name, median_ms, median_f, f.front(), f.back());
		}
	} catch (const std::exception &e) {
		std::cerr << "namespace-measure-flush: " << e.what() << '\n';
		return 1;
	}
}
